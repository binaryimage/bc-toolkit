// fxrender.cpp - test rig for binaryimage's MPC effect plugins.
//   fxrender <plugin.so> <outdir|-> [seconds]        render every program over a drum/bass/chord loop
//   env WAV=1 writes WAVs, BLK=n block size, TORTURE=1 random automation + program hopping
// Prints per program: output vs input loudness (dB), peak, p99 CPU, NaN/blow-up flags.
#include "mpc_vst2_abi.h"
#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <chrono>
#include <string>
#include <algorithm>

static VstTimeInfo g_ti;
static VstIntPtr host(AEffect*, VstInt32 op, VstInt32, VstIntPtr, void*, float)
{
    if (op == 1) return 2400;
    if (op == 7) return (VstIntPtr)&g_ti;
    return 0;
}
static void wav(const std::string& path, const std::vector<float>& L, const std::vector<float>& R, int sr)
{
    FILE* f = fopen(path.c_str(), "wb"); if (!f) return;
    int n = (int)L.size(), data = n * 4;
    auto w32 = [&](int v) { fwrite(&v, 4, 1, f); }; auto w16 = [&](short v) { fwrite(&v, 2, 1, f); };
    fwrite("RIFF", 1, 4, f); w32(36 + data); fwrite("WAVEfmt ", 1, 8, f); w32(16); w16(1); w16(2); w32(sr); w32(sr * 4); w16(4); w16(16);
    fwrite("data", 1, 4, f); w32(data);
    for (int i = 0; i < n; i++) for (float x : {L[i], R[i]}) { float c = x > 1 ? 1 : (x < -1 ? -1 : x); w16((short)lrintf(c * 32767)); }
    fclose(f);
}
// 120 bpm loop: kick, snare, hats, 16th saw bass (E phrygian), a chord stab each bar. Silence at the end.
static void makeLoop(std::vector<float>& L, std::vector<float>& R, int sr, double secs)
{
    int N = (int)(secs * sr); L.assign(N, 0); R.assign(N, 0);
    const double beat = 0.5; unsigned s = 1234;
    auto rnd = [&]() { s = s * 1664525u + 1013904223u; return ((s >> 9) & 0xFFFF) / 32768.0f - 1.0f; };
    const int bass[16] = {40, 40, 52, 41, 40, 47, 46, 40, 40, 43, 40, 41, 52, 40, 46, 41};
    const double end = secs - 1.8;
    float hp = 0, bl = 0;
    for (int i = 0; i < N; i++)
    {
        double t = (double)i / sr; if (t >= end) break;
        double b = t / beat; double bf = b - floor(b); double tb = bf * beat; int bi = (int)b;
        float kick = (float)(sin(2 * M_PI * (45 * tb + 75 * (1 - exp(-tb * 30)) / 30)) * exp(-tb * 9));
        float sn = (bi % 2 == 1) ? (float)((rnd() * 0.7 + 0.4 * sin(2 * M_PI * 190 * tb)) * exp(-tb * 18)) : 0.0f;
        double h = fmod(t, beat * 0.5); float n = rnd(); float hpo = n - hp; hp = n;
        float hat = (float)(hpo * exp(-h * 60) * 0.35);
        int st = (int)(t / (beat * 0.25)); double ts = fmod(t, beat * 0.25);
        double f = 440.0 * pow(2.0, (bass[st % 16] - 69) / 12.0); double ph = fmod(t * f, 1.0);
        float saw = (float)(2 * ph - 1) * (float)exp(-ts * 10); bl += 0.25f * (saw - bl);
        double bar = fmod(t, beat * 4); float chord = 0;
        if (bar < beat * 2) for (int nn : {64, 67, 70, 76}) { double fr = 440.0 * pow(2.0, (nn - 69) / 12.0); chord += (float)(2 * fmod(t * fr, 1.0) - 1); }
        chord *= 0.06f * (float)exp(-bar * 1.2);
        float m = 0.5f * kick + 0.35f * sn + hat + 0.45f * bl;
        L[i] = 0.7f * (m + chord * 1.2f); R[i] = 0.7f * (m * 0.95f + chord * 0.8f + hat * 0.2f);
    }
}
int main(int argc, char** argv)
{
    if (argc < 3) { printf("fxrender <plugin.so> <outdir|-> [seconds]\n"); return 1; }
    void* h = dlopen(argv[1], RTLD_NOW); if (!h) { printf("%s\n", dlerror()); return 1; }
    auto entry = (AEffect * (*)(audioMasterCallback))dlsym(h, "VSTPluginMain");
    AEffect* e = entry(host);
    auto D = [&](int op, int idx, VstIntPtr v, void* p, float o) { return e->dispatcher(e, op, idx, v, p, o); };
    const int sr = 44100; const int blk = getenv("BLK") ? atoi(getenv("BLK")) : 128;
    const double secs = argc > 3 ? atof(argv[3]) : 8.0;
    const bool torture = getenv("TORTURE") != nullptr, writeWav = getenv("WAV") != nullptr && strcmp(argv[2], "-");
    D(effOpen, 0, 0, 0, 0); D(effSetSampleRate, 0, 0, 0, sr); D(effSetBlockSize, 0, blk, 0, 0); D(effMainsChanged, 0, 1, 0, 0);
    std::vector<float> inL, inR; makeLoop(inL, inR, sr, secs);
    const int N = (int)inL.size();
    double inRms = 0; for (int i = 0; i < N; i++) inRms += 0.5 * (inL[i] * inL[i] + inR[i] * inR[i]); inRms = sqrt(inRms / N);
    int nprog = e->numPrograms; unsigned rs = 99; auto rnd = [&]() { rs = rs * 1664525u + 1013904223u; return ((rs >> 8) & 0xFFFFFF) / 16777216.0f; };
    int fails = 0;
    for (int p = getenv("PROG") ? atoi(getenv("PROG")) : 0; p < nprog; p++)
    {
        char name[64] = {0}; D(effSetProgram, 0, p, 0, 0); D(effGetProgramName, 0, 0, name, 0);
        D(effMainsChanged, 0, 0, 0, 0); D(effMainsChanged, 0, 1, 0, 0);
        std::vector<float> L(N), R(N), bL(blk), bR(blk), iL(blk), iR(blk);
        std::vector<double> bt; bool bad = false; float maxJump = 0, prevL = 0;
        memset(&g_ti, 0, sizeof g_ti); g_ti.sampleRate = sr; g_ti.tempo = 120; g_ti.flags = 0x2 | 0x200 | 0x400;
        for (int pos = 0; pos < N; pos += blk)
        {
            int n = std::min(blk, N - pos);
            g_ti.ppqPos = (double)pos / sr * 2.0; g_ti.samplePos = pos;
            if (const char* au = getenv("AUTO"))     // "idx@sec=norm,idx@sec=norm"
            {
                std::string a = au; size_t st = 0;
                while (st < a.size()) { size_t e2 = a.find(',', st); if (e2 == std::string::npos) e2 = a.size();
                    int k; float tt, v; if (sscanf(a.substr(st, e2 - st).c_str(), "%d@%f=%f", &k, &tt, &v) == 3) { int fr = (int)(tt * sr); if (fr >= pos && fr < pos + n) e->setParameter(e, k, v); }
                    st = e2 + 1; }
            }
            if (torture && rnd() < 0.08f) { int k = (int)(rnd() * e->numParams); e->setParameter(e, k, rnd()); }
            if (torture && rnd() < 0.003f) { D(effSetProgram, 0, (int)(rnd() * nprog), 0, 0); }
            if (torture && rnd() < 0.004f) { g_ti.flags ^= 0x2; }
            for (int i = 0; i < n; i++) { iL[i] = inL[pos + i]; iR[i] = inR[pos + i]; }
            float* ins[2] = {iL.data(), iR.data()}; float* outs[2] = {bL.data(), bR.data()};
            auto a = std::chrono::steady_clock::now();
            e->processReplacing(e, ins, outs, n);
            auto b = std::chrono::steady_clock::now();
            bt.push_back(std::chrono::duration<double, std::micro>(b - a).count());
            for (int i = 0; i < n; i++)
            {
                if (!std::isfinite(bL[i]) || !std::isfinite(bR[i]) || fabsf(bL[i]) > 8 || fabsf(bR[i]) > 8) bad = true;
                L[pos + i] = bL[i]; R[pos + i] = bR[i];
                maxJump = std::max(maxJump, fabsf(bL[i] - prevL)); prevL = bL[i];
            }
        }
        float pk = 0; double rms = 0; for (int i = 0; i < N; i++) { pk = std::max(pk, std::max(fabsf(L[i]), fabsf(R[i]))); rms += 0.5 * (L[i] * L[i] + R[i] * R[i]); }
        rms = sqrt(rms / N);
        std::sort(bt.begin(), bt.end()); double p99 = bt[(size_t)(bt.size() * 0.99)];
        double blockUs = 1e6 * blk / sr;
        // tail energy in the last second (the loop is silent there): shows reverb/delay tails
        double tail = 0; for (int i = N - sr; i < N; i++) tail += L[i] * L[i]; tail = sqrt(tail / sr);
        printf("%3d %-22s gain %+6.1f dB  peak %6.1f dB  tail %6.1f dB  p99 %5.1f%%  jump %.2f %s\n", p, name,
               20 * log10((rms + 1e-12) / inRms), 20 * log10(pk + 1e-9), 20 * log10(tail + 1e-9), 100 * p99 / blockUs, maxJump, bad ? "BAD!" : "");
        if (bad) fails++;
        if (writeWav) { std::string fn = std::string(argv[2]) + "/" + std::to_string(p) + "_" + name + ".wav"; for (auto& c : fn) if (c == ' ') c = '_'; wav(fn, L, R, sr); }
        if (torture || getenv("ONE")) break;
    }
    if (getenv("WAV") && !strcmp(argv[2], "-")) {}
    D(effMainsChanged, 0, 0, 0, 0); D(effClose, 0, 0, 0, 0);
    return fails ? 2 : 0;
}
