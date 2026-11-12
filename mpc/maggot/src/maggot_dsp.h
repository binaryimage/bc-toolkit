// maggot_dsp.h - building blocks for Maggot (binaryimage's noise-but-beautiful mangle synth).
//
// SPDX-License-Identifier: MIT
//
// Original code. Everything here is allocation-free after prepare() and safe on the audio thread.
//
//   MorphOsc      sine > triangle > saw > square in one knob (PolyBLEP edges), with a sine wavefolder
//   NoiseGen      white, pink, crackle (sparse sharp spikes) and dust (sparse soft grains)
//   Resonator     note-tuned comb / Karplus string: turns noise and grit into bells and strings
//   Svf           Zavalishin state-variable filter (LP / BP / HP / Notch) with saturation
//   MangleBuffer  4 s stereo recorder + playhead: tape stop, stutter, reverse, freeze
//   Smear         granular cloud reading the same buffer (up to 8 grains)
//   Crusher       bit depth + sample-rate reduction
//   Wow           tape wow/flutter (modulated short delay) with soft saturation

#ifndef MAGGOT_DSP_H
#define MAGGOT_DSP_H

#include <cmath>
#include <cstdint>
#include <vector>
#include <algorithm>
#include "dsp/math.h"

namespace mg {

using mpc::dsp::fastTanh;
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float wrap01(float p) { return p - std::floor(p); }

// Fast sine of a phase in cycles (parabolic, ~0.1 % error). Good enough inside folders and LFOs.
inline float fsin(float ph)
{
    float x = ph - std::floor(ph);           // 0..1
    x = x * 2.0f - 1.0f;                     // -1..1  (maps to -pi..pi)
    const float y = 4.0f * x * (1.0f - std::fabs(x));
    return -(0.225f * (y * std::fabs(y) - y) + y);
}

struct Rng
{
    uint32_t s = 0x9E3779B9u;
    void seed(uint32_t v) { s = v ? v : 1u; }
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uni() { return (float)(next() >> 8) * (1.0f / 16777216.0f); }      // 0..1
    float bi() { return uni() * 2.0f - 1.0f; }                                 // -1..1
};

inline float blep(float t, float dt)
{
    if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}

// ---------------------------------------------------------------------------
// Morphing oscillator. shape 0..1: 0 sine, 1/3 triangle, 2/3 saw, 1 square.
// fold 0..1 runs the result through a sine wavefolder (gentle shimmer -> shredded).
// ---------------------------------------------------------------------------
struct MorphOsc
{
    float ph = 0.0f;
    void reset(float p) { ph = p; }
    float process(float inc, float shape, float fold, float pm = 0.0f)
    {
        inc = clampf(inc, 1e-6f, 0.45f);
        const float p = wrap01(ph + pm);
        float y;
        const float s3 = shape * 3.0f;
        if (s3 < 1.0f)
        {
            const float sn = fsin(p);
            const float tr = p < 0.5f ? 4.0f * p - 1.0f : 3.0f - 4.0f * p;
            y = sn + (tr - sn) * s3;
        }
        else if (s3 < 2.0f)
        {
            const float tr = p < 0.5f ? 4.0f * p - 1.0f : 3.0f - 4.0f * p;
            const float sw = 2.0f * p - 1.0f - blep(p, inc);
            y = tr + (sw - tr) * (s3 - 1.0f);
        }
        else
        {
            const float sw = 2.0f * p - 1.0f - blep(p, inc);
            float sq = p < 0.5f ? 1.0f : -1.0f;
            sq += blep(p, inc);
            sq -= blep(wrap01(p + 0.5f), inc);
            y = sw + (sq - sw) * clampf(s3 - 2.0f, 0.0f, 1.0f);
        }
        if (fold > 0.001f) y = fsin(y * (0.25f + fold * 2.2f));
        ph += inc;
        if (ph >= 1.0f) ph -= 1.0f;
        return y;
    }
};

// ---------------------------------------------------------------------------
// Noise: 0 white, 1 pink, 2 crackle, 3 dust. color 0..1 tilts it darker/brighter.
// ---------------------------------------------------------------------------
struct NoiseGen
{
    Rng rng;
    float b0 = 0, b1 = 0, b2 = 0, lp = 0, dust = 0;
    float process(int type, float color)
    {
        float x;
        switch (type)
        {
            case 1: {  // Paul Kellet's economy pink
                const float w = rng.bi();
                b0 = 0.99765f * b0 + w * 0.0990460f;
                b1 = 0.96300f * b1 + w * 0.2965164f;
                b2 = 0.57000f * b2 + w * 1.0526913f;
                x = (b0 + b1 + b2 + w * 0.1848f) * 0.25f;
                break;
            }
            case 2: {  // crackle: rare sharp spikes, like a broken cable
                const float r = rng.uni();
                x = r > 0.9985f ? rng.bi() * 3.0f : 0.0f;
                break;
            }
            case 3: {  // dust: sparse soft grains that decay
                if (rng.uni() > 0.997f) dust = rng.bi();
                dust *= 0.985f;
                x = dust * 1.5f;
                break;
            }
            default: x = rng.bi(); break;
        }
        // color: one-pole tilt (0 = dark lowpass, 0.5 = flat, 1 = bright highpass-ish)
        const float a = 0.02f + 0.96f * color;
        lp += (x - lp) * a;
        return color < 0.5f ? lp : x + (x - lp) * (color - 0.5f) * 1.5f;
    }
};

// ---------------------------------------------------------------------------
// Resonator: a note-tuned comb filter with damping in the loop (Karplus-Strong family).
// ---------------------------------------------------------------------------
struct Resonator
{
    std::vector<float> buf;
    int w = 0, size = 0;
    float lp = 0.0f;
    void prepare(float sr) { size = (int)(sr / 20.0f) + 4; buf.assign((size_t)size, 0.0f); w = 0; lp = 0.0f; }
    void clear() { std::fill(buf.begin(), buf.end(), 0.0f); lp = 0.0f; }
    // period in samples, feedback 0..0.999, damp 0..1 (0 bright, 1 dark)
    float process(float x, float period, float feedback, float damp)
    {
        period = clampf(period, 2.0f, (float)(size - 3));
        float rp = (float)w - period;
        while (rp < 0.0f) rp += (float)size;
        const int i0 = (int)rp;
        const float f = rp - (float)i0;
        const int i1 = i0 + 1 >= size ? 0 : i0 + 1;
        const float d = buf[(size_t)i0] + (buf[(size_t)i1] - buf[(size_t)i0]) * f;
        lp += (d - lp) * (1.0f - damp * 0.95f);
        const float y = x + lp * feedback;
        buf[(size_t)w] = fastTanh(y);
        if (++w >= size) w = 0;
        return d;
    }
};

// ---------------------------------------------------------------------------
// State-variable filter: 0 LP, 1 BP, 2 HP, 3 Notch. cutoff set per sample allowed (filter FM).
// ---------------------------------------------------------------------------
struct Svf
{
    float ic1 = 0, ic2 = 0;
    void reset() { ic1 = ic2 = 0.0f; }
    float process(float x, float g, float k, int type)
    {
        const float a1 = 1.0f / (1.0f + g * (g + k));
        const float a2 = g * a1, a3 = g * a2;
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        ic1 = fastTanh(ic1 * 0.5f) * 2.0f;     // gentle saturation keeps self-oscillation in check
        switch (type)
        {
            case 1: return v1 * k;
            case 2: return x - k * v1 - v2;
            case 3: return x - k * v1;
            default: return v2;
        }
    }
};
// tan(pi*f/sr) approximation for g (good to ~0.45 sr)
inline float svfG(float hz, float sr)
{
    const float wc = 3.14159265f * clampf(hz, 10.0f, sr * 0.45f) / sr;
    const float w2 = wc * wc;
    return wc * (1.0f + w2 * (0.333333f + w2 * (0.133333f + w2 * 0.0539683f)));
}

// ---------------------------------------------------------------------------
// MangleBuffer: always records the last 4 s. The playhead modes:
//   0 live (pass-through)
//   1 tape stop   - speed falls 1 -> 0 over stopTime, then holds silence; on release it spins back up
//   2 stutter     - loops the last `loopLen` samples captured at the press
//   3 reverse     - plays backwards from the press point, looping over loopLen
//   4 freeze      - crossfaded loop of the last ~loopLen, held as a drone
// Crossfades in and out of every mode so nothing clicks.
// ---------------------------------------------------------------------------
class MangleBuffer
{
public:
    void prepare(float sr)
    {
        sr_ = sr;
        size_ = (int)(sr * 4.0f);
        L_.assign((size_t)size_, 0.0f); R_.assign((size_t)size_, 0.0f);
        w_ = 0; mode_ = 0; mix_ = 0.0f; speed_ = 1.0f;
    }
    void clear() { std::fill(L_.begin(), L_.end(), 0.0f); std::fill(R_.begin(), R_.end(), 0.0f); mode_ = 0; mix_ = 0.0f; }

    // Call once per block with the button states. Priority: tape stop > freeze > reverse > stutter.
    void setModes(bool tapeStop, bool stutter, bool reverse, bool freeze, float loopSamples, float stopSeconds)
    {
        int want = tapeStop ? 1 : (freeze ? 4 : (reverse ? 3 : (stutter ? 2 : 0)));
        loopLen_ = clampf(loopSamples, 64.0f, (float)size_ * 0.45f);
        stopRate_ = 1.0f / std::max(0.02f, stopSeconds) / sr_;
        if (want != mode_)
        {
            if (want != 0)
            {
                // Capture the point we lock onto.
                anchor_ = (float)w_;
                rp_ = (float)w_ - (want == 3 ? 1.0f : loopLen_);
                if (want == 1) { rp_ = (float)w_ - 2.0f; speed_ = 1.0f; spinUp_ = false; }
            }
            else if (mode_ == 1)
            {
                spinUp_ = true;          // tape stop released: spin back up before fading to live
                releaseMode_ = 1;
                return;                  // stay in tape mode until spun up
            }
            mode_ = want;
        }
    }

    void process(float& l, float& r)
    {
        L_[(size_t)w_] = l; R_[(size_t)w_] = r;
        if (++w_ >= size_) w_ = 0;

        const float target = mode_ == 0 ? 0.0f : 1.0f;
        mix_ += (target - mix_) * 0.004f;
        if (mix_ < 1e-4f && mode_ == 0) return;

        float ol = 0.0f, orr = 0.0f;
        switch (mode_)
        {
            case 1:   // tape stop
            {
                if (spinUp_)
                {
                    speed_ += stopRate_ * 2.0f;
                    if (speed_ >= 1.0f) { speed_ = 1.0f; spinUp_ = false; mode_ = 0; }
                }
                else speed_ = std::max(0.0f, speed_ - stopRate_);
                rp_ += speed_;
                // Keep the playhead behind the writer.
                read(rp_, ol, orr);
                const float g = std::min(1.0f, speed_ * 4.0f);     // fade out as it grinds to a halt
                ol *= g; orr *= g;
                break;
            }
            case 2:   // stutter loop
            {
                rp_ += 1.0f;
                if (rp_ >= anchor_) rp_ -= loopLen_;
                readXf(rp_, anchor_ - loopLen_, ol, orr);
                break;
            }
            case 3:   // reverse
            {
                rp_ -= 1.0f;
                if (rp_ <= anchor_ - loopLen_) rp_ += loopLen_;
                read(rp_, ol, orr);
                const float edge = std::min(rp_ - (anchor_ - loopLen_), anchor_ - rp_);
                const float g = clampf(edge / 256.0f, 0.0f, 1.0f);
                ol *= g; orr *= g;
                break;
            }
            case 4:   // freeze
            {
                rp_ += 1.0f;
                if (rp_ >= anchor_) rp_ -= loopLen_;
                readXf(rp_, anchor_ - loopLen_, ol, orr);
                break;
            }
            default:
                break;
        }
        l = l + (ol - l) * mix_;
        r = r + (orr - r) * mix_;
    }

    // Read helpers for the granular smear.
    int writePos() const { return w_; }
    int size() const { return size_; }
    void read(float pos, float& l, float& r) const
    {
        while (pos < 0.0f) pos += (float)size_;
        while (pos >= (float)size_) pos -= (float)size_;
        const int i0 = (int)pos;
        const float f = pos - (float)i0;
        const int i1 = i0 + 1 >= size_ ? 0 : i0 + 1;
        l = L_[(size_t)i0] + (L_[(size_t)i1] - L_[(size_t)i0]) * f;
        r = R_[(size_t)i0] + (R_[(size_t)i1] - R_[(size_t)i0]) * f;
    }

private:
    // Loop read with a short crossfade across the loop seam.
    void readXf(float pos, float start, float& l, float& r) const
    {
        read(pos, l, r);
        const float xf = std::min(512.0f, loopLen_ * 0.25f);
        const float into = pos - start;
        if (into < xf)
        {
            float l2, r2;
            read(pos + loopLen_, l2, r2);   // the matching point past the loop end (fresh audio)
            const float g = into / xf;
            l = l2 + (l - l2) * g;
            r = r2 + (r - r2) * g;
        }
    }

    float sr_ = 44100.0f;
    std::vector<float> L_, R_;
    int size_ = 0, w_ = 0, mode_ = 0, releaseMode_ = 0;
    float mix_ = 0.0f, rp_ = 0.0f, anchor_ = 0.0f, loopLen_ = 4096.0f, speed_ = 1.0f, stopRate_ = 0.0001f;
    bool spinUp_ = false;
};

// ---------------------------------------------------------------------------
// Smear: a granular cloud over the recent past in the MangleBuffer.
// amount 0..1 (wet), size 0..1 (grain 20..300 ms), spread scatters grain start and pitch.
// ---------------------------------------------------------------------------
class Smear
{
public:
    void prepare(float sr) { sr_ = sr; for (auto& g : grains_) g.active = false; rng_.seed(0xBADF00Du); counter_ = 0.0f; }
    void process(const MangleBuffer& buf, float& l, float& r, float amount, float size, float pitchSpread)
    {
        if (amount <= 0.001f) return;
        const float grainLen = (0.02f + size * 0.28f) * sr_;
        counter_ -= 1.0f;
        if (counter_ <= 0.0f)
        {
            counter_ = grainLen / 4.0f;     // ~4 overlapping grains
            for (auto& g : grains_)
                if (!g.active)
                {
                    g.active = true;
                    g.len = grainLen;
                    g.t = 0.0f;
                    const float back = grainLen + rng_.uni() * sr_ * (0.05f + size * 0.6f);
                    g.pos = (float)buf.writePos() - back;
                    const float semis = (rng_.uni() < 0.5f ? 0.0f : (rng_.uni() < 0.5f ? 12.0f : -12.0f)) * pitchSpread
                                      + rng_.bi() * 0.2f * pitchSpread;
                    g.rate = std::exp2(semis / 12.0f);
                    g.pan = rng_.bi() * 0.8f;
                    break;
                }
        }
        float sl = 0.0f, sr = 0.0f;
        for (auto& g : grains_)
        {
            if (!g.active) continue;
            float gl, gr;
            buf.read(g.pos, gl, gr);
            const float x = g.t / g.len;
            const float env = 4.0f * x * (1.0f - x);     // parabolic window
            const float m = (gl + gr) * 0.5f * env;
            sl += m * (1.0f - g.pan) * 0.5f;
            sr += m * (1.0f + g.pan) * 0.5f;
            g.pos += g.rate;
            g.t += 1.0f;
            if (g.t >= g.len) g.active = false;
        }
        l = l + (sl * 1.4f - l) * amount;
        r = r + (sr * 1.4f - r) * amount;
    }
private:
    struct Grain { bool active = false; float pos = 0, rate = 1, t = 0, len = 1, pan = 0; };
    Grain grains_[8];
    Rng rng_;
    float sr_ = 44100.0f, counter_ = 0.0f;
};

// ---------------------------------------------------------------------------
// Crusher: bit depth (16..2) and sample-rate hold (1..32x), mixed.
// ---------------------------------------------------------------------------
struct Crusher
{
    float holdL = 0, holdR = 0, cnt = 0;
    void process(float& l, float& r, float bits, float down)
    {
        cnt += 1.0f;
        if (cnt >= down) { cnt -= down; holdL = l; holdR = r; }
        float a = holdL, b = holdR;
        if (bits < 15.9f)
        {
            const float q = std::exp2(bits - 1.0f);
            a = std::round(a * q) / q;
            b = std::round(b * q) / q;
        }
        l = a; r = b;
    }
};

// ---------------------------------------------------------------------------
// Wow: tape wow/flutter, a gently modulated short delay plus soft saturation.
// ---------------------------------------------------------------------------
class Wow
{
public:
    void prepare(float sr) { sr_ = sr; size_ = (int)(sr * 0.05f) + 4; L_.assign((size_t)size_, 0.0f); R_.assign((size_t)size_, 0.0f); w_ = 0; }
    void clear() { std::fill(L_.begin(), L_.end(), 0.0f); std::fill(R_.begin(), R_.end(), 0.0f); }
    void process(float& l, float& r, float depth)
    {
        L_[(size_t)w_] = l; R_[(size_t)w_] = r;
        if (++w_ >= size_) w_ = 0;
        if (depth <= 0.001f) return;
        ph1_ += 0.6f / sr_; if (ph1_ >= 1.0f) ph1_ -= 1.0f;
        ph2_ += 7.3f / sr_; if (ph2_ >= 1.0f) ph2_ -= 1.0f;
        const float mod = (fsin(ph1_) * 0.8f + fsin(ph2_) * 0.2f) * depth * 0.004f * sr_;
        const float d = 0.008f * sr_ + mod;
        float rp = (float)w_ - d;
        while (rp < 0.0f) rp += (float)size_;
        const int i0 = (int)rp; const float f = rp - (float)i0; const int i1 = i0 + 1 >= size_ ? 0 : i0 + 1;
        const float wl = L_[(size_t)i0] + (L_[(size_t)i1] - L_[(size_t)i0]) * f;
        const float wr = R_[(size_t)i0] + (R_[(size_t)i1] - R_[(size_t)i0]) * f;
        l = fastTanh(wl * (1.0f + depth)) / (1.0f + depth * 0.5f);
        r = fastTanh(wr * (1.0f + depth)) / (1.0f + depth * 0.5f);
    }
private:
    std::vector<float> L_, R_;
    int size_ = 0, w_ = 0;
    float sr_ = 44100.0f, ph1_ = 0.0f, ph2_ = 0.3f;
};

} // namespace mg

#endif // MAGGOT_DSP_H
