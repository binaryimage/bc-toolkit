// bi_fx.h - binaryimage's shared DSP building blocks for the MPC effect plugins
// (Grendel, Ouroboros, Fenrir, Moloch, Mare, Banshee) and the Legion choir.
//
// SPDX-License-Identifier: MIT
//
// Rules, same as the SDK: everything that needs memory gets it in prepare() (never on the audio
// thread); reset() only clears; process functions never allocate.
#pragma once
#include <cmath>
#include <cstdint>
#include <vector>
#include <algorithm>

namespace bi {

constexpr float kPi = 3.14159265358979f;
constexpr float kTwoPi = 6.28318530717959f;

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float undenorm(float v) { return std::fabs(v) < 1e-20f ? 0.0f : v; }
inline float dbToGain(float db) { return std::pow(10.0f, db * 0.05f); }
inline float semis(float st) { return std::pow(2.0f, st * (1.0f / 12.0f)); }

// Rational tanh: smooth, cheap, exact enough for saturation (|x| clipped at 4.97).
inline float fastTanh(float x)
{
    x = clampf(x, -4.97f, 4.97f);
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}
inline float softClip(float x) { return fastTanh(x); }

// xorshift32 random numbers.
struct Rng
{
    uint32_t s = 0x9E3779B9u;
    void seed(uint32_t v) { s = v ? v : 0x9E3779B9u; }
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uni() { return (float)(next() >> 8) * (1.0f / 16777216.0f); }       // 0..1
    float bi() { return uni() * 2.0f - 1.0f; }                                   // -1..1
    bool chance(float p) { return uni() < p; }
};

// One-pole parameter smoother.
struct Smooth
{
    float v = 0.0f, target = 0.0f, c = 0.999f;
    void prepare(float ms, float sr) { c = std::exp(-1.0f / (std::max(ms, 0.1f) * 0.001f * sr)); }
    void snap(float x) { v = target = x; }
    float next() { v = target + (v - target) * c; return v; }
};

// One-pole filters.
struct OnePole
{
    float z = 0.0f, a = 0.5f;
    void set(float hz, float sr) { a = 1.0f - std::exp(-kTwoPi * clampf(hz, 5.0f, sr * 0.49f) / sr); }
    void reset() { z = 0.0f; }
    float lp(float x) { z += a * (x - z); z = undenorm(z); return z; }
    float hp(float x) { return x - lp(x); }
};

struct DcBlock
{
    float x1 = 0.0f, y1 = 0.0f;
    void reset() { x1 = y1 = 0.0f; }
    float process(float x) { const float y = x - x1 + 0.9975f * y1; x1 = x; y1 = undenorm(y); return y1; }
};

// TPT state variable filter (Zavalishin). set() costs a tan(): call per block or when changed.
struct Svf
{
    float g = 0.1f, k = 1.0f, a1 = 0, a2 = 0, a3 = 0, ic1 = 0, ic2 = 0;
    float lp = 0, bp = 0, hp = 0;
    void reset() { ic1 = ic2 = 0.0f; }
    void set(float hz, float q, float sr)
    {
        g = std::tan(kPi * clampf(hz, 10.0f, sr * 0.45f) / sr);
        k = 1.0f / std::max(q, 0.05f);
        a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    void process(float x)
    {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = undenorm(2.0f * v1 - ic1);
        ic2 = undenorm(2.0f * v2 - ic2);
        lp = v2; bp = v1; hp = x - k * v1 - v2;
    }
    float processLP(float x) { process(x); return lp; }
    float processBP(float x) { process(x); return bp; }
    float processHP(float x) { process(x); return hp; }
};

// Power-of-two ring buffer with absolute positions, so several heads can wander independently.
struct Ring
{
    std::vector<float> b;
    uint32_t mask = 0;
    int64_t w = 0;                       // samples written so far
    void prepare(int minSize)
    {
        uint32_t n = 1; while ((int)n < minSize + 4) n <<= 1;
        b.assign(n, 0.0f); mask = n - 1; w = 0;
    }
    void clear() { std::fill(b.begin(), b.end(), 0.0f); }
    int size() const { return (int)mask + 1; }
    void write(float x) { b[(uint32_t)w & mask] = x; ++w; }
    // Overwrite without advancing (for frozen buffers fed by feedback).
    float at(int64_t i) const { return b[(uint32_t)i & mask]; }
    // Read at an absolute fractional position (cubic Hermite).
    float readAbs(double pos) const
    {
        const double fl = std::floor(pos);
        const int64_t i = (int64_t)fl;
        const float f = (float)(pos - fl);
        const float y0 = at(i - 1), y1 = at(i), y2 = at(i + 1), y3 = at(i + 2);
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    }
    // Read `delay` samples behind the newest sample (delay >= 1).
    float readDelay(float delay) const { return readAbs((double)(w - 1) - (double)std::max(delay, 1.0f)); }
    float readDelayLin(float delay) const
    {
        const double pos = (double)(w - 1) - (double)std::max(delay, 1.0f);
        const double fl = std::floor(pos); const float f = (float)(pos - fl); const int64_t i = (int64_t)fl;
        return at(i) + f * (at(i + 1) - at(i));
    }
};

// Delay-line pitch shifter: two read heads half a window apart, sin^2 windows.
// ratio 2 = up an octave, 0.5 = down an octave. Jitter randomises where each head restarts.
struct PitchShifter
{
    Ring r;
    float phase = 0.0f, sr = 44100.0f;
    float off[2] = {0, 0};
    Rng rng;
    void prepare(float sampleRate, float maxWindowMs)
    {
        sr = sampleRate;
        r.prepare((int)(sr * maxWindowMs * 0.001f * 2.2f) + 64);
        phase = 0.0f; off[0] = off[1] = 0.0f;
    }
    void reset() { r.clear(); phase = 0.0f; }
    float process(float x, float ratio, float windowSamples, float jitter = 0.0f)
    {
        r.write(x);
        const float win = clampf(windowSamples, 64.0f, (float)(r.size() / 2 - 8));
        const float prev1 = phase;
        phase += (1.0f - ratio) / win;
        phase -= std::floor(phase);
        float ph2 = phase + 0.5f; if (ph2 >= 1.0f) ph2 -= 1.0f;
        if (std::fabs(phase - prev1) > 0.5f) off[0] = jitter * rng.uni() * win * 0.5f;   // head 1 wrapped (silent)
        if (std::fabs(ph2 - prev2) > 0.5f) off[1] = jitter * rng.uni() * win * 0.5f;     // head 2 wrapped (silent)
        prev2 = ph2;
        const float d1 = 2.0f + phase * win + off[0];
        const float d2 = 2.0f + ph2 * win + off[1];
        const float w1 = std::sin(kPi * phase), w2 = std::sin(kPi * ph2);
        return r.readDelayLin(d1) * w1 * w1 + r.readDelayLin(d2) * w2 * w2;
    }
    float prev2 = 0.5f;
};

// 8-line feedback delay network reverb with input diffusion and gentle modulation.
class Reverb
{
public:
    void prepare(float sampleRate)
    {
        sr_ = sampleRate;
        const float s = sr_ / 44100.0f;
        for (int i = 0; i < 8; ++i) line_[i].prepare((int)(kLen[i] * 2.1f * s) + 64);
        for (int i = 0; i < 4; ++i) ap_[i].prepare((int)(kAp[i] * 2.1f * s) + 8);
        pre_[0].prepare((int)(sr_ * 0.5f)); pre_[1].prepare((int)(sr_ * 0.5f));
        reset();
    }
    void reset()
    {
        for (auto& l : line_) l.clear();
        for (auto& a : ap_) a.clear();
        pre_[0].clear(); pre_[1].clear();
        for (auto& d : damp_) d.reset();
        lfo_ = 0.0f;
    }
    // size 0..1, decay seconds, damp 0..1 (high = darker), mod 0..1, preMs, freeze holds the tail.
    void set(float size, float decaySec, float damp, float mod, float preMs, bool freeze)
    {
        const float s = (0.35f + 1.6f * clampf(size, 0.0f, 1.0f)) * sr_ / 44100.0f;
        for (int i = 0; i < 8; ++i)
        {
            len_[i] = kLen[i] * s;
            const float g = std::pow(10.0f, -3.0f * len_[i] / (std::max(decaySec, 0.05f) * sr_));
            fb_[i] = freeze ? 1.0f : std::min(g, 0.999f);
            damp_[i].set(freeze ? 20000.0f : 20000.0f * std::pow(0.03f, clampf(damp, 0.0f, 1.0f)), sr_);
        }
        for (int i = 0; i < 4; ++i) apLen_[i] = kAp[i] * s;
        mod_ = clampf(mod, 0.0f, 1.0f) * 12.0f * sr_ / 44100.0f;
        pre_ms_ = clampf(preMs, 0.0f, 480.0f) * 0.001f * sr_;
        freeze_ = freeze;
    }
    void process(float inL, float inR, float& outL, float& outR)
    {
        pre_[0].write(inL); pre_[1].write(inR);
        float xl = pre_ms_ > 1.0f ? pre_[0].readDelayLin(pre_ms_) : inL;
        float xr = pre_ms_ > 1.0f ? pre_[1].readDelayLin(pre_ms_) : inR;
        if (freeze_) xl = xr = 0.0f;
        float x = 0.5f * (xl + xr), d = 0.5f * (xl - xr);
        for (int i = 0; i < 4; ++i)       // allpass diffusion on the mid signal
        {
            const float z = ap_[i].readDelayLin(apLen_[i]);
            const float v = x + 0.62f * z;
            ap_[i].write(v);
            x = z - 0.62f * v;
        }
        lfo_ += 0.7f / sr_; if (lfo_ >= 1.0f) lfo_ -= 1.0f;
        float o[8];
        for (int i = 0; i < 8; ++i)
        {
            const float m = mod_ * std::sin(kTwoPi * (lfo_ + 0.125f * (float)i));
            o[i] = line_[i].readDelayLin(len_[i] + m);
        }
        // Householder mix: y = o - (2/8) * sum(o)
        float sum = 0.0f; for (float v : o) sum += v;
        sum *= 0.25f;
        for (int i = 0; i < 8; ++i)
        {
            const float in = (i & 1) ? x - d * 0.5f : x + d * 0.5f;
            float y = (o[i] - sum) * fb_[i];
            y = damp_[i].lp(y);
            line_[i].write(undenorm(y + in * 0.35f));
        }
        outL = (o[0] + o[2] + o[4] + o[6]) * 0.5f;
        outR = (o[1] + o[3] + o[5] + o[7]) * 0.5f;
    }
private:
    static constexpr float kLen[8] = {1031, 1327, 1523, 1871, 2053, 2357, 2647, 2999};
    static constexpr float kAp[4] = {142, 107, 379, 277};
    float sr_ = 44100.0f, len_[8] = {}, fb_[8] = {}, apLen_[4] = {}, mod_ = 0.0f, lfo_ = 0.0f, pre_ms_ = 0.0f;
    bool freeze_ = false;
    Ring line_[8], ap_[4], pre_[2];
    OnePole damp_[8];
};

// Step clock: locked to the MPC's song position while it plays, free-running otherwise.
// begin() once per block; tick() once per sample returns true on each new step.
struct StepClock
{
    double ppq = 0.0, beatsPerSample = 0.0;
    int64_t lastStep = -1;
    double freeCount = 0.0, freeLen = 1.0;
    bool locked = false;
    float stepBeats = 0.25f;
    void reset() { lastStep = -1; freeCount = 0.0; }
    template <class Ctx>
    void begin(const Ctx& ctx, float beats, float freeSamples, bool sync)
    {
        const double bpm = (ctx.transport.bpm > 1.0 && ctx.transport.bpm < 999.0) ? ctx.transport.bpm : 120.0;
        beatsPerSample = bpm / (60.0 * ctx.sampleRate);
        stepBeats = std::max(beats, 1.0f / 64.0f);
        const bool canLock = sync && ctx.transport.playing && ctx.transport.ppqValid;
        if (canLock != locked) lastStep = -1;
        locked = canLock;
        ppq = ctx.transport.ppqPos;
        freeLen = sync ? (double)stepBeats / beatsPerSample : std::max((double)freeSamples, 16.0);
    }
    bool tick()
    {
        if (locked)
        {
            const int64_t s = (int64_t)std::floor(ppq / (double)stepBeats);
            ppq += beatsPerSample;
            if (s != lastStep) { lastStep = s; return true; }
            return false;
        }
        if (lastStep < 0) { lastStep = 0; freeCount = 0.0; return true; }
        freeCount += 1.0;
        if (freeCount >= freeLen) { freeCount -= freeLen; return true; }
        return false;
    }
    // Length of one step in samples (for sizing grains to the beat).
    float stepSamples() const { return (float)freeLen; }
};

// Envelope follower (peak, separate attack/release).
struct EnvFollow
{
    float v = 0.0f, a = 0.01f, r = 0.001f;
    void set(float attMs, float relMs, float sr)
    {
        a = 1.0f - std::exp(-1.0f / (std::max(attMs, 0.05f) * 0.001f * sr));
        r = 1.0f - std::exp(-1.0f / (std::max(relMs, 0.05f) * 0.001f * sr));
    }
    void reset() { v = 0.0f; }
    float process(float x)
    {
        x = std::fabs(x);
        v += (x > v ? a : r) * (x - v);
        v = undenorm(v);
        return v;
    }
};

} // namespace bi
