// dahaka_core.h - Dahaka's synth engine for MPC standalone, split into three instruments.
//
// SPDX-License-Identifier: MIT
//
// This is the desktop Dahaka's own voice code (Voice.cpp, DspHelpers.h, WavetableBank.cpp,
// Engine.cpp) with JUCE removed, so it builds with Akai's MPC Plugin SDK. Each MPC plugin
// defines DAHAKA_ENGINE before including this file and gets only that engine:
//
//   DAHAKA_ENGINE 0  Dahaka FM    4-operator FM/PM, 8 algorithms, feedback, 6 op waves
//   DAHAKA_ENGINE 1  Dahaka Wave  2 wavetable oscillators (8 tables x 32 frames) with mutators
//   DAHAKA_ENGINE 2  Dahaka VA    2 virtual-analog oscillators, 7-voice unison, sync, sub, drift
//
// What all three share (straight from Dahaka): noise, the drive + state-variable filter
// (LP12, LP24, Band, HP12, Notch), filter/amp/mod envelopes, two LFOs (6 shapes), a 6-slot mod
// matrix, 12-voice poly / mono / legato with glide (default off).
//
// Left out on purpose: Dahaka's arpeggiator and FX rack (use Chimera after it), and the
// serial cross-engine routing (each plugin has one engine). The desktop renders at 2x
// oversampling; here it runs at the host rate to save CPU on the MPC.

#ifndef DAHAKA_CORE_H
#define DAHAKA_CORE_H

#ifndef DAHAKA_ENGINE
#error "define DAHAKA_ENGINE (0 FM, 1 Wave, 2 VA) before including dahaka_core.h"
#endif

#include "mpc_plugin.h"
#include "dsp/math.h"
#include "dsp/midi_cc.h"

#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace dk {

constexpr bool kFM = DAHAKA_ENGINE == 0;
constexpr bool kWT = DAHAKA_ENGINE == 1;
constexpr bool kVA = DAHAKA_ENGINE == 2;

constexpr int numOps = 4, numWt = 2, numVa = 2, maxUnison = 7, numLfos = 2, numModSlots = 6;
constexpr int maxVoices = 12;
constexpr double kTwoPi = 6.283185307179586;

template <typename T> inline T jmin(T a, T b) { return a < b ? a : b; }
template <typename T> inline T jmax(T a, T b) { return a > b ? a : b; }
template <typename T> inline T jlimit(T lo, T hi, T v) { return v < lo ? lo : (v > hi ? hi : v); }
inline float clamp01(float v) { return jlimit(0.0f, 1.0f, v); }
inline float wrap01(float p) { return p - std::floor(p); }
using mpc::dsp::fastTanh;

// ---------------------------------------------------------------------------
// DspHelpers.h (desktop), JUCE removed
// ---------------------------------------------------------------------------
struct SineTable
{
    static constexpr int size = 4096;
    std::array<float, size + 1> t {};
    SineTable() { for (int i = 0; i <= size; ++i) t[(size_t)i] = (float)std::sin(kTwoPi * i / size); }
    static const SineTable& get() { static const SineTable s; return s; }
    float operator()(float phase) const noexcept
    {
        const float p = (phase - std::floor(phase)) * size;
        const int i = jmin((int)p, size - 1);
        const float fr = p - (float)i;
        return t[(size_t)i] + fr * (t[(size_t)i + 1] - t[(size_t)i]);
    }
};

// Exponential ADSR (linear attack). Times in seconds.
class Envelope
{
public:
    enum class Stage { Idle, Attack, Decay, Sustain, Release };
    void setSampleRate(double sr) noexcept { fs = (float)sr; }
    void setParams(float a, float d, float s, float r) noexcept
    {
        attackInc = 1.0f / jmax(1.0f, a * fs);
        decayCoef = coef(d);
        releaseCoef = coef(r);
        sustain = s;
    }
    void noteOn() noexcept { stage = Stage::Attack; }
    void noteOff() noexcept { if (stage != Stage::Idle) stage = Stage::Release; }
    void reset() noexcept { stage = Stage::Idle; value = 0.0f; }
    bool isActive() const noexcept { return stage != Stage::Idle; }
    float current() const noexcept { return value; }
    float next() noexcept
    {
        switch (stage)
        {
            case Stage::Idle: return 0.0f;
            case Stage::Attack:
                value += attackInc;
                if (value >= 1.0f) { value = 1.0f; stage = Stage::Decay; }
                break;
            case Stage::Decay:
                value += (sustain - value) * decayCoef;
                if (std::fabs(value - sustain) < 1.0e-4f) stage = Stage::Sustain;
                break;
            case Stage::Sustain:
                value += (sustain - value) * 0.002f;
                break;
            case Stage::Release:
                value -= value * releaseCoef;
                if (value < 1.0e-4f) { value = 0.0f; stage = Stage::Idle; }
                break;
        }
        return value;
    }
private:
    // The desktop computes these with exp() every block; cache by time so the MPC skips it.
    float coef(float t) const noexcept { return 1.0f - std::exp(-6.9f / jmax(1.0f, t * fs)); }
    Stage stage = Stage::Idle;
    float fs = 44100.0f, value = 0.0f, attackInc = 0.01f, decayCoef = 0.01f, releaseCoef = 0.01f, sustain = 1.0f;
};

struct XorShift
{
    uint32_t s = 0x9E3779B9u;
    void seed(uint32_t v) noexcept { s = v ? v : 1u; }
    uint32_t nextU() noexcept { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float bipolar() noexcept { return (float)(nextU() >> 8) * (2.0f / 16777216.0f) - 1.0f; }
};

class Lfo
{
public:
    void reset(float startPhase) noexcept { phase = startPhase; }
    void seed(uint32_t v) noexcept { rng.seed(v); held = rng.bipolar(); prev = rng.bipolar(); }
    float advance(int shape, float rateHz, int numSamples, float fs) noexcept
    {
        float v = 0.0f;
        switch (shape)
        {
            case 0: v = SineTable::get()(phase); break;
            case 1: v = phase < 0.5f ? 4.0f * phase - 1.0f : 3.0f - 4.0f * phase; break;
            case 2: v = 1.0f - 2.0f * phase; break;
            case 3: v = phase < 0.5f ? 1.0f : -1.0f; break;
            case 4: v = held; break;
            default: { const float t = phase * phase * (3.0f - 2.0f * phase); v = prev + t * (held - prev); break; }
        }
        phase += rateHz * (float)numSamples / fs;
        if (phase >= 1.0f) { phase -= std::floor(phase); prev = held; held = rng.bipolar(); }
        return v;
    }
private:
    float phase = 0.0f, held = 0.0f, prev = 0.0f;
    XorShift rng;
};

struct Svf
{
    float ic1 = 0.0f, ic2 = 0.0f, a1 = 1.0f, a2 = 0.0f, a3 = 0.0f, k = 2.0f;
    void reset() noexcept { ic1 = ic2 = 0.0f; }
    void set(float cutoff, float fs, float q) noexcept
    {
        const float g = std::tan(3.14159265f * cutoff / fs);
        k = q;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    struct Out { float lp, bp, hp; };
    Out process(float v0) noexcept
    {
        const float v3 = v0 - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        return {v2, v1, v0 - k * v1 - v2};
    }
};

struct DcBlocker
{
    float x1 = 0.0f, y1 = 0.0f, r = 0.9995f;
    void setSampleRate(double fs) noexcept { r = 1.0f - (float)(kTwoPi * 8.0 / fs); }
    void reset() noexcept { x1 = y1 = 0.0f; }
    float process(float x) noexcept { const float y = x - x1 + r * y1; x1 = x; y1 = y; return y; }
};

// ---------------------------------------------------------------------------
// Params.h enums (desktop). Arp Motion source and the serial-routing destinations are unused.
// ---------------------------------------------------------------------------
enum VoiceMode { Poly = 0, Mono, Legato };
enum ModSource { srcOff, srcLfo1, srcLfo2, srcModEnv, srcFilterEnv, srcVelocity,
                 srcModWheel, srcAftertouch, srcKeyTrack, srcRandom, numModSources };
enum ModDest { dstOff, dstPitch, dstFmDepth, dstFmFeedback, dstFmLevel, dstWt1Pos, dstWt2Pos,
               dstWt1Mut, dstWt2Mut, dstWt1Level, dstWt2Level, dstCutoff, dstResonance,
               dstAmp, dstNoise, dstVaPw, dstVaDetune, dstVa1Level, dstVa2Level, dstVa2Pitch, numModDests };
enum VaWave { vaSaw, vaPulse, vaTriangle, vaSine };
enum Mutator { mutOff, mutFmLin, mutSync, mutWarp, mutStack };
enum FilterMode { fltLp12, fltLp24, fltBp, fltHp, fltNotch };

struct FmAlgorithm { bool mod[numOps][numOps]; bool carrier[numOps]; };
inline const FmAlgorithm& getAlgorithm(int index)
{
    // Operators: 0 = A, 1 = B, 2 = C, 3 = D.  mod[target][source]
    static const FmAlgorithm algos[8] = {
        {{{0,1,0,0},{0,0,1,0},{0,0,0,1},{0,0,0,0}}, {1,0,0,0}},   // 1 Stack: D>C>B>A
        {{{0,1,0,0},{0,0,1,1},{0,0,0,0},{0,0,0,0}}, {1,0,0,0}},   // 2 Branch: (C+D)>B>A
        {{{0,1,1,0},{0,0,0,0},{0,0,0,1},{0,0,0,0}}, {1,0,0,0}},   // 3 Twin Mod: B>A, D>C>A
        {{{0,1,0,0},{0,0,0,0},{0,0,0,1},{0,0,0,0}}, {1,0,1,0}},   // 4 Two Pairs: B>A, D>C
        {{{0,0,0,1},{0,0,0,1},{0,0,0,1},{0,0,0,0}}, {1,1,1,0}},   // 5 Fan Out: D>A, D>B, D>C
        {{{0,0,0,0},{0,0,1,0},{0,0,0,1},{0,0,0,0}}, {1,1,0,0}},   // 6 Stack + Sine: D>C>B, A alone
        {{{0,0,1,0},{0,0,1,0},{0,0,0,1},{0,0,0,0}}, {1,1,0,0}},   // 7 Split: D>C, C>A, C>B
        {{{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0}}, {1,1,1,1}},   // 8 Additive
    };
    return algos[jlimit(0, 7, index)];
}

// ---------------------------------------------------------------------------
// WavetableBank (desktop WavetableBank.cpp) - same 8 tables x 32 frames x 11 band-limited
// levels. The desktop used JUCE's FFT; this uses a small radix-2 FFT. Built once, shared by
// every instance, when the first Dahaka Wave loads.
// ---------------------------------------------------------------------------
#if DAHAKA_ENGINE == 1
class WavetableBank
{
public:
    static constexpr int tableSize = 2048, stride = tableSize + 1, numFrames = 32, numTables = 8, numLevels = 11;
    static const WavetableBank& get() { static const WavetableBank b; return b; }

    const float* frame(int table, int frameIndex, int level) const
    {
        return data.data() + (((size_t)table * numFrames + (size_t)frameIndex) * numLevels + (size_t)level) * stride;
    }
    static int levelForIncrement(float inc) noexcept
    {
        // Highest harmonic that stays below ~0.45 fs: level L keeps 1024 >> L harmonics.
        const float maxH = 0.45f / jmax(inc, 1.0e-6f);
        int l = 0;
        while (l < numLevels - 1 && (float)(1024 >> l) > maxH) ++l;
        return l;
    }
    float lookup(int table, float position, float phase, int level) const noexcept
    {
        const float fp = clamp01(position) * (numFrames - 1);
        const int f0 = jmin((int)fp, numFrames - 2);
        const float ff = fp - (float)f0;
        const float p = wrap01(phase);
        const float ip = p * tableSize;
        const int i0 = jmin((int)ip, tableSize - 1);
        const float fr = ip - (float)i0;
        const float* a = frame(jlimit(0, numTables - 1, table), f0, level);
        const float* b = frame(jlimit(0, numTables - 1, table), f0 + 1, level);
        const float va = a[i0] + fr * (a[i0 + 1] - a[i0]);
        const float vb = b[i0] + fr * (b[i0 + 1] - b[i0]);
        return va + ff * (vb - va);
    }

private:
    std::vector<float> data;

    static double gauss(double x, double c, double w) { const double d = (x - c) / w; return std::exp(-0.5 * d * d); }
    template <typename F> static void additive(std::vector<double>& out, int maxHarm, F amp)
    {
        std::fill(out.begin(), out.end(), 0.0);
        const int n = (int)out.size();
        for (int k = 1; k <= maxHarm; ++k)
        {
            const double a = amp(k);
            if (std::fabs(a) < 1.0e-6) continue;
            for (int i = 0; i < n; ++i) out[(size_t)i] += a * std::sin(kTwoPi * k * i / n);
        }
    }
    static double classicShape(int s, double p)
    {
        switch (s)
        {
            case 0: return std::sin(kTwoPi * p);
            case 1: return p < 0.5 ? 4.0 * p - 1.0 : 3.0 - 4.0 * p;
            case 2: return 1.0 - 2.0 * p;
            case 3: return p < 0.5 ? 1.0 : -1.0;
            default: return p < 0.2 ? 1.0 : -1.0;
        }
    }
    static void generateFrame(int table, double x, std::vector<double>& out)
    {
        const int n = (int)out.size();
        switch (table)
        {
            case 0: { const double seg = x * 4.0; const int s0 = jmin((int)seg, 3); const double f = seg - s0;
                      for (int i = 0; i < n; ++i) { const double p = (double)i / n; out[(size_t)i] = (1.0 - f) * classicShape(s0, p) + f * classicShape(s0 + 1, p); }
                      break; }
            case 1: { const double count = 1.0 + x * x * 63.0;
                      additive(out, 64, [count](int k) { return jlimit(0.0, 1.0, count - k + 1.0) / k; }); break; }
            case 2: {
                static const double formants[5][3] = {{800, 1150, 2900}, {400, 1600, 2700}, {350, 2300, 3000}, {450, 800, 2830}, {325, 700, 2530}};
                const double seg = x * 4.0; const int v0 = jmin((int)seg, 3); const double f = seg - v0;
                double F[3]; for (int j = 0; j < 3; ++j) F[j] = (1.0 - f) * formants[v0][j] + f * formants[v0 + 1][j];
                additive(out, 64, [&](int k) { const double hz = k * 110.0;
                    return 0.08 / k + gauss(hz, F[0], 110.0) + 0.6 * gauss(hz, F[1], 140.0) + 0.3 * gauss(hz, F[2], 180.0); });
                break; }
            case 3: { const double w = 0.5 - x * 0.47; for (int i = 0; i < n; ++i) out[(size_t)i] = ((double)i / n) < w ? 1.0 : -1.0; break; }
            case 4: { const double r = 1.0 + x * 7.0; for (int i = 0; i < n; ++i) { const double s = std::fmod((double)i / n * r, 1.0); out[(size_t)i] = 1.0 - 2.0 * s; } break; }
            case 5: { const double g = 1.0 + x * 6.0; for (int i = 0; i < n; ++i) out[(size_t)i] = std::sin(g * 1.5707963267948966 * std::sin(kTwoPi * i / n)); break; }
            case 6: additive(out, 48, [x](int k) { if (k == 1) return 1.0; return ((k % 2) ? (1.0 - x) : x) / k; }); break;
            default: {
                static const int primes[] = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43};
                for (int i = 0; i < n; ++i) out[(size_t)i] = std::sin(kTwoPi * i / n);
                int j = 0;
                for (int k : primes) { const double a = std::pow(x, 0.7) * std::pow(0.82, j++);
                    for (int i = 0; i < n; ++i) out[(size_t)i] += a * std::sin(kTwoPi * k * i / n + j * 0.7); }
                break; }
        }
    }
    static void fft(std::vector<std::complex<double>>& a, bool inverse)
    {
        const size_t n = a.size();
        for (size_t i = 1, j = 0; i < n; ++i)
        {
            size_t bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap(a[i], a[j]);
        }
        for (size_t len = 2; len <= n; len <<= 1)
        {
            const double ang = kTwoPi / (double)len * (inverse ? 1.0 : -1.0);
            const std::complex<double> wl(std::cos(ang), std::sin(ang));
            for (size_t i = 0; i < n; i += len)
            {
                std::complex<double> w(1.0, 0.0);
                for (size_t k = 0; k < len / 2; ++k)
                {
                    const auto u = a[i + k], v = a[i + k + len / 2] * w;
                    a[i + k] = u + v; a[i + k + len / 2] = u - v;
                    w *= wl;
                }
            }
        }
        if (inverse) for (auto& x : a) x /= (double)n;
    }

    WavetableBank()
    {
        data.assign((size_t)numTables * numFrames * numLevels * stride, 0.0f);
        std::vector<double> cycle(tableSize);
        std::vector<std::complex<double>> spec(tableSize), work(tableSize);
        std::vector<float> levels((size_t)numLevels * tableSize);
        for (int t = 0; t < numTables; ++t)
            for (int f = 0; f < numFrames; ++f)
            {
                generateFrame(t, (double)f / (numFrames - 1), cycle);
                for (int i = 0; i < tableSize; ++i) spec[(size_t)i] = {cycle[(size_t)i], 0.0};
                fft(spec, false);
                float peak = 0.0f;
                for (int l = 0; l < numLevels; ++l)
                {
                    const int maxHarm = 1024 >> l;
                    work = spec;
                    for (int k = 0; k < tableSize; ++k)
                    {
                        const int h = k <= tableSize / 2 ? k : tableSize - k;
                        if (h == 0 || h > maxHarm || h == tableSize / 2) work[(size_t)k] = 0.0;
                    }
                    fft(work, true);
                    float* dst = levels.data() + (size_t)l * tableSize;
                    for (int i = 0; i < tableSize; ++i) dst[i] = (float)work[(size_t)i].real();
                    if (l == 0) for (int i = 0; i < tableSize; ++i) peak = jmax(peak, std::fabs(dst[i]));
                }
                const float norm = peak > 1.0e-9f ? 0.95f / peak : 0.0f;
                for (int l = 0; l < numLevels; ++l)
                {
                    float* dst = const_cast<float*>(frame(t, f, l));
                    const float* src = levels.data() + (size_t)l * tableSize;
                    for (int i = 0; i < tableSize; ++i) dst[i] = src[i] * norm;
                    dst[tableSize] = dst[0];
                }
            }
    }
};
#endif

// ---------------------------------------------------------------------------
// SynthParams (desktop SynthParams.h) - plain values snapshotted once per block
// ---------------------------------------------------------------------------
struct Adsr { float a = 0.01f, d = 0.3f, s = 0.7f, r = 0.3f; };
struct OpParams { int wave = 0; float ratio = 1.0f, level = 0.5f, vel = 0.5f; Adsr env; };
struct WtParams { int table = 0, mutMode = 0; float pos = 0.5f, pitchRatio = 1.0f, level = 0.0f, mutAmt = 0.0f, mutRatio = 1.0f; };
struct VaParams { int wave = 0, unison = 1; float pitchRatio = 1.0f, pw = 0.5f, detune = 0.25f, level = 0.0f; };
struct ModSlot { int src = 0, dst = 0; float amt = 0.0f; };
struct SynthParams
{
    int voiceMode = Poly;
    float glide = 0.0f, bendRange = 2.0f;
    int algo = 0;
    float fmDepth = 0.3f, fmFeedback = 0.0f, fmLevel = 1.0f;
    OpParams op[numOps];
    WtParams wt[numWt];
    VaParams va[numVa];
    bool vaSync = false;
    float vaSub = 0.0f, vaDrift = 0.15f, noise = 0.0f;
    int fltMode = 1;
    float cutoff = 8000.0f, reso = 0.15f, drive = 0.0f, fltEnv = 0.0f, fltKey = 0.5f, fltVel = 0.0f;
    Adsr fEnv, aEnv, mEnv;
    float ampVel = 0.5f;
    int lfoShape[numLfos] {};
    float lfoRate[numLfos] {5.0f, 0.3f};
    bool lfoRetrig[numLfos] {true, false};
    ModSlot mod[numModSlots];
};
struct GlobalMod { float bend = 0.0f, wheel = 0.0f, aftertouch = 0.0f; };

// ---------------------------------------------------------------------------
// Voice (desktop Voice.cpp), one engine compiled in
// ---------------------------------------------------------------------------
inline float opWave(int w, float ph, const SineTable& s) noexcept
{
    const float x = s(ph);
    switch (w)
    {
        case 1: return x > 0.0f ? x : 0.0f;
        case 2: return 2.0f * std::fabs(x) - 1.0f;
        case 3: return fastTanh(4.0f * x);
        case 4: return 0.55f * (x + 0.5f * s(2.0f * ph) + 0.333f * s(3.0f * ph));
        case 5: return wrap01(2.0f * ph) < 0.5f ? std::fabs(x) : 0.0f;
        default: return x;
    }
}
inline float polyBlep(float t, float dt) noexcept
{
    if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}
inline float vaSample(int wave, float t, float dt, float pw, const SineTable& s) noexcept
{
    switch (wave)
    {
        case vaPulse: { float v = t < pw ? 1.0f : -1.0f; v += polyBlep(t, dt); v -= polyBlep(wrap01(t + 1.0f - pw), dt); return v * 0.8f; }
        case vaTriangle: return 4.0f * std::fabs(t - 0.5f) - 1.0f;
        case vaSine: return s(t);
        default: return (2.0f * t - 1.0f - polyBlep(t, dt)) * 0.8f;
    }
}

class Voice
{
public:
    uint64_t age = 0;

    void prepare(double sampleRate, uint32_t seed)
    {
        fs = (float)sampleRate;
        for (auto& e : opEnv) e.setSampleRate(sampleRate);
        ampEnv.setSampleRate(sampleRate); fltEnv.setSampleRate(sampleRate); modEnv.setSampleRate(sampleRate);
        dc.setSampleRate(sampleRate);
        rng.seed(seed);
        for (int i = 0; i < numLfos; ++i) { lfo[i].seed(rng.nextU()); lfo[i].reset(0.5f + 0.5f * rng.bipolar()); }
        for (auto& v : vaState) for (auto& ph : v.phase) ph = 0.5f + 0.5f * rng.bipolar();
        kill();
    }
    void start(int newNote, float vel, bool retrigger, float glideFrom)
    {
        note = newNote;
        targetNote = (float)newNote;
        currentNote = glideFrom >= 0.0f ? glideFrom : targetNote;
        released = false;
        if (!retrigger) return;
        if (!ampEnv.isActive()) { svf1.reset(); svf2.reset(); dc.reset(); fbHistory[0] = fbHistory[1] = 0.0f; }
        velocity = vel;
        randomValue = rng.bipolar();
        lfoNeedsReset = true;
        for (auto& e : opEnv) e.noteOn();
        ampEnv.noteOn(); fltEnv.noteOn(); modEnv.noteOn();
    }
    void release() noexcept
    {
        released = true;
        for (auto& e : opEnv) e.noteOff();
        ampEnv.noteOff(); fltEnv.noteOff(); modEnv.noteOff();
    }
    void kill() noexcept
    {
        released = true;
        for (auto& e : opEnv) e.reset();
        ampEnv.reset(); fltEnv.reset(); modEnv.reset();
        svf1.reset(); svf2.reset(); dc.reset();
    }
    bool isActive() const noexcept { return ampEnv.isActive(); }
    bool isReleased() const noexcept { return released; }
    int getNote() const noexcept { return note; }
    float getCurrentPitch() const noexcept { return currentNote; }
    float getLevel() const noexcept { return ampEnv.current(); }

    void render(float* out, int numSamples, const SynthParams& p, const GlobalMod& g) noexcept
    {
        if (!ampEnv.isActive()) return;
        const auto& sine = SineTable::get();
#if DAHAKA_ENGINE == 1
        const auto& bank = WavetableBank::get();
#endif
        const auto& algo = getAlgorithm(p.algo);
        int numCarriers = 0;
        for (bool c : algo.carrier) numCarriers += c ? 1 : 0;
        const float carrierNorm = 1.0f / std::sqrt((float)jmax(1, numCarriers));

        if (kFM) for (int i = 0; i < numOps; ++i) opEnv[i].setParams(p.op[i].env.a, p.op[i].env.d, p.op[i].env.s, p.op[i].env.r);
        ampEnv.setParams(p.aEnv.a, p.aEnv.d, p.aEnv.s, p.aEnv.r);
        fltEnv.setParams(p.fEnv.a, p.fEnv.d, p.fEnv.s, p.fEnv.r);
        modEnv.setParams(p.mEnv.a, p.mEnv.d, p.mEnv.s, p.mEnv.r);
        if (lfoNeedsReset)
        {
            for (int i = 0; i < numLfos; ++i) if (p.lfoRetrig[i]) lfo[i].reset(0.0f);
            lfoNeedsReset = false;
        }
        const float velAmp = 1.0f - p.ampVel + p.ampVel * velocity;
        float opGainBase[numOps];
        for (int i = 0; i < numOps; ++i) opGainBase[i] = p.op[i].level * (1.0f - p.op[i].vel + p.op[i].vel * velocity);

        for (int start = 0; start < numSamples; start += controlInterval)
        {
            const int n = jmin(controlInterval, numSamples - start);

            // ---- control rate -------------------------------------------------
            if (p.glide > 0.0005f) currentNote += (targetNote - currentNote) * (1.0f - std::exp(-(float)n / (p.glide * 0.33f * fs)));
            else currentNote = targetNote;

            float src[numModSources] {};
            src[srcLfo1] = lfo[0].advance(p.lfoShape[0], p.lfoRate[0], n, fs);
            src[srcLfo2] = lfo[1].advance(p.lfoShape[1], p.lfoRate[1], n, fs);
            src[srcModEnv] = modEnv.current();
            src[srcFilterEnv] = fltEnv.current();
            src[srcVelocity] = velocity;
            src[srcModWheel] = g.wheel;
            src[srcAftertouch] = g.aftertouch;
            src[srcKeyTrack] = (currentNote - 60.0f) / 60.0f;
            src[srcRandom] = randomValue;
            float dst[numModDests] {};
            for (const auto& slot : p.mod)
                if (slot.src > 0 && slot.dst > 0 && slot.src < numModSources && slot.dst < numModDests)
                    dst[slot.dst] += src[slot.src] * slot.amt;

            const float pitch = currentNote + g.bend * p.bendRange + dst[dstPitch] * 12.0f;
            const float baseInc = 440.0f * std::exp2((pitch - 69.0f) / 12.0f) / fs;

            // FM
            float opInc[numOps], depth = 0.0f, fbAmt = 0.0f, fmLevel = 0.0f;
            if (kFM)
            {
                for (int i = 0; i < numOps; ++i) opInc[i] = jmin(baseInc * p.op[i].ratio, 0.45f);
                depth = clamp01(p.fmDepth + dst[dstFmDepth]) * 2.0f;
                fbAmt = clamp01(p.fmFeedback + dst[dstFmFeedback]) * 0.35f;
                fmLevel = jlimit(0.0f, 2.0f, p.fmLevel * (1.0f + dst[dstFmLevel]));
            }

            // Wavetable
            struct WtCtl { float inc, pos, amt, level, stackInc[4]; int lvl; } wc[numWt] {};
#if DAHAKA_ENGINE == 1
            for (int w = 0; w < numWt; ++w)
            {
                const auto& wp = p.wt[w];
                auto& c = wc[w];
                c.inc = jmin(baseInc * wp.pitchRatio, 0.45f);
                c.pos = clamp01(wp.pos + dst[dstWt1Pos + w]);
                c.amt = clamp01(wp.mutAmt + dst[dstWt1Mut + w]);
                c.level = jlimit(0.0f, 2.0f, wp.level * (1.0f + dst[dstWt1Level + w]));
                float effInc = c.inc;
                switch (wp.mutMode)
                {
                    case mutFmLin: effInc *= 1.0f + c.amt * 4.0f; break;
                    case mutSync: effInc *= 1.0f + c.amt * 7.0f; break;
                    case mutWarp: effInc *= 0.5f / jmax(0.02f, 0.5f - c.amt * 0.48f); break;
                    default: break;
                }
                c.lvl = WavetableBank::levelForIncrement(effInc);
                for (int k = 0; k < 4; ++k) c.stackInc[k] = c.inc * std::exp2(((float)k - 1.5f) * c.amt * 0.25f / 12.0f);
            }
#endif

            // Virtual analog
            struct VaCtl { float inc[maxUnison]; int n; float pw, level, gain; } vc[numVa] {};
            float vaMasterInc = 0.0f;
            if (kVA)
            {
                if (--driftCounter <= 0) { driftTarget = rng.bipolar(); driftCounter = jmax(1, (int)(fs * 0.35f / (float)controlInterval)); }
                drift += (driftTarget - drift) * 0.015f;
                const float driftCents = drift * p.vaDrift * 14.0f;
                for (int o = 0; o < numVa; ++o)
                {
                    const auto& vp = p.va[o];
                    auto& c = vc[o];
                    c.n = jlimit(1, maxUnison, vp.unison);
                    c.level = jlimit(0.0f, 2.0f, vp.level * (1.0f + dst[dstVa1Level + o]));
                    c.pw = jlimit(0.05f, 0.95f, vp.pw + dst[dstVaPw] * 0.45f);
                    c.gain = 1.0f / std::sqrt((float)c.n);
                    const float det = clamp01(vp.detune + dst[dstVaDetune]);
                    float ratio = vp.pitchRatio * std::exp2((o == 0 ? driftCents : -0.7f * driftCents) / 1200.0f);
                    if (o == 1) ratio *= std::exp2(dst[dstVa2Pitch] * 2.0f);
                    const float base = baseInc * ratio;
                    if (o == 0) vaMasterInc = jmin(base, 0.45f);
                    for (int k = 0; k < c.n; ++k)
                    {
                        const float d = c.n > 1 ? 2.0f * (float)k / (float)(c.n - 1) - 1.0f : 0.0f;
                        c.inc[k] = jmin(base * std::exp2(d * det * 0.6f / 12.0f), 0.45f);
                    }
                }
            }

            const float noiseLevel = clamp01(p.noise + dst[dstNoise]);
            const float octaves = p.fltEnv * 7.0f * fltEnv.current() + p.fltKey * (currentNote - 60.0f) / 12.0f
                                + p.fltVel * (velocity - 1.0f) * 4.0f + dst[dstCutoff] * 5.0f;
            const float cutoff = jlimit(20.0f, jmin(20000.0f, fs * 0.45f), p.cutoff * std::exp2(octaves));
            const float reso = clamp01(p.reso + dst[dstResonance]);
            const float kReso = 2.0f - 1.96f * reso;
            if (p.fltMode == fltLp24) { svf1.set(cutoff, fs, 1.414f); svf2.set(cutoff, fs, kReso); }
            else svf1.set(cutoff, fs, kReso);
            const float ampMod = jlimit(0.0f, 2.0f, 1.0f + dst[dstAmp]);
            const float driveAmt = p.drive, driveGain = 1.0f + driveAmt * 8.0f;

            // ---- audio rate ---------------------------------------------------
            for (int i = 0; i < n; ++i)
            {
                float x = 0.0f;
                if (kFM)
                {
                    float opOut[numOps] {};
                    for (int o = numOps - 1; o >= 0; --o)
                    {
                        float pm = 0.0f;
                        for (int m = o + 1; m < numOps; ++m) if (algo.mod[o][m]) pm += opOut[m];
                        const float env = opEnv[o].next();
                        float phaseOffset = pm * depth;
                        if (o == numOps - 1) phaseOffset += fbAmt * (fbHistory[0] + fbHistory[1]);
                        const float v = opWave(p.op[o].wave, opPhase[o] + phaseOffset, sine);
                        opPhase[o] = wrap01(opPhase[o] + opInc[o]);
                        opOut[o] = v * opGainBase[o] * env;
                        if (o == numOps - 1) { fbHistory[1] = fbHistory[0]; fbHistory[0] = v * env; }
                    }
                    float fmOut = 0.0f;
                    for (int o = 0; o < numOps; ++o) if (algo.carrier[o]) fmOut += opOut[o];
                    x += fmOut * carrierNorm * fmLevel;
                }
#if DAHAKA_ENGINE == 1
                for (int w = 0; w < numWt; ++w)
                {
                    const auto& wp = p.wt[w];
                    const auto& c = wc[w];
                    auto& st = wtState[w];
                    if (c.level <= 0.0f) { st.phase = wrap01(st.phase + c.inc); continue; }
                    float v = 0.0f;
                    switch (wp.mutMode)
                    {
                        case mutFmLin:
                        {
                            const float m = sine(st.modPhase);
                            st.modPhase = wrap01(st.modPhase + c.inc * wp.mutRatio);
                            v = bank.lookup(wp.table, c.pos, st.phase, c.lvl);
                            st.phase = wrap01(st.phase + c.inc * (1.0f + c.amt * 4.0f * m));
                            break;
                        }
                        case mutSync:
                        {
                            const float slave = 1.0f + c.amt * 7.0f;
                            st.master += c.inc;
                            if (st.master >= 1.0f) { st.master -= 1.0f; st.phase = wrap01(st.master * slave); }
                            else st.phase = wrap01(st.phase + c.inc * slave);
                            v = bank.lookup(wp.table, c.pos, st.phase, c.lvl);
                            break;
                        }
                        case mutWarp:
                        {
                            const float bp = 0.5f - c.amt * 0.48f, ph = st.phase;
                            const float q = ph < bp ? ph * 0.5f / bp : 0.5f + (ph - bp) * 0.5f / (1.0f - bp);
                            v = bank.lookup(wp.table, c.pos, q, c.lvl);
                            st.phase = wrap01(st.phase + c.inc);
                            break;
                        }
                        case mutStack:
                        {
                            for (int k = 0; k < 4; ++k) { v += bank.lookup(wp.table, c.pos, st.stack[k], c.lvl); st.stack[k] = wrap01(st.stack[k] + c.stackInc[k]); }
                            v *= 0.5f;
                            break;
                        }
                        default:
                            v = bank.lookup(wp.table, c.pos, st.phase, c.lvl);
                            st.phase = wrap01(st.phase + c.inc);
                            break;
                    }
                    x += v * c.level;
                }
#endif
                if (kVA)
                {
                    bool syncReset = false;
                    float syncFrac = 0.0f;
                    vaMaster += vaMasterInc;
                    if (vaMaster >= 1.0f) { vaMaster -= 1.0f; syncReset = true; syncFrac = vaMaster / jmax(1.0e-6f, vaMasterInc); }
                    if (p.vaSub > 0.0f)
                    {
                        const float dt = vaMasterInc * 0.5f;
                        subPhase = wrap01(subPhase + dt);
                        float s = subPhase < 0.5f ? 1.0f : -1.0f;
                        s += polyBlep(subPhase, dt);
                        s -= polyBlep(wrap01(subPhase + 0.5f), dt);
                        x += s * 0.7f * p.vaSub;
                    }
                    for (int o = 0; o < numVa; ++o)
                    {
                        const auto& c = vc[o];
                        auto& st = vaState[o];
                        const bool reset = o == 1 && p.vaSync && syncReset;
                        if (c.level <= 0.0f) { if (reset) for (int k = 0; k < c.n; ++k) st.phase[k] = syncFrac * c.inc[k]; continue; }
                        float sum = 0.0f;
                        for (int k = 0; k < c.n; ++k)
                        {
                            float& ph = st.phase[k];
                            sum += vaSample(p.va[o].wave, ph, c.inc[k], c.pw, sine);
                            ph += c.inc[k];
                            if (ph >= 1.0f) ph -= 1.0f;
                            if (reset) ph = syncFrac * c.inc[k];
                        }
                        x += sum * c.gain * c.level;
                    }
                }
                if (noiseLevel > 0.0f) x += rng.bipolar() * noiseLevel;
                x *= 0.5f;
                if (driveAmt > 0.001f) x = fastTanh(x * driveGain);
                float y;
                switch (p.fltMode)
                {
                    case fltLp24: y = svf2.process(svf1.process(x).lp).lp; break;
                    case fltBp: y = svf1.process(x).bp * kReso; break;
                    case fltHp: y = svf1.process(x).hp; break;
                    case fltNotch: { const auto o = svf1.process(x); y = o.lp + o.hp; break; }
                    default: y = svf1.process(x).lp; break;
                }
                const float a = ampEnv.next();
                fltEnv.next();
                modEnv.next();
                out[start + i] += dc.process(y) * a * velAmp * ampMod * 0.5f;
            }
            if (!ampEnv.isActive()) { kill(); break; }
        }
    }

private:
    static constexpr int controlInterval = 32;
    float fs = 44100.0f;
    int note = 60;
    float targetNote = 60.0f, currentNote = 60.0f, velocity = 1.0f, randomValue = 0.0f;
    bool released = true, lfoNeedsReset = false;
    Envelope opEnv[numOps], ampEnv, fltEnv, modEnv;
    Lfo lfo[numLfos];
    float opPhase[numOps] {};
    float fbHistory[2] {};
    struct WtState { float phase = 0.0f, master = 0.0f, modPhase = 0.0f, stack[4] {}; } wtState[numWt];
    struct VaState { float phase[maxUnison] {}; } vaState[numVa];
    float vaMaster = 0.0f, subPhase = 0.0f;
    float drift = 0.0f, driftTarget = 0.0f;
    int driftCounter = 0;
    Svf svf1, svf2;
    DcBlocker dc;
    XorShift rng;
};

} // namespace dk

#endif // DAHAKA_CORE_H
