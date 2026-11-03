// "BC Split": an ASIO driver that doesn't talk to hardware. Every app that opens it (Ableton,
// Toolkit, Rocksmith through RS_ASIO...) gets a slot in the Split Host, which owns the real interface.
// Built twice: 64-bit for Ableton and Toolkit, 32-bit for Rocksmith.
#ifndef NOMINMAX
 #define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <olectl.h>
#include <string>
#include <vector>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <atomic>

#include "asiosys.h"
#include "asio.h"
#include "iasiodrv.h"

#include "SplitCore.h"

// {6F3B8C2E-1D47-4A9B-9E3C-5B2A7D81F4C6}
static const CLSID CLSID_Split = { 0x6f3b8c2e, 0x1d47, 0x4a9b, { 0x9e, 0x3c, 0x5b, 0x2a, 0x7d, 0x81, 0xf4, 0xc6 } };
static const wchar_t* kClsidString = L"{6F3B8C2E-1D47-4A9B-9E3C-5B2A7D81F4C6}";

static HINSTANCE g_module = nullptr;
static std::atomic<long> g_objects { 0 }, g_locks { 0 };

static std::wstring moduleDir()
{
    wchar_t buf[MAX_PATH] {};
    GetModuleFileNameW (g_module, buf, MAX_PATH);
    std::wstring p (buf);
    auto s = p.find_last_of (L"\\/");
    return s == std::wstring::npos ? std::wstring() : p.substr (0, s);
}

static std::string utf8 (const std::wstring& w)
{
    int n = WideCharToMultiByte (CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s ((size_t) (n > 0 ? n - 1 : 0), '\0');
    if (n > 1) WideCharToMultiByte (CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
    return s;
}

static void toAsioSamples (int64_t v, ASIOSamples* s) { s->hi = (unsigned long) ((uint64_t) v >> 32); s->lo = (unsigned long) ((uint64_t) v & 0xffffffffu); }
static void toAsioStamp (int64_t v, ASIOTimeStamp* s) { s->hi = (unsigned long) ((uint64_t) v >> 32); s->lo = (unsigned long) ((uint64_t) v & 0xffffffffu); }

// ============================================================ the driver object

class SplitDriver : public IASIO, private split::ClientCore::Handler
{
public:
    SplitDriver() { ++g_objects; }
    ~SplitDriver() { disposeBuffers(); core.disconnect(); --g_objects; }

    // ---------------------------------------------------------------- IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, void** ppv) override
    {
        if (ppv == nullptr) return E_POINTER;
        // ASIO hosts ask for the driver's own CLSID as the interface id
        if (riid == IID_IUnknown || riid == CLSID_Split) { *ppv = static_cast<IASIO*> (this); AddRef(); return S_OK; }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG) ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { long r = --refs; if (r == 0) delete this; return (ULONG) r; }

    // ---------------------------------------------------------------- IASIO
    ASIOBool init (void*) override
    {
        if (core.isConnected()) return ASIOTrue;
        std::string err;
        const std::string host = utf8 (moduleDir() + L"\\" + std::wstring (L"BC Split Host.exe"));
        if (! core.connect (host, 8000, err)) { setError (err); return ASIOFalse; }
        return ASIOTrue;
    }

    void getDriverName (char* name) override { std::strncpy (name, split::kDriverName, 31); name[31] = 0; }
    long getDriverVersion() override { return 1; }
    void getErrorMessage (char* s) override { std::strncpy (s, lastError.c_str(), 123); s[123] = 0; }

    ASIOError start() override
    {
        if (! buffersReady) return ASE_NotPresent;
        core.setRunning (true);
        return ASE_OK;
    }

    ASIOError stop() override { core.setRunning (false); return ASE_OK; }

    ASIOError getChannels (long* numIn, long* numOut) override
    {
        auto* sh = core.shared();
        if (sh == nullptr) return ASE_NotPresent;
        *numIn = sh->numIns; *numOut = sh->numOuts;
        return ASE_OK;
    }

    ASIOError getLatencies (long* inLat, long* outLat) override
    {
        auto* sh = core.shared();
        if (sh == nullptr) return ASE_NotPresent;
        // inputs arrive as the interface delivers them; outputs go out kOutputDelayBlocks buffers later (the host mixes every app)
        *inLat = sh->inLatency;
        *outLat = sh->outLatency + sh->bufferSize * split::kOutputDelayBlocks;
        return ASE_OK;
    }

    ASIOError getBufferSize (long* mn, long* mx, long* pref, long* gran) override
    {
        auto* sh = core.shared();
        if (sh == nullptr) return ASE_NotPresent;
        *mn = *mx = *pref = sh->bufferSize;
        *gran = 0;
        return ASE_OK;
    }

    ASIOError canSampleRate (ASIOSampleRate r) override
    {
        auto* sh = core.shared();
        if (sh == nullptr) return ASE_NotPresent;
        if (std::abs (r - sh->sampleRate) < 0.5) return ASE_OK;
        // another rate only when nobody else is using Split (or we'd yank them)
        int others = 0;
        for (int i = 0; i < split::kMaxClients; ++i)
            if (i != core.slotIndex() && sh->clients[i].state.load() != split::clientFree) ++others;
        const bool common = r == 44100 || r == 48000 || r == 88200 || r == 96000;
        return others == 0 && common ? ASE_OK : ASE_NoClock;
    }

    ASIOError getSampleRate (ASIOSampleRate* r) override
    {
        auto* sh = core.shared();
        if (sh == nullptr) return ASE_NotPresent;
        *r = sh->sampleRate;
        return ASE_OK;
    }

    ASIOError setSampleRate (ASIOSampleRate r) override
    {
        auto* sh = core.shared();
        if (sh == nullptr) return ASE_NotPresent;
        if (r == 0 || std::abs (r - sh->sampleRate) < 0.5) return ASE_OK;   // 0 = external clock request: we're always internal
        if (canSampleRate (r) != ASE_OK) { setError ("Other apps are using Split at " + std::to_string ((int) sh->sampleRate) + " Hz."); return ASE_NoClock; }
        bool tempSlot = core.slot() == nullptr;
        std::string err;
        if (tempSlot && ! core.attach (split::processName(), nullptr, err)) { setError (err); return ASE_NoClock; }
        bool ok = core.requestRate (r, 6000);
        if (tempSlot) core.detach();
        if (! ok) { setError ("The interface wouldn't switch to " + std::to_string ((int) r) + " Hz."); return ASE_NoClock; }
        return ASE_OK;
    }

    ASIOError getClockSources (ASIOClockSource* clocks, long* num) override
    {
        if (clocks == nullptr || num == nullptr || *num < 1) return ASE_InvalidParameter;
        clocks[0].index = 0; clocks[0].associatedChannel = -1; clocks[0].associatedGroup = -1; clocks[0].isCurrentSource = ASIOTrue;
        std::strcpy (clocks[0].name, "Split host");
        *num = 1;
        return ASE_OK;
    }

    ASIOError setClockSource (long ref) override { return ref == 0 ? ASE_OK : ASE_InvalidParameter; }

    ASIOError getSamplePosition (ASIOSamples* pos, ASIOTimeStamp* stamp) override
    {
        auto* sh = core.shared();
        if (sh == nullptr) return ASE_NotPresent;
        int64_t s = sh->seq.load();
        int64_t b = std::max<int64_t> (0, s - 1);
        if (pos) toAsioSamples (b * sh->bufferSize, pos);
        if (stamp) toAsioStamp (sh->blockTimeNs[b % split::kSlots], stamp);
        return ASE_OK;
    }

    ASIOError getChannelInfo (ASIOChannelInfo* info) override
    {
        auto* sh = core.shared();
        if (sh == nullptr) return ASE_NotPresent;
        const long n = info->isInput ? sh->numIns : sh->numOuts;
        if (info->channel < 0 || info->channel >= n) return ASE_InvalidParameter;
        info->type = ASIOSTInt32LSB;
        info->channelGroup = 0;
        info->isActive = ASIOFalse;
        for (auto& c : chans) if (c.input == (info->isInput != 0) && c.channel == info->channel) info->isActive = ASIOTrue;
        const char* nm = info->isInput ? sh->inNames[info->channel] : sh->outNames[info->channel];
        std::strncpy (info->name, nm[0] ? nm : (info->isInput ? "In" : "Out"), 31); info->name[31] = 0;
        return ASE_OK;
    }

    ASIOError createBuffers (ASIOBufferInfo* infos, long num, long bufferSize, ASIOCallbacks* cb) override
    {
        auto* sh = core.shared();
        if (sh == nullptr) return ASE_NotPresent;
        disposeBuffers();
        if (bufferSize != sh->bufferSize) { setError ("Split runs at " + std::to_string (sh->bufferSize) + " samples; change the buffer in the Split window."); return ASE_InvalidMode; }
        if (cb == nullptr || num <= 0) return ASE_InvalidParameter;
        size = bufferSize;
        int maxOut = 0;
        for (long i = 0; i < num; ++i)
        {
            auto& bi = infos[i];
            const long n = bi.isInput ? sh->numIns : sh->numOuts;
            if (bi.channelNum < 0 || bi.channelNum >= n) { disposeBuffers(); return ASE_InvalidParameter; }
            Chan c; c.input = bi.isInput != 0; c.channel = bi.channelNum;
            c.mem.assign ((size_t) size * 2, 0);
            bi.buffers[0] = c.mem.data();
            bi.buffers[1] = c.mem.data() + size;
            if (! c.input) maxOut = std::max (maxOut, (int) c.channel + 1);
            chans.push_back (std::move (c));
        }
        // the vectors don't move again, so re-point the app at their final addresses
        for (long i = 0; i < num; ++i) { infos[i].buffers[0] = chans[(size_t) i].mem.data(); infos[i].buffers[1] = chans[(size_t) i].mem.data() + size; }
        callbacks = cb;
        timeInfo = cb->asioMessage != nullptr && cb->asioMessage (kAsioSelectorSupported, kAsioSupportsTimeInfo, nullptr, nullptr) == 1
                   && cb->asioMessage (kAsioSupportsTimeInfo, 0, nullptr, nullptr) == 1;
        std::string err;
        if (! core.attach (split::processName(), this, err)) { setError (err); disposeBuffers(); return ASE_NoMemory; }
        core.setOutChannels (maxOut);
        buffersReady = true;
        return ASE_OK;
    }

    ASIOError disposeBuffers() override
    {
        core.setRunning (false);
        core.detach();
        chans.clear();
        callbacks = nullptr;
        buffersReady = false;
        return ASE_OK;
    }

    ASIOError controlPanel() override
    {
        split::launch (utf8 (moduleDir() + L"\\BC Split Host.exe"), "--show");
        return ASE_OK;
    }

    ASIOError future (long selector, void*) override
    {
        switch (selector)
        {
            case kAsioCanTimeInfo: return ASE_SUCCESS;
            default: return ASE_NotPresent;
        }
    }

    ASIOError outputReady() override { return ASE_NotPresent; }

private:
    // ---------------------------------------------------------------- one block, on our worker thread
    void block (int64_t b, int slot) override
    {
        auto* sh = core.shared();
        auto* mine = core.slot();
        if (callbacks == nullptr || sh == nullptr || mine == nullptr) return;
        const long idx = (long) (b & 1);
        for (auto& c : chans)
        {
            if (! c.input) continue;
            const float* src = sh->in[slot][c.channel];
            int32_t* dst = c.mem.data() + idx * size;
            for (long i = 0; i < size; ++i)
            {
                double v = (double) src[i] * 2147483647.0;
                dst[i] = (int32_t) (v >= 2147483647.0 ? 2147483647.0 : v <= -2147483648.0 ? -2147483648.0 : v);
            }
        }
        if (timeInfo && callbacks->bufferSwitchTimeInfo != nullptr)
        {
            ASIOTime t {};
            t.timeInfo.speed = 1.0;
            t.timeInfo.sampleRate = sh->sampleRate;
            toAsioStamp (sh->blockTimeNs[slot], &t.timeInfo.systemTime);
            toAsioSamples (b * size, &t.timeInfo.samplePosition);
            t.timeInfo.flags = kSystemTimeValid | kSamplePositionValid | kSampleRateValid | kSpeedValid;
            callbacks->bufferSwitchTimeInfo (&t, idx, ASIOTrue);
        }
        else callbacks->bufferSwitch (idx, ASIOTrue);
        for (auto& c : chans)
        {
            if (c.input) continue;
            const int32_t* src = c.mem.data() + idx * size;
            float* dst = mine->out[slot][c.channel];
            for (long i = 0; i < size; ++i) dst[i] = (float) ((double) src[i] / 2147483648.0);
        }
    }

    void configChanged() override { if (callbacks && callbacks->asioMessage) callbacks->asioMessage (kAsioResetRequest, 0, nullptr, nullptr); }
    void hostLost() override { setError ("The Split host closed."); configChanged(); }

    void setError (const std::string& e) { lastError = e.substr (0, 123); }

    struct Chan { bool input = false; long channel = 0; std::vector<int32_t> mem; };
    std::atomic<long> refs { 1 };
    split::ClientCore core;
    std::vector<Chan> chans;
    ASIOCallbacks* callbacks = nullptr;
    long size = 0;
    bool timeInfo = false, buffersReady = false;
    std::string lastError { "No error" };
};

// ============================================================ COM plumbing

class Factory : public IClassFactory
{
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, void** ppv) override
    {
        if (riid == IID_IUnknown || riid == IID_IClassFactory) { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 2; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE CreateInstance (IUnknown* outer, REFIID riid, void** ppv) override
    {
        if (outer != nullptr) return CLASS_E_NOAGGREGATION;
        auto* d = new (std::nothrow) SplitDriver();
        if (d == nullptr) return E_OUTOFMEMORY;
        HRESULT hr = d->QueryInterface (riid, ppv);
        d->Release();
        return hr;
    }
    HRESULT STDMETHODCALLTYPE LockServer (BOOL lock) override { if (lock) ++g_locks; else --g_locks; return S_OK; }
};
static Factory g_factory;

extern "C" BOOL WINAPI DllMain (HINSTANCE h, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) { g_module = h; DisableThreadLibraryCalls (h); }
    return TRUE;
}

STDAPI DllGetClassObject (REFCLSID clsid, REFIID riid, void** ppv)
{
    if (clsid != CLSID_Split) return CLASS_E_CLASSNOTAVAILABLE;
    return g_factory.QueryInterface (riid, ppv);
}

STDAPI DllCanUnloadNow() { return g_objects.load() == 0 && g_locks.load() == 0 ? S_OK : S_FALSE; }

static LONG setString (HKEY root, const std::wstring& path, const wchar_t* name, const std::wstring& value)
{
    HKEY k;
    LONG r = RegCreateKeyExW (root, path.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr);
    if (r != ERROR_SUCCESS) return r;
    r = RegSetValueExW (k, name, 0, REG_SZ, (const BYTE*) value.c_str(), (DWORD) ((value.size() + 1) * sizeof (wchar_t)));
    RegCloseKey (k);
    return r;
}

STDAPI DllRegisterServer()
{
    wchar_t path[MAX_PATH] {};
    GetModuleFileNameW (g_module, path, MAX_PATH);
    const std::wstring clsidKey = std::wstring (L"CLSID\\") + kClsidString;
    const std::wstring asioKey = L"SOFTWARE\\ASIO\\BC Split";
    if (setString (HKEY_CLASSES_ROOT, clsidKey, nullptr, L"BC Split") != ERROR_SUCCESS
        || setString (HKEY_CLASSES_ROOT, clsidKey + L"\\InprocServer32", nullptr, path) != ERROR_SUCCESS
        || setString (HKEY_CLASSES_ROOT, clsidKey + L"\\InprocServer32", L"ThreadingModel", L"Apartment") != ERROR_SUCCESS
        || setString (HKEY_LOCAL_MACHINE, asioKey, L"CLSID", kClsidString) != ERROR_SUCCESS
        || setString (HKEY_LOCAL_MACHINE, asioKey, L"Description", L"BC Split") != ERROR_SUCCESS)
        return SELFREG_E_CLASS;
    RegDeleteTreeW (HKEY_LOCAL_MACHINE, L"SOFTWARE\\ASIO\\binaryimage Split");   // the name this driver had in Toolkit 1.1
    return S_OK;
}

STDAPI DllUnregisterServer()
{
    RegDeleteTreeW (HKEY_CLASSES_ROOT, (std::wstring (L"CLSID\\") + kClsidString).c_str());
    RegDeleteTreeW (HKEY_LOCAL_MACHINE, L"SOFTWARE\\ASIO\\BC Split");
    return S_OK;
}
