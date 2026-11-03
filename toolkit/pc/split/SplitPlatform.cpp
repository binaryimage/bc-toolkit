#include "SplitPlatform.h"
#include <chrono>
#include <thread>
#include <cstring>

#if defined (_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #define WIN32_LEAN_AND_MEAN
 #include <windows.h>
 #include <avrt.h>
 #include <mmsystem.h>
 #pragma comment (lib, "avrt.lib")
 #pragma comment (lib, "winmm.lib")
#else
 #include <fcntl.h>
 #include <sys/mman.h>
 #include <sys/stat.h>
 #include <unistd.h>
 #include <semaphore.h>
 #include <signal.h>
 #include <ctime>
 #include <cerrno>
 #include <spawn.h>
 #include <fstream>
 extern char** environ;
#endif

namespace split
{
std::string signalName (int i) { return std::string ("binaryimageSplit.v1.tick.") + std::to_string (i); }

#if defined (_WIN32)
// ------------------------------------------------------------ Windows
static std::wstring wide (const std::string& s)
{
    int n = MultiByteToWideChar (CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w ((size_t) (n > 0 ? n - 1 : 0), L'\0');
    if (n > 1) MultiByteToWideChar (CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return w;
}
static std::wstring localName (const char* n) { return L"Local\\" + wide (n); }

SharedRegion::~SharedRegion() { close(); }

bool SharedRegion::create (const char* name, size_t size, bool& existed)
{
    close();
    HANDLE h = CreateFileMappingW (INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, (DWORD) ((uint64_t) size >> 32), (DWORD) (size & 0xffffffffu), localName (name).c_str());
    if (h == nullptr) return false;
    existed = GetLastError() == ERROR_ALREADY_EXISTS;
    void* p = MapViewOfFile (h, FILE_MAP_ALL_ACCESS, 0, 0, size);
    if (p == nullptr) { CloseHandle (h); return false; }
    handle = (intptr_t) h; ptr = p; bytes = size;
    return true;
}

bool SharedRegion::open (const char* name, size_t size)
{
    close();
    HANDLE h = OpenFileMappingW (FILE_MAP_ALL_ACCESS, FALSE, localName (name).c_str());
    if (h == nullptr) return false;
    void* p = MapViewOfFile (h, FILE_MAP_ALL_ACCESS, 0, 0, size);
    if (p == nullptr) { CloseHandle (h); return false; }
    handle = (intptr_t) h; ptr = p; bytes = size;
    return true;
}

void SharedRegion::close()
{
    if (ptr != nullptr) UnmapViewOfFile (ptr);
    if (handle != 0) CloseHandle ((HANDLE) handle);
    ptr = nullptr; handle = 0; bytes = 0;
}

void SharedRegion::unlink (const char*) {}

Signal::~Signal() { close(); }
bool Signal::openOrCreate (const std::string& name)
{
    close();
    HANDLE h = CreateEventW (nullptr, FALSE, FALSE, localName (name.c_str()).c_str());
    handle = (intptr_t) h;
    return h != nullptr;
}
void Signal::close() { if (handle != 0) CloseHandle ((HANDLE) handle); handle = 0; }
void Signal::signal() { if (handle != 0) SetEvent ((HANDLE) handle); }
bool Signal::wait (int ms) { return handle != 0 && WaitForSingleObject ((HANDLE) handle, (DWORD) ms) == WAIT_OBJECT_0; }
void Signal::unlink (const std::string&) {}

uint32_t currentPid() { return (uint32_t) GetCurrentProcessId(); }

bool processAlive (uint32_t pid)
{
    if (pid == 0) return false;
    HANDLE h = OpenProcess (SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (h == nullptr) return GetLastError() == ERROR_ACCESS_DENIED;   // exists but belongs to someone else
    DWORD r = WaitForSingleObject (h, 0);
    CloseHandle (h);
    return r == WAIT_TIMEOUT;
}

uint32_t tickMs() { return (uint32_t) GetTickCount64(); }

int64_t nowNs()
{
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency (&f); QueryPerformanceCounter (&c);
    return (int64_t) ((double) c.QuadPart * 1.0e9 / (double) f.QuadPart);
}

void sleepMs (int ms) { Sleep ((DWORD) ms); }

bool launch (const std::string& exe, const std::string& args)
{
    std::wstring cmd = L"\"" + wide (exe) + L"\" " + wide (args);
    STARTUPINFOW si {}; si.cb = sizeof (si);
    PROCESS_INFORMATION pi {};
    std::wstring dir = wide (exe);
    auto slash = dir.find_last_of (L"\\/");
    dir = slash == std::wstring::npos ? std::wstring() : dir.substr (0, slash);
    if (! CreateProcessW (nullptr, &cmd[0], nullptr, nullptr, FALSE, DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP, nullptr,
                          dir.empty() ? nullptr : dir.c_str(), &si, &pi))
        return false;
    CloseHandle (pi.hThread); CloseHandle (pi.hProcess);
    return true;
}

std::string processName()
{
    wchar_t buf[MAX_PATH] {};
    GetModuleFileNameW (nullptr, buf, MAX_PATH);
    std::wstring w (buf);
    auto slash = w.find_last_of (L"\\/");
    if (slash != std::wstring::npos) w = w.substr (slash + 1);
    if (w.size() > 4 && _wcsicmp (w.c_str() + w.size() - 4, L".exe") == 0) w.resize (w.size() - 4);
    int n = WideCharToMultiByte (CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s ((size_t) (n > 0 ? n - 1 : 0), '\0');
    if (n > 1) WideCharToMultiByte (CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
    return s;
}

void preciseTimers() { timeBeginPeriod (1); }

void makeThreadRealtime()
{
    DWORD task = 0;
    if (AvSetMmThreadCharacteristicsW (L"Pro Audio", &task) == nullptr)
        SetThreadPriority (GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
}

#else
// ------------------------------------------------------------ POSIX (tests)
static std::string posixName (const std::string& n) { return "/" + n; }

SharedRegion::~SharedRegion() { close(); }

bool SharedRegion::create (const char* name, size_t size, bool& existed)
{
    close();
    int fd = shm_open (posixName (name).c_str(), O_RDWR | O_CREAT | O_EXCL, 0600);
    existed = false;
    if (fd < 0 && errno == EEXIST) { existed = true; fd = shm_open (posixName (name).c_str(), O_RDWR, 0600); }
    if (fd < 0) return false;
    if (! existed && ftruncate (fd, (off_t) size) != 0) { ::close (fd); return false; }
    void* p = mmap (nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    ::close (fd);
    if (p == MAP_FAILED) return false;
    ptr = p; bytes = size;
    return true;
}

bool SharedRegion::open (const char* name, size_t size)
{
    close();
    int fd = shm_open (posixName (name).c_str(), O_RDWR, 0600);
    if (fd < 0) return false;
    struct stat st {};
    if (fstat (fd, &st) != 0 || (size_t) st.st_size < size) { ::close (fd); return false; }
    void* p = mmap (nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    ::close (fd);
    if (p == MAP_FAILED) return false;
    ptr = p; bytes = size;
    return true;
}

void SharedRegion::close() { if (ptr != nullptr) munmap (ptr, bytes); ptr = nullptr; bytes = 0; }
void SharedRegion::unlink (const char* name) { shm_unlink (posixName (name).c_str()); }

Signal::~Signal() { close(); }
bool Signal::openOrCreate (const std::string& name)
{
    close();
    sem_t* s = sem_open (posixName (name).c_str(), O_CREAT, 0600, 0);
    if (s == SEM_FAILED) return false;
    handle = (intptr_t) s;
    return true;
}
void Signal::close() { if (handle != 0) sem_close ((sem_t*) handle); handle = 0; }
void Signal::signal()
{
    if (handle == 0) return;
    int v = 0;
    sem_getvalue ((sem_t*) handle, &v);
    if (v < 1) sem_post ((sem_t*) handle);    // behave like an auto-reset event
}
bool Signal::wait (int ms)
{
    if (handle == 0) return false;
    timespec ts {}; clock_gettime (CLOCK_REALTIME, &ts);
    ts.tv_sec += ms / 1000; ts.tv_nsec += (long) (ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) { ts.tv_sec += 1; ts.tv_nsec -= 1000000000L; }
    while (sem_timedwait ((sem_t*) handle, &ts) != 0) if (errno != EINTR) return false;
    while (sem_trywait ((sem_t*) handle) == 0) {}
    return true;
}
void Signal::unlink (const std::string& name) { sem_unlink (posixName (name).c_str()); }

uint32_t currentPid() { return (uint32_t) getpid(); }
bool processAlive (uint32_t pid) { return pid != 0 && (kill ((pid_t) pid, 0) == 0 || errno == EPERM); }
uint32_t tickMs() { return (uint32_t) std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::steady_clock::now().time_since_epoch()).count(); }
int64_t nowNs() { return (int64_t) std::chrono::duration_cast<std::chrono::nanoseconds> (std::chrono::steady_clock::now().time_since_epoch()).count(); }
void sleepMs (int ms) { std::this_thread::sleep_for (std::chrono::milliseconds (ms)); }

bool launch (const std::string& exe, const std::string& args)
{
    pid_t pid;
    std::string a = args;
    char* argv[] = { const_cast<char*> (exe.c_str()), a.empty() ? nullptr : &a[0], nullptr };
    return posix_spawn (&pid, exe.c_str(), nullptr, nullptr, argv, environ) == 0;
}

std::string processName()
{
    std::ifstream f ("/proc/self/comm");
    std::string s; std::getline (f, s);
    return s;
}

void makeThreadRealtime() {}
void preciseTimers() {}
#endif
}
