// synthtorture.cpp <plugin.so> [seconds] [block] - random notes, chords, wheel, bend, sustain pedal,
// random parameter changes and program hops. Reports NaN / blow-ups, peak and CPU.
#include "mpc_vst2_abi.h"
#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <chrono>
#include <algorithm>
static VstTimeInfo ti;
static VstIntPtr host(AEffect*, VstInt32 op, VstInt32, VstIntPtr, void*, float) { if (op == 1) return 2400; if (op == 7) return (VstIntPtr)&ti; return 0; }
int main(int argc, char** argv)
{
    void* h = dlopen(argv[1], RTLD_NOW); if (!h) { printf("%s\n", dlerror()); return 1; }
    AEffect* e = ((AEffect * (*)(audioMasterCallback))dlsym(h, "VSTPluginMain"))(host);
    auto D = [&](int op, int i, VstIntPtr v, void* p, float o) { return e->dispatcher(e, op, i, v, p, o); };
    const int sr = 44100; const double secs = argc > 2 ? atof(argv[2]) : 60; const int blk = argc > 3 ? atoi(argv[3]) : 128;
    D(effOpen, 0, 0, 0, 0); D(effSetSampleRate, 0, 0, 0, sr); D(effSetBlockSize, 0, blk, 0, 0); D(effMainsChanged, 0, 1, 0, 0);
    memset(&ti, 0, sizeof ti); ti.sampleRate = sr; ti.tempo = 120; ti.flags = 0x2 | 0x200 | 0x400;
    unsigned s = 7; auto rnd = [&]() { s = s * 1664525u + 1013904223u; return ((s >> 8) & 0xFFFFFF) / 16777216.0f; };
    std::vector<float> L(blk), R(blk); float* outs[2] = {L.data(), R.data()};
    bool held[128] = {}; float pk = 0; int bad = 0; std::vector<double> bt;
    const int N = (int)(secs * sr);
    for (int pos = 0; pos < N; pos += blk)
    {
        ti.ppqPos = pos / (double)sr * 2.0;
        std::vector<VstMidiEvent> ev;
        auto add = [&](int a, int b, int c) { VstMidiEvent m; memset(&m, 0, sizeof m); m.type = kVstMidiType; m.byteSize = sizeof m; m.deltaFrames = (int)(rnd() * blk); m.midiData[0] = (char)a; m.midiData[1] = (char)b; m.midiData[2] = (char)c; ev.push_back(m); };
        if (rnd() < 0.08f) { int nn = 24 + (int)(rnd() * 84); add(0x90, nn, 1 + (int)(rnd() * 126)); held[nn] = true; }
        if (rnd() < 0.07f) { int nn = 24 + (int)(rnd() * 84); if (held[nn]) { add(0x80, nn, 0); held[nn] = false; } }
        if (rnd() < 0.01f) { int c = (int)(rnd() * 127); add(0xB0, 1, c); }
        if (rnd() < 0.01f) { int b = (int)(rnd() * 16383); add(0xE0, b & 127, b >> 7); }
        if (rnd() < 0.004f) add(0xB0, 64, rnd() < 0.5f ? 127 : 0);
        if (rnd() < 0.002f) for (int k = 0; k < 128; k++) if (held[k]) { add(0x80, k, 0); held[k] = false; }
        if (rnd() < 0.03f) e->setParameter(e, 1 + (int)(rnd() * (e->numParams - 1)), rnd());
        if (rnd() < 0.002f) D(effSetProgram, 0, (int)(rnd() * e->numPrograms), 0, 0);
        std::sort(ev.begin(), ev.end(), [](const VstMidiEvent& a, const VstMidiEvent& b) { return a.deltaFrames < b.deltaFrames; });
        std::vector<char> mem(sizeof(VstEvents) + sizeof(void*) * (ev.size() + 2)); VstEvents* ve = (VstEvents*)mem.data(); ve->numEvents = (int)ev.size();
        for (size_t k = 0; k < ev.size(); k++) ve->events[k] = (VstEvent*)&ev[k];
        if (!ev.empty()) D(effProcessEvents, 0, 0, ve, 0);
        auto a = std::chrono::steady_clock::now(); e->processReplacing(e, nullptr, outs, blk); auto b = std::chrono::steady_clock::now();
        bt.push_back(std::chrono::duration<double, std::micro>(b - a).count());
        for (int i = 0; i < blk; i++) { if (!std::isfinite(L[i]) || !std::isfinite(R[i])) bad++; pk = std::max(pk, std::max(fabsf(L[i]), fabsf(R[i]))); }
    }
    std::sort(bt.begin(), bt.end()); double budget = 1e6 * blk / sr;
    printf("block %d: peak %.1f dB, NaN %d, cpu median %.1f%% p99 %.1f%%\n", blk, 20 * log10(pk + 1e-9), bad, 100 * bt[bt.size() / 2] / budget, 100 * bt[(size_t)(bt.size() * 0.99)] / budget);
    return bad ? 2 : 0;
}
