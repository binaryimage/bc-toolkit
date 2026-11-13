// drumrender.cpp <plugin.so> <outdir> - plays a 2-bar beat on every program at 120 bpm with the
// transport running (so the Mother sequence plays), sums all outputs, prints peak/CPU like render.
// env PAIRS=1 prints the level on each output pair; WAV=1 writes the stereo mix.
#include "mpc_vst2_abi.h"
#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <chrono>
#include <algorithm>
#include <string>
static double g_ppq = 0; static VstTimeInfo ti;
static VstIntPtr host(AEffect*, VstInt32 op, VstInt32, VstIntPtr, void*, float) { if (op == 1) return 2400; if (op == 7) { memset(&ti, 0, sizeof ti); ti.sampleRate = 44100; ti.tempo = 120; ti.flags = 0x400 | 0x2 | 0x200; ti.ppqPos = g_ppq; return (VstIntPtr)&ti; } return 0; }
int main(int argc, char** argv)
{
    void* h = dlopen(argv[1], RTLD_NOW); AEffect* e = ((AEffect * (*)(audioMasterCallback))dlsym(h, "VSTPluginMain"))(host);
    auto D = [&](int op, int i, VstIntPtr v, void* p, float o) { return e->dispatcher(e, op, i, v, p, o); };
    const int sr = 44100, blk = 128, N = (sr * 4 / 128) * 128, no = e->numOutputs;
    D(effOpen, 0, 0, 0, 0); D(effSetSampleRate, 0, 0, 0, sr); D(effSetBlockSize, 0, blk, 0, 0); D(effMainsChanged, 0, 1, 0, 0);
    // 16th-note grid, 2 bars, pads (note 36 + pad index)
    const char* pat[16] = {
        "x...x...x...x..x",  // kick
        "........x.......",  // kick2
        "....x.......x...",  // snare
        "............x..x",  // clap
        "..x.......x.....",  // rim
        ".............x..",  // tom L
        "..............x.",  // tom M
        "...............x",  // tom H
        "x.x.x.x.x.x.x.x.",  // closed hat
        "......x.......x.",  // open hat
        "x...............",  // cymbal
        ".x.....x.....x..",  // FM
        "................", "................", "................", "................"};
    for (int p = 0; p < e->numPrograms; p++)
    {
        char name[64] = {0}; D(effSetProgram, 0, p, 0, 0); D(effGetProgramName, 0, 0, name, 0);
        D(effMainsChanged, 0, 0, 0, 0); D(effMainsChanged, 0, 1, 0, 0);
        if (getenv("ROUTE")) e->setParameter(e, 1, atoi(getenv("ROUTE")) / 4.0f);
        std::vector<std::vector<float>> bufs(no, std::vector<float>(blk)); std::vector<float*> outs(no);
        for (int c = 0; c < no; c++) outs[c] = bufs[c].data();
        std::vector<float> L(N), R(N); std::vector<double> pairE(no / 2, 0.0); std::vector<double> bt;
        bool bad = false;
        for (int pos = 0; pos < N; pos += blk)
        {
            g_ppq = (double)pos / sr * 2.0;
            std::vector<VstMidiEvent> ev;
            const int s16 = sr / 8;                      // 16th at 120 bpm = 5512.5 samples
            for (int st = 0; st < 32; st++)
            {
                const int t = (int)(st * sr / 8.0);
                if (t < pos || t >= pos + blk || t >= sr * 3) continue;
                for (int d = 0; d < 16; d++) if ((getenv("SOLO") ? (d == atoi(getenv("SOLO")) && st % 8 == 0) : (d < 12 && pat[d][st % 16] == 'x'))) { VstMidiEvent m; memset(&m, 0, sizeof m); m.type = kVstMidiType; m.byteSize = sizeof m; m.deltaFrames = t - pos; m.midiData[0] = (char)0x90; m.midiData[1] = (char)(36 + d); m.midiData[2] = (char)(st % 4 == 0 ? 120 : 90); ev.push_back(m); }
            }
            (void)s16;
            std::vector<char> mem(sizeof(VstEvents) + sizeof(void*) * (ev.size() + 2)); VstEvents* ve = (VstEvents*)mem.data(); ve->numEvents = (int)ev.size();
            for (size_t k = 0; k < ev.size(); k++) ve->events[k] = (VstEvent*)&ev[k];
            if (!ev.empty()) D(effProcessEvents, 0, 0, ve, 0);
            auto a = std::chrono::steady_clock::now(); e->processReplacing(e, nullptr, outs.data(), blk); auto b = std::chrono::steady_clock::now();
            bt.push_back(std::chrono::duration<double, std::micro>(b - a).count());
            for (int i = 0; i < blk; i++)
            {
                float l = 0, r = 0;
                for (int c = 0; c < no; c += 2) { l += bufs[c][i]; r += bufs[c + 1][i]; pairE[c / 2] += bufs[c][i] * bufs[c][i] + bufs[c + 1][i] * bufs[c + 1][i]; }
                if (!std::isfinite(l) || !std::isfinite(r)) bad = true;
                L[pos + i] = l; R[pos + i] = r;
            }
        }
        float pk = 0; double rms = 0; for (int i = 0; i < N; i++) { pk = std::max(pk, std::max(fabsf(L[i]), fabsf(R[i]))); rms += L[i] * L[i]; }
        std::sort(bt.begin(), bt.end());
        printf("p99 %.1f%%  %2d %-18s peak %6.1f dB  rms %6.1f dB %s\n", 100 * bt[(size_t)(bt.size() * 0.99)] / (1e6 * blk / sr), p, name, 20 * log10(pk + 1e-9), 10 * log10(rms / N + 1e-18), bad ? "NAN!" : "");
        if (getenv("PAIRS")) { printf("    pairs:"); for (size_t k = 0; k < pairE.size(); k++) printf(" %d:%.0f", (int)k + 1, 10 * log10(pairE[k] / N + 1e-18)); printf("\n"); }
        if (getenv("WAV")) { std::string fn = std::string(argv[2]) + "/" + std::to_string(p) + ".raw"; FILE* f = fopen(fn.c_str(), "wb"); fwrite(L.data(), 4, N, f); fclose(f); }
    }
}
