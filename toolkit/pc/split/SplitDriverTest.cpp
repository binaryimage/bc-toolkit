// Opens "BC Split" exactly the way an ASIO host does (registry -> CLSID -> CoCreateInstance) twice,
// as two apps, and checks audio goes both ways through the Split Host (run it with --test first).
#ifndef NOMINMAX
 #define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <cstdio>
#include <cmath>
#include <atomic>
#include <string>
#include "asiosys.h"
#include "asio.h"
#include "iasiodrv.h"

static int fails = 0, passes = 0;
static void check (bool ok, const char* what, const std::string& d = {})
{
    std::printf ("%s  [%d-bit] %s%s%s%s\n", ok ? "PASS" : "FAIL", (int) sizeof (void*) * 8, what, d.empty() ? "" : "   (", d.c_str(), d.empty() ? "" : ")");
    (ok ? passes : fails)++;
}

struct AppState { ASIOBufferInfo bufs[16]; long size = 0; std::atomic<int> blocks { 0 }; std::atomic<double> peak0 { 0 }, mean4 { 0 }; bool write = false; };
static AppState A, B;

static void process (AppState& s, long idx)
{
    auto* in0 = (const int*) s.bufs[0].buffers[idx];
    auto* in4 = (const int*) s.bufs[4].buffers[idx];
    double pk = 0, sum = 0;
    for (long i = 0; i < s.size; ++i) { pk = std::max (pk, std::abs (in0[i] / 2147483648.0)); sum += in4[i] / 2147483648.0; }
    s.peak0 = pk; s.mean4 = sum / s.size;
    auto* out0 = (int*) s.bufs[8].buffers[idx];
    for (long i = 0; i < s.size; ++i) out0[i] = s.write ? (int) (0.25 * 2147483647.0) : 0;
    ++s.blocks;
}

static void swA (long idx, ASIOBool) { process (A, idx); }
static ASIOTime* swTiA (ASIOTime*, long idx, ASIOBool) { process (A, idx); return nullptr; }
static long msgA (long sel, long val, void*, double*) { if (sel == kAsioSelectorSupported) return val == kAsioSupportsTimeInfo || val == kAsioResetRequest ? 1 : 0; return sel == kAsioSupportsTimeInfo ? 1 : 0; }
static void swB (long idx, ASIOBool) { process (B, idx); }
static ASIOTime* swTiB (ASIOTime*, long idx, ASIOBool) { process (B, idx); return nullptr; }
static long msgB (long, long, void*, double*) { return 0; }   // an old-style host: plain bufferSwitch only
static void srChanged (ASIOSampleRate) {}

static IASIO* open (CLSID& clsid)
{
    IASIO* d = nullptr;
    HRESULT hr = CoCreateInstance (clsid, nullptr, CLSCTX_INPROC_SERVER, clsid, (void**) &d);
    return SUCCEEDED (hr) ? d : nullptr;
}

int main()
{
    CoInitialize (nullptr);
    HKEY k; wchar_t clsidStr[64] {}; DWORD sz = sizeof (clsidStr);
    bool reg = RegOpenKeyExW (HKEY_LOCAL_MACHINE, L"SOFTWARE\\ASIO\\BC Split", 0, KEY_READ, &k) == ERROR_SUCCESS
               && RegQueryValueExW (k, L"CLSID", nullptr, nullptr, (BYTE*) clsidStr, &sz) == ERROR_SUCCESS;
    check (reg, "BC Split is in the ASIO driver list");
    if (! reg) return 1;
    CLSID clsid; CLSIDFromString (clsidStr, &clsid);

    IASIO* a = open (clsid);
    IASIO* b = open (clsid);
    check (a != nullptr && b != nullptr, "two apps can load the driver");
    if (! a || ! b) return 1;
    char name[64] {}; a->getDriverName (name);
    check (std::string (name) == "BC Split", "driver name", name);
    if (a->init (GetDesktopWindow()) != ASIOTrue) { char e[128] {}; a->getErrorMessage (e); check (false, "app A init (connects to the host)", e); return 1; }
    check (true, "app A init (connects to the host)");
    if (b->init (GetDesktopWindow()) != ASIOTrue) { char e[128] {}; b->getErrorMessage (e); check (false, "app B init", e); return 1; }
    check (true, "app B init");

    long ins = 0, outs = 0, mn, mx, pref, gran; ASIOSampleRate sr = 0;
    a->getChannels (&ins, &outs); a->getBufferSize (&mn, &mx, &pref, &gran); a->getSampleRate (&sr);
    check (ins == 8 && outs == 8 && pref == 256 && sr == 48000, "format matches the host", std::to_string (ins) + " in, " + std::to_string (outs) + " out, " + std::to_string (pref) + " @ " + std::to_string ((int) sr));
    ASIOChannelInfo ci {}; ci.channel = 0; ci.isInput = ASIOTrue; a->getChannelInfo (&ci);
    check (std::string (ci.name) == "Test sine 1" && ci.type == ASIOSTInt32LSB, "channel names come from the real interface", ci.name);

    ASIOCallbacks cbA { swA, srChanged, msgA, swTiA }, cbB { swB, srChanged, msgB, swTiB };
    for (int i = 0; i < 16; ++i)
    {
        A.bufs[i].isInput = B.bufs[i].isInput = i < 8 ? ASIOTrue : ASIOFalse;
        A.bufs[i].channelNum = B.bufs[i].channelNum = i % 8;
    }
    A.size = B.size = pref; A.write = true;
    check (a->createBuffers (A.bufs, 16, pref, &cbA) == ASE_OK && b->createBuffers (B.bufs, 16, pref, &cbB) == ASE_OK, "both create 8+8 buffers");
    check (a->createBuffers (A.bufs, 16, pref * 2, &cbA) == ASE_InvalidMode, "a different buffer size is refused");
    a->createBuffers (A.bufs, 16, pref, &cbA);
    check (a->start() == ASE_OK && b->start() == ASE_OK, "both start");
    Sleep (1500);
    check (A.blocks > 200 && B.blocks > 200, "both get callbacks (time-info host and plain host)", std::to_string (A.blocks.load()) + " / " + std::to_string (B.blocks.load()));
    check (std::abs (A.peak0 - 0.5) < 0.02, "app A hears input 1", std::to_string (A.peak0.load()));
    check (std::abs (B.mean4 - 0.25) < 0.01, "app B hears app A's output through the mix", std::to_string (B.mean4.load()));
    long il = 0, ol = 0; a->getLatencies (&il, &ol);
    check (ol == 256 * 3, "reported output latency includes Split's two extra buffers", std::to_string (ol));
    check (a->canSampleRate (44100) == ASE_NoClock, "can't change the rate under another app");
    ASIOSamples pos {}; ASIOTimeStamp ts {};
    check (a->getSamplePosition (&pos, &ts) == ASE_OK && (pos.lo != 0 || pos.hi != 0), "sample position advances");
    a->stop(); b->stop();
    a->disposeBuffers(); b->disposeBuffers();
    a->Release(); b->Release();
    CoUninitialize();
    std::printf ("\n%d passed, %d failed\n", passes, fails);
    return fails == 0 ? 0 : 1;
}
