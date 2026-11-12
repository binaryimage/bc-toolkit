// janus_dsp.h - building blocks for Janus (an AN1x-style virtual analogue).
//
// SPDX-License-Identifier: MIT
//
// Everything here is allocation free after prepare() and safe on the audio thread.
//   Osc         PolyBLEP saw / pulse / triangle / sine with hard sync and linear FM input
//   Ladder      4-pole zero-delay-feedback ladder with pole mixing:
//               LPF24, LPF18, LPF12, BPF, HPF (the AN1x's filter types)
//   OnePoleHP   the extra non-resonant high-pass after the main filter
//   PitchEG     attack-free decay envelope for pitch blips and sync sweeps
//   FX          distortion, chorus/flanger, tempo delay, reverb (stereo)

#ifndef JANUS_DSP_H
#define JANUS_DSP_H

#include <cmath>
#include <cstring>
#include <vector>
#include "dsp/math.h"

namespace janus {

using mpc::dsp::fastTanh;

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Fast 2^x for control-rate use (max error ~0.2 %).
inline float fastExp2(float x)
{
    x = clampf(x, -30.0f, 30.0f);
    const float fi = std::floor(x);
    const float f = x - fi;
    // 3rd-order polynomial for 2^f on [0,1)
    const float p = 1.0f + f * (0.6951786f + f * (0.2261850f + f * 0.0782080f));
    return std::ldexp(p, (int)fi);
}

// ---------------------------------------------------------------------------
// PolyBLEP helpers
// ---------------------------------------------------------------------------
inline float blep(float t, float dt)
{
    if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}

// ---------------------------------------------------------------------------
// Oscillator. Waves: 0 Saw, 1 Pulse, 2 Triangle, 3 Sine.
// process(inc) takes the phase increment (freq / sr) so FM can change it per sample.
// Hard sync: call process() with reset = true and the fraction of the sample
// after the master's wrap; the slave restarts its phase there.
// ---------------------------------------------------------------------------
class Osc
{
public:
    void reset(float phase = 0.0f) { phase_ = phase; tri_ = 0.0f; }
    void setWave(int w) { wave_ = w; }
    void setPulseWidth(float pw) { pw_ = clampf(pw, 0.03f, 0.97f); }
    float phase() const { return phase_; }

    // Returns true in `wrapped` when the phase wrapped this sample (for using this osc as a sync master),
    // and sets wrapFrac to how far past the wrap the phase landed (in samples, 0..1).
    float process(float inc, bool& wrapped, float& wrapFrac)
    {
        inc = clampf(inc, -0.45f, 0.45f);
        const float dt = std::fabs(inc) < 1e-6f ? 1e-6f : std::fabs(inc);
        float y = shape(phase_, dt);
        phase_ += inc;
        wrapped = false;
        if (phase_ >= 1.0f) { phase_ -= 1.0f; wrapped = true; wrapFrac = phase_ / dt; }
        else if (phase_ < 0.0f) { phase_ += 1.0f; }   // through-zero FM
        return y;
    }

    // Slave version: restart at the master's wrap point.
    float processSynced(float inc, bool masterWrapped, float masterFrac)
    {
        inc = clampf(inc, -0.45f, 0.45f);
        const float dt = std::fabs(inc) < 1e-6f ? 1e-6f : std::fabs(inc);
        float y = shape(phase_, dt);
        if (pendingJump_ != 0.0f)
        {
            // Second half of the BLEP from last sample's reset.
            y -= pendingJump_ * 0.5f * (1.0f - pendingT_) * (1.0f - pendingT_);
            pendingJump_ = 0.0f;
        }
        if (masterWrapped)
        {
            // The master wrapped (1 - masterFrac) samples from now: restart the slave there.
            const float before = naive(phase_ + inc * (1.0f - masterFrac));
            const float jump = naive(0.0f) - before;
            const float t = clampf(masterFrac, 0.0f, 1.0f);
            y += jump * 0.5f * t * t;                 // first half of the BLEP on this sample
            phase_ = t * inc;
            if (phase_ < 0.0f) phase_ += 1.0f;
            pendingJump_ = jump; pendingT_ = t;
            return y;
        }
        phase_ += inc;
        if (phase_ >= 1.0f) phase_ -= 1.0f;
        else if (phase_ < 0.0f) phase_ += 1.0f;
        return y;
    }

private:
    float naive(float p) const
    {
        p -= std::floor(p);
        switch (wave_)
        {
            case 0: return 2.0f * p - 1.0f;
            case 1: return p < pw_ ? 1.0f : -1.0f;
            case 2: return p < 0.5f ? 4.0f * p - 1.0f : 3.0f - 4.0f * p;
            default: return mpc::dsp::fastSin01(p);
        }
    }
    float shape(float p, float dt)
    {
        switch (wave_)
        {
            case 0: return 2.0f * p - 1.0f - blep(p, dt);
            case 1:
            {
                float y = p < pw_ ? 1.0f : -1.0f;
                y += blep(p, dt);
                float q = p - pw_; if (q < 0.0f) q += 1.0f;
                y -= blep(q, dt);
                return y;
            }
            case 2:
            {
                // Leaky-integrated band-limited square -> triangle.
                float sq = p < 0.5f ? 1.0f : -1.0f;
                sq += blep(p, dt);
                float q = p - 0.5f; if (q < 0.0f) q += 1.0f;
                sq -= blep(q, dt);
                tri_ = tri_ * 0.9995f + sq * 4.0f * dt;
                return clampf(tri_, -1.2f, 1.2f);
            }
            default: return mpc::dsp::fastSin01(p);
        }
    }

    int wave_ = 0;
    float phase_ = 0.0f, pw_ = 0.5f, tri_ = 0.0f;
    float pendingJump_ = 0.0f, pendingT_ = 0.0f;
};

// ---------------------------------------------------------------------------
// Ladder filter: Zavalishin TPT ladder, saturating input, pole-mixed outputs.
// Types: 0 LPF24, 1 LPF18, 2 LPF12, 3 BPF, 4 HPF.
// ---------------------------------------------------------------------------
class Ladder
{
public:
    void setSampleRate(float sr) { sr_ = sr; }
    void reset() { s1_ = s2_ = s3_ = s4_ = 0.0f; }

    // Control rate: cutoff in Hz, resonance 0..1.
    void set(float hz, float res)
    {
        hz = clampf(hz, 15.0f, sr_ * 0.45f);
        const float wc = 3.14159265f * hz / sr_;
        // tan() approximation good to ~0.45 * sr
        const float w2 = wc * wc;
        const float g = wc * (1.0f + w2 * (0.333333f + w2 * (0.133333f + w2 * 0.0539683f)));
        G_ = g / (1.0f + g);
        k_ = clampf(res, 0.0f, 1.0f) * 4.2f;   // self-oscillates just past 4
    }

    float process(float x, int type, float drive)
    {
        const float G = G_, G2 = G * G, G3 = G2 * G, G4 = G3 * G;
        // Output estimate from the states (the zero-delay part).
        const float S = G3 * s1_ * (1.0f - G) + G2 * s2_ * (1.0f - G) + G * s3_ * (1.0f - G) + s4_ * (1.0f - G);
        // Resonance compensation keeps the low end for low-pass types.
        const float comp = type <= 2 ? (1.0f + 0.5f * k_) : 1.0f;
        const float in = fastTanh(x * drive) * comp;
        float u = (in - k_ * S) / (1.0f + k_ * G4);
        u = fastTanh(u);
        // Four one-pole stages (TPT).
        float v = (u - s1_) * G;  const float y1 = v + s1_; s1_ = y1 + v;
        v = (y1 - s2_) * G;       const float y2 = v + s2_; s2_ = y2 + v;
        v = (y2 - s3_) * G;       const float y3 = v + s3_; s3_ = y3 + v;
        v = (y3 - s4_) * G;       const float y4 = v + s4_; s4_ = y4 + v;
        switch (type)
        {
            case 0: return y4;
            case 1: return y3;
            case 2: return y2;
            case 3: return 2.0f * (y1 - y2);       // 12 dB band-pass mix
            default: return u - 2.0f * y1 + y2;   // 12 dB high-pass mix
        }
    }

private:
    float sr_ = 44100.0f, G_ = 0.5f, k_ = 0.0f;
    float s1_ = 0, s2_ = 0, s3_ = 0, s4_ = 0;
};

// Two cascaded one-pole high-passes (12 dB, no resonance) - the AN1x's extra HPF.
class HP12
{
public:
    void reset() { a_ = b_ = xa_ = xb_ = 0.0f; }
    void set(float hz, float sr) { const float c = std::exp(-6.2831853f * clampf(hz, 5.0f, sr * 0.4f) / sr); r_ = c; }
    float process(float x)
    {
        const float ya = r_ * (a_ + x - xa_); xa_ = x; a_ = ya;
        const float yb = r_ * (b_ + ya - xb_); xb_ = ya; b_ = yb;
        return yb;
    }
private:
    float r_ = 0.999f, a_ = 0, b_ = 0, xa_ = 0, xb_ = 0;
};

// Pitch envelope: jumps to 1 on a note and decays exponentially to 0.
class PitchEG
{
public:
    void set(float decayMs, float sr) { const float n = clampf(decayMs, 1.0f, 10000.0f) * 0.001f * sr; c_ = std::exp(-4.6f / n); }
    void trigger() { v_ = 1.0f; }
    void reset() { v_ = 0.0f; }
    float process() { const float y = v_; v_ *= c_; return y; }
private:
    float v_ = 0.0f, c_ = 0.999f;
};

// ---------------------------------------------------------------------------
// Effects (stereo, in place)
// ---------------------------------------------------------------------------
class StereoDelayLine
{
public:
    void init(int maxSamples) { buf_.assign((size_t)maxSamples + 4, 0.0f); size_ = (int)buf_.size(); w_ = 0; }
    void clear() { std::fill(buf_.begin(), buf_.end(), 0.0f); }
    void write(float x) { buf_[(size_t)w_] = x; if (++w_ >= size_) w_ = 0; }
    // Fractional read, d samples behind the last write (d >= 1).
    float read(float d) const
    {
        d = clampf(d, 1.0f, (float)(size_ - 3));
        float rp = (float)w_ - d; while (rp < 0.0f) rp += (float)size_;
        const int i0 = (int)rp; const float f = rp - (float)i0;
        const int i1 = i0 + 1 >= size_ ? 0 : i0 + 1;
        return buf_[(size_t)i0] + (buf_[(size_t)i1] - buf_[(size_t)i0]) * f;
    }
private:
    std::vector<float> buf_;
    int size_ = 0, w_ = 0;
};

class Chorus
{
public:
    void prepare(float sr) { sr_ = sr; l_.init((int)(sr * 0.05f)); r_.init((int)(sr * 0.05f)); }
    void reset() { l_.clear(); r_.clear(); ph_ = 0.0f; fbL_ = fbR_ = 0.0f; }
    // type 0 chorus, 1 flanger
    void process(float* L, float* R, int n, int type, float rateHz, float depth, float mix)
    {
        const float inc = rateHz / sr_;
        const float base = type == 0 ? 0.012f * sr_ : 0.0015f * sr_;
        const float mod = type == 0 ? 0.006f * sr_ * depth : 0.0013f * sr_ * depth;
        const float fb = type == 0 ? 0.0f : 0.55f + 0.3f * depth;
        for (int i = 0; i < n; ++i)
        {
            ph_ += inc; if (ph_ >= 1.0f) ph_ -= 1.0f;
            const float tl = ph_ < 0.5f ? 4.0f * ph_ - 1.0f : 3.0f - 4.0f * ph_;
            float p2 = ph_ + 0.25f; if (p2 >= 1.0f) p2 -= 1.0f;
            const float tr = p2 < 0.5f ? 4.0f * p2 - 1.0f : 3.0f - 4.0f * p2;
            const float dl = l_.read(base + mod * tl), dr = r_.read(base + mod * tr);
            l_.write(L[i] + fbL_ * fb); r_.write(R[i] + fbR_ * fb);
            fbL_ = dl; fbR_ = dr;
            L[i] += (dl - L[i]) * mix * (type == 0 ? 0.7f : 0.5f) + dl * mix * (type == 0 ? 0.0f : 0.5f);
            R[i] += (dr - R[i]) * mix * (type == 0 ? 0.7f : 0.5f) + dr * mix * (type == 0 ? 0.0f : 0.5f);
        }
    }
private:
    StereoDelayLine l_, r_;
    float sr_ = 44100.0f, ph_ = 0.0f, fbL_ = 0, fbR_ = 0;
};

class PingPongDelay
{
public:
    void prepare(float sr) { sr_ = sr; l_.init((int)(sr * 2.6f)); r_.init((int)(sr * 2.6f)); }
    void reset() { l_.clear(); r_.clear(); lpL_ = lpR_ = 0.0f; }
    void process(float* L, float* R, int n, float delaySamples, float feedback, float mix, float toneHz)
    {
        if (mix <= 0.0001f) { for (int i = 0; i < n; ++i) { l_.write(0.0f); r_.write(0.0f); } return; }
        const float a = 1.0f - std::exp(-6.2831853f * toneHz / sr_);
        for (int i = 0; i < n; ++i)
        {
            const float dl = l_.read(delaySamples), dr = r_.read(delaySamples);
            lpL_ += (dl - lpL_) * a; lpR_ += (dr - lpR_) * a;
            const float in = 0.5f * (L[i] + R[i]);
            // Ping-pong: input enters left, each repeat crosses over.
            l_.write(in + lpR_ * feedback);
            r_.write(lpL_ * feedback);
            L[i] += dl * mix; R[i] += dr * mix;
        }
    }
private:
    StereoDelayLine l_, r_;
    float sr_ = 44100.0f, lpL_ = 0, lpR_ = 0;
};

// Small Freeverb-style reverb: 4 damped combs + 2 allpasses per side.
class Reverb
{
public:
    void prepare(float sr)
    {
        const float s = sr / 44100.0f;
        static const int cl[4] = {1116, 1277, 1422, 1617}, ap[2] = {556, 341};
        for (int i = 0; i < 4; ++i) { cL_[i].init((int)(cl[i] * s), 1.0f); cR_[i].init((int)((cl[i] + 23) * s), 1.0f); }
        for (int i = 0; i < 2; ++i) { aL_[i].init((int)(ap[i] * s), 1.0f); aR_[i].init((int)((ap[i] + 23) * s), 1.0f); }
    }
    void reset() { for (auto& c : cL_) c.clear(); for (auto& c : cR_) c.clear(); for (auto& a : aL_) a.clear(); for (auto& a : aR_) a.clear(); }
    void process(float* L, float* R, int n, float size, float damp, float mix)
    {
        if (mix <= 0.0001f) return;
        const float fb = 0.70f + 0.28f * clampf(size, 0.0f, 1.0f);
        const float d = clampf(damp, 0.0f, 1.0f) * 0.6f;
        for (int i = 0; i < n; ++i)
        {
            const float in = (L[i] + R[i]) * 0.015f;
            float ol = 0.0f, orr = 0.0f;
            for (int c = 0; c < 4; ++c) { ol += cL_[c].comb(in, fb, d); orr += cR_[c].comb(in, fb, d); }
            for (int a = 0; a < 2; ++a) { ol = aL_[a].allpass(ol); orr = aR_[a].allpass(orr); }
            L[i] += ol * mix; R[i] += orr * mix;
        }
    }
private:
    struct Line
    {
        std::vector<float> b; int i = 0; float lp = 0.0f;
        void init(int n, float) { b.assign((size_t)(n < 8 ? 8 : n), 0.0f); i = 0; lp = 0.0f; }
        void clear() { std::fill(b.begin(), b.end(), 0.0f); lp = 0.0f; }
        float comb(float x, float fb, float damp)
        {
            const float y = b[(size_t)i];
            lp = y * (1.0f - damp) + lp * damp;
            b[(size_t)i] = x + lp * fb;
            if (++i >= (int)b.size()) i = 0;
            return y;
        }
        float allpass(float x)
        {
            const float y = b[(size_t)i];
            b[(size_t)i] = x + y * 0.5f;
            if (++i >= (int)b.size()) i = 0;
            return y - x;
        }
    };
    Line cL_[4], cR_[4], aL_[2], aR_[2];
};

} // namespace janus

#endif // JANUS_DSP_H
