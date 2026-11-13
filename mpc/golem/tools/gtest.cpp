// gtest.cpp - functional checks for Golem: pitch, touch patching, sequencer.
#include "mpc_vst2_abi.h"
#include <dlfcn.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include <thread>
#include <chrono>
#include <string>
#include <complex>
static VstTimeInfo ti;
static VstIntPtr host(AEffect*, VstInt32 op, VstInt32, VstIntPtr, void*, float) { if (op == 1) return 2400; if (op == 7) return (VstIntPtr)&ti; return 0; }
AEffect* e;
VstIntPtr D(int op, int i, VstIntPtr v, void* p, float o) { return e->dispatcher(e, op, i, v, p, o); }
int idx(const char* name) { for (int i = 0; i < e->numParams; i++) { char n[64] = {0}; D(effGetParamName, i, 0, n, 0); if (!strcmp(n, name)) return i; } printf("no param %s\n", name); exit(1); }
std::string disp(int i) { char b[64] = {0}; D(effGetParamDisplay, i, 0, b, 0); return b; }
void note(int on, int n) { VstMidiEvent m; memset(&m, 0, sizeof m); m.type = kVstMidiType; m.byteSize = sizeof m; m.midiData[0] = (char)(on ? 0x90 : 0x80); m.midiData[1] = (char)n; m.midiData[2] = 100;
  char mem[sizeof(VstEvents) + 16]; VstEvents* ve = (VstEvents*)mem; ve->numEvents = 1; ve->events[0] = (VstEvent*)&m; D(effProcessEvents, 0, 0, ve, 0); }
std::vector<float> render(int frames) { std::vector<float> out; float L[128], R[128]; float* o[2] = {L, R}; for (int p = 0; p < frames; p += 128) { ti.ppqPos += 128.0 / 44100 * 2; e->processReplacing(e, nullptr, o, 128); out.insert(out.end(), L, L + 128); } return out; }
float peakHz(const std::vector<float>& x, int start, int n, float lo, float hi) { float best = 0, bf = 0; for (float f = lo; f < hi; f += 0.5f) { std::complex<double> s = 0; for (int i = 0; i < n; i += 2) s += std::polar((double)x[start + i], -2 * M_PI * f * i / 44100); if (std::abs(s) > best) { best = std::abs(s); bf = f; } } return bf; }
int main(int argc, char** argv)
{
    void* h = dlopen(argv[1], RTLD_NOW); e = ((AEffect * (*)(audioMasterCallback))dlsym(h, "VSTPluginMain"))(host);
    memset(&ti, 0, sizeof ti); ti.sampleRate = 44100; ti.tempo = 120; ti.flags = 0x400 | 0x200;
    D(effOpen, 0, 0, 0, 0); D(effSetSampleRate, 0, 0, 0, 44100); D(effSetBlockSize, 0, 128, 0, 0); D(effMainsChanged, 0, 1, 0, 0);
    // 1. pitch: C3 (48) should be ~130.8 Hz with VCO1 saw, filter open
    e->setParameter(e, idx("Cutoff"), 1.0f);
    note(1, 48); auto x = render(44100); note(0, 48);
    printf("pitch: peak %.1f Hz (want 130.8)\n", peakHz(x, 8000, 16384, 60, 300));
    // 2. touch patching: S&H (src 13 of 25) -> VCF Cutoff (dst 7 of 20)
    e->setParameter(e, idx("Patch Out"), 13.0f / 25.0f);
    e->setParameter(e, idx("Patch In"), 7.0f / 20.0f);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    printf("after tap-tap: C1 From=%s C1 To=%s C1 Amt=%s | Jack Out 13=%s Jack In 7=%s | picks %s / %s\n", disp(idx("C1 From")).c_str(), disp(idx("C1 To")).c_str(),
           disp(idx("C1 Amt")).c_str(), disp(idx("Jack Out 13")).c_str(), disp(idx("Jack In 7")).c_str(), disp(idx("Patch Out")).c_str(), disp(idx("Patch In")).c_str());
    // second cable from S&H -> Pan makes the S&H jack "split" (31)
    e->setParameter(e, idx("Patch Out"), 13.0f / 25.0f); e->setParameter(e, idx("Patch In"), 20.0f / 20.0f);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    printf("split: C2 %s->%s, Jack Out 13=%s, Jack In 20=%s\n", disp(idx("C2 From")).c_str(), disp(idx("C2 To")).c_str(), disp(idx("Jack Out 13")).c_str(), disp(idx("Jack In 20")).c_str());
    // tap a lit input alone -> unplug
    e->setParameter(e, idx("Patch In"), 7.0f / 20.0f);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    printf("unplug VCF Cutoff: C1 %s->%s, Jack In 7=%s, Jack Out 13=%s\n", disp(idx("C1 From")).c_str(), disp(idx("C1 To")).c_str(), disp(idx("Jack In 7")).c_str(), disp(idx("Jack Out 13")).c_str());
    e->setParameter(e, idx("Unplug All"), 1.0f); std::this_thread::sleep_for(std::chrono::milliseconds(200));
    printf("unplug all: C2 %s->%s, Jack Out 13=%s\n", disp(idx("C2 From")).c_str(), disp(idx("C2 To")).c_str(), disp(idx("Jack Out 13")).c_str());
    // 3. sequencer: Free mode 8 Hz, steps 0/+7/+12 semis pattern, check pitch sequence
    e->setParameter(e, idx("Seq Mode"), 2.0f / 3.0f);
    e->setParameter(e, idx("Seq Length"), (3.0f - 1) / 15.0f);
    e->setParameter(e, idx("Seq Range"), 0.0f);   // 1 octave
    int A1 = idx("A1"); e->setParameter(e, A1, 0.0f); e->setParameter(e, A1 + 1, 7.0f / 12.0f); e->setParameter(e, A1 + 2, 1.0f);
    e->setParameter(e, idx("Seq Free Hz"), 0.3f);
    printf("seq rate %s, A2=%s A3=%s\n", disp(idx("Seq Free Hz")).c_str(), disp(A1 + 1).c_str(), disp(A1 + 2).c_str());
    x = render(44100 * 2);
    // find step length from display
    for (int k = 0; k < 16; k++) { int st = 200 + k * 4410; printf("  t=%.2fs peak %.1f Hz\n", st / 44100.0, peakHz(x, st, 2048, 100, 300)); }
    return 0;
}
