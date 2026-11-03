#include "WinLoopback.h"

#if defined (_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <thread>
#include <atomic>
#include <vector>
#include <cstring>

#pragma comment (lib, "ole32.lib")

namespace tkwin
{
struct Loop
{
    LoopbackCallback cb = nullptr; void* ctx = nullptr;
    std::atomic<bool> quit { false }, ok { false }, ready { false };
    std::thread th;

    void run()
    {
        HRESULT hr = CoInitializeEx (nullptr, COINIT_MULTITHREADED);
        bool coInit = SUCCEEDED (hr);
        IMMDeviceEnumerator* en = nullptr; IMMDevice* dev = nullptr; IAudioClient* ac = nullptr;
        IAudioCaptureClient* cc = nullptr; WAVEFORMATEX* wf = nullptr;
        do
        {
            if (FAILED (CoCreateInstance (__uuidof (MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof (IMMDeviceEnumerator), (void**) &en))) break;
            if (FAILED (en->GetDefaultAudioEndpoint (eRender, eConsole, &dev))) break;
            if (FAILED (dev->Activate (__uuidof (IAudioClient), CLSCTX_ALL, nullptr, (void**) &ac))) break;
            if (FAILED (ac->GetMixFormat (&wf))) break;
            if (FAILED (ac->Initialize (AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 2000000, 0, wf, nullptr))) break;
            if (FAILED (ac->GetService (__uuidof (IAudioCaptureClient), (void**) &cc))) break;
            if (FAILED (ac->Start())) break;
            ok = true;
        } while (false);
        ready = true;

        if (ok)
        {
            const int ch = wf->nChannels, sr = (int) wf->nSamplesPerSec, bits = wf->wBitsPerSample;
            bool isFloat = wf->wFormatTag == WAVE_FORMAT_IEEE_FLOAT || (wf->wFormatTag == WAVE_FORMAT_EXTENSIBLE && bits == 32);
            std::vector<float> L, R;
            while (! quit)
            {
                Sleep (8);
                UINT32 packet = 0;
                while (! quit && SUCCEEDED (cc->GetNextPacketSize (&packet)) && packet > 0)
                {
                    BYTE* data = nullptr; UINT32 frames = 0; DWORD flags = 0;
                    if (FAILED (cc->GetBuffer (&data, &frames, &flags, nullptr, nullptr))) break;
                    L.assign (frames, 0.0f); R.assign (frames, 0.0f);
                    if (! (flags & AUDCLNT_BUFFERFLAGS_SILENT) && data != nullptr)
                    {
                        for (UINT32 i = 0; i < frames; ++i)
                        {
                            float l = 0, r = 0;
                            if (isFloat)
                            {
                                const float* f = reinterpret_cast<const float*> (data) + (size_t) i * ch;
                                l = f[0]; r = ch > 1 ? f[1] : f[0];
                            }
                            else if (bits == 16)
                            {
                                const short* s = reinterpret_cast<const short*> (data) + (size_t) i * ch;
                                l = s[0] / 32768.0f; r = (ch > 1 ? s[1] : s[0]) / 32768.0f;
                            }
                            else if (bits == 24 || bits == 32)
                            {
                                const int bpsm = bits / 8;
                                auto rd = [&] (int c) { int v = 0; std::memcpy (&v, data + ((size_t) i * ch + c) * bpsm, (size_t) bpsm); if (bits == 24) v = (v << 8) >> 8; return v / (bits == 24 ? 8388608.0f : 2147483648.0f); };
                                l = rd (0); r = ch > 1 ? rd (1) : l;
                            }
                            L[i] = l; R[i] = r;
                        }
                    }
                    cc->ReleaseBuffer (frames);
                    if (cb != nullptr && frames > 0) cb (ctx, L.data(), R.data(), (int) frames, sr);
                }
            }
            ac->Stop();
        }
        if (cc) cc->Release();
        if (ac) ac->Release();
        if (dev) dev->Release();
        if (en) en->Release();
        if (wf) CoTaskMemFree (wf);
        if (coInit) CoUninitialize();
    }
};

void* loopbackStart (LoopbackCallback cb, void* ctx)
{
    auto* l = new Loop();
    l->cb = cb; l->ctx = ctx;
    l->th = std::thread ([l] { l->run(); });
    while (! l->ready) Sleep (5);
    if (! l->ok) { l->quit = true; l->th.join(); delete l; return nullptr; }
    return l;
}

void loopbackStop (void* h)
{
    if (h == nullptr) return;
    auto* l = static_cast<Loop*> (h);
    l->quit = true;
    if (l->th.joinable()) l->th.join();
    delete l;
}

bool loopbackSupported() { return true; }
}

#else
namespace tkwin
{
void* loopbackStart (LoopbackCallback, void*) { return nullptr; }
void loopbackStop (void*) {}
bool loopbackSupported() { return false; }
}
#endif
