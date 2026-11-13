// golem_dsp.h - the analog parts of Golem: VCOs, the 4012/4072 ladder filter, noise and the
// spring reverb. Original binaryimage code (MIT).
#pragma once
#include "bi_fx.h"
#include <cmath>

namespace golem {
using namespace bi;

inline float polyBlep(float t, float dt)
{
    if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}

// One VCO: every waveform at once, because any of them can be patched somewhere.
struct Vco
{
    float ph = 0.0f, drift = 0.0f, driftT = 0.0f;
    int driftCount = 0;
    float saw = 0, pulse = 0, tri = 0, sine = 0, square = 0;
    bool wrapped = false;
    float wrapFrac = 0.0f;
    void reset(float p) { ph = p; }
    // inc = cycles per sample, pw = 0.05..0.95; syncReset/syncFrac hard-sync this VCO to another.
    void tick(float inc, float pw, bool syncReset = false, float syncFrac = 0.0f)
    {
        inc = clampf(inc, 1e-7f, 0.45f);
        ph += inc;
        wrapped = false;
        if (ph >= 1.0f) { ph -= 1.0f; wrapped = true; wrapFrac = ph / inc; }
        if (syncReset) { ph = syncFrac * inc; }
        const float dt = inc;
        saw = 2.0f * ph - 1.0f - polyBlep(ph, dt);
        float p2 = ph + (1.0f - pw); if (p2 >= 1.0f) p2 -= 1.0f;
        const float saw2 = 2.0f * p2 - 1.0f - polyBlep(p2, dt);
        pulse = (saw - saw2) * 0.5f + (1.0f - 2.0f * pw) * 0.5f;       // centred pulse
        float p3 = ph + 0.5f; if (p3 >= 1.0f) p3 -= 1.0f;
        const float saw3 = 2.0f * p3 - 1.0f - polyBlep(p3, dt);
        square = (saw - saw3) * 0.5f;
        tri = 1.0f - 4.0f * std::fabs(ph - 0.5f);
        sine = std::sin(kTwoPi * ph);
    }
};

// Padé tan, good to ~0.1% up to x = 1.1 (cutoff below ~0.35 of the oversampled rate).
inline float fastTan(float x) { const float x2 = x * x; return x * (15.0f - x2) / (15.0f - 6.0f * x2); }

// Four-pole transistor ladder, zero-delay feedback, run at 2x the sample rate.
// type 0 = ARP 4012 (the early, Moog-style ladder: fat, loses bass as resonance rises)
// type 1 = ARP 4072 (ARP's own: brighter, keeps its low end, screams harder)
class Ladder
{
public:
    void setSampleRate(float sr) { sr2_ = sr * 2.0f; }
    void reset() { for (auto& s : s_) s = 0.0f; prev_ = 0.0f; }
    float process(float x, float cutoffHz, float reso, int type, float drive)
    {
        const float fc = clampf(cutoffHz, 8.0f, sr2_ * 0.23f);
        const float g = fastTan(kPi * fc / sr2_);
        const float G = g / (1.0f + g);
        const float k = reso * reso * (type == 0 ? 4.7f : 4.9f) + reso * (1.0f - reso) * 3.2f;
        const float pre = 1.0f + 2.5f * drive;
        float out = 0.0f;
        for (int o = 0; o < 2; ++o)
        {
            const float in = (o == 0 ? 0.5f * (prev_ + x) : x) * pre;
            const float b = 1.0f - G;
            const float sigma = G * G * G * b * s_[0] + G * G * b * s_[1] + G * b * s_[2] + b * s_[3];
            float comp = type == 1 ? 1.0f + 0.6f * k : 1.0f + 0.15f * k;
            float u = (in * comp - k * sigma) / (1.0f + k * G * G * G * G);
            u = type == 0 ? fastTanh(u) : fastTanh(u * 1.25f + 0.05f) * 0.82f;
            float y = u;
            for (int i = 0; i < 4; ++i)
            {
                const float v = (y - s_[i]) * G;
                const float yo = v + s_[i];
                s_[i] = undenorm(yo + v);
                y = type == 1 && i == 1 ? fastTanh(yo) : yo;
            }
            out += y;
        }
        prev_ = x;
        return out * 0.5f / pre * (1.0f + 0.3f * drive);
    }
private:
    float sr2_ = 88200.0f, s_[4] = {0, 0, 0, 0}, prev_ = 0.0f;
};

// White to pink to low-frequency noise (the 2600's noise colour slider).
struct Noise
{
    Rng rng;
    float b0 = 0, b1 = 0, b2 = 0, lf = 0;
    float process(float color)
    {
        const float w = rng.bi();
        b0 = 0.99765f * b0 + w * 0.0990460f;
        b1 = 0.96300f * b1 + w * 0.2965164f;
        b2 = 0.57000f * b2 + w * 1.0526913f;
        const float pink = (b0 + b1 + b2 + w * 0.1848f) * 0.22f;
        lf += 0.0008f * (w * 6.0f - lf);
        if (color < 0.5f) return lerpf(w * 0.6f, pink, color * 2.0f);
        return lerpf(pink, clampf(lf * 5.0f, -1.0f, 1.0f), (color - 0.5f) * 2.0f);
    }
};

// A cheap spring tank: a chain of stretched allpasses (the "chirp"), a long feedback delay with
// damping, and a little wobble, so it boings like the 2600's springs.
class Spring
{
public:
    void prepare(float sr, float delayMs)
    {
        sr_ = sr;
        line_.prepare((int)(sr * 0.12f));
        baseD_ = delayMs * 0.001f * sr;
        for (auto& a : ap_) a.prepare(8);
        reset();
    }
    void reset() { line_.clear(); for (auto& a : ap_) a.clear(); damp_.reset(); hp_.reset(); fb_ = 0.0f; ph_ = 0.0f; }
    float process(float x, float decay, float tone)
    {
        ph_ += 1.7f / sr_; if (ph_ >= 1.0f) ph_ -= 1.0f;
        float y = x + fb_;
        for (int i = 0; i < 12; ++i)          // dispersion: each stage = 2-sample allpass
        {
            const float z = ap_[i].readDelayLin(2.0f);
            const float v = y + 0.62f * z;
            ap_[i].write(undenorm(v));
            y = z - 0.62f * v;
        }
        line_.write(y);
        const float d = baseD_ * (1.0f + 0.004f * std::sin(kTwoPi * ph_));
        float o = line_.readDelayLin(d);
        damp_.set(1500.0f + 6500.0f * tone, sr_);
        hp_.set(120.0f, sr_);
        o = hp_.hp(damp_.lp(o));
        fb_ = o * (0.55f + 0.4f * decay);
        return o;
    }
private:
    float sr_ = 44100.0f, baseD_ = 1500.0f, fb_ = 0.0f, ph_ = 0.0f;
    Ring line_, ap_[12];
    OnePole damp_, hp_;
};
} // namespace golem
