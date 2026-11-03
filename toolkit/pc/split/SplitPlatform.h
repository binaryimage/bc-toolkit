#pragma once
// The few OS pieces Split needs: named shared memory, a named wake-up signal, process checks, clocks.
// Windows uses file mappings + auto-reset events; Linux (tests only) uses shm_open + POSIX semaphores.
#include <cstdint>
#include <string>

namespace split
{
    class SharedRegion
    {
    public:
        SharedRegion() = default;
        ~SharedRegion();
        SharedRegion (const SharedRegion&) = delete;
        SharedRegion& operator= (const SharedRegion&) = delete;

        /** create (or attach to an existing) region; existed = it was already there */
        bool create (const char* name, size_t size, bool& existed);
        /** attach to a region someone else created */
        bool open (const char* name, size_t size);
        void close();
        void* data() const { return ptr; }
        /** host only: remove the name so a fresh host starts clean (no-op on Windows) */
        static void unlink (const char* name);

    private:
        void* ptr = nullptr;
        size_t bytes = 0;
        intptr_t handle = 0;
    };

    /** A named auto-reset signal: many signal() calls before a wait() wake it once. */
    class Signal
    {
    public:
        Signal() = default;
        ~Signal();
        Signal (const Signal&) = delete;
        Signal& operator= (const Signal&) = delete;
        bool openOrCreate (const std::string& name);
        void close();
        void signal();
        /** true when signalled, false on timeout */
        bool wait (int timeoutMs);
        static void unlink (const std::string& name);
    private:
        intptr_t handle = 0;
    };

    uint32_t currentPid();
    bool processAlive (uint32_t pid);
    uint32_t tickMs();
    int64_t nowNs();
    void sleepMs (int ms);
    /** run an executable with arguments, detached */
    bool launch (const std::string& exePath, const std::string& args);
    /** this process's exe name without folder or ".exe" */
    std::string processName();
    /** raise the calling thread to audio priority (MMCSS "Pro Audio" on Windows) */
    void makeThreadRealtime();
    /** 1 ms sleep resolution for the test clock (Windows defaults to ~15 ms) */
    void preciseTimers();

    std::string signalName (int clientIndex);
}
