#pragma once
#include "DspCommon.h"
#include <array>

namespace chimera
{
enum class ReverbType { Room = 0, Hall, Plate, Spring, Shimmer, Cloud, Bloom, Swell };

struct ReverbParams
{
    int   type      = 1;
    float decay     = 3.5f;   // seconds (RT60)
    float predelay  = 20.0f;  // ms
    float size      = 0.6f;   // 0..1
    float tone      = 0.6f;   // 0..1 dark -> bright
    float lowEnd    = 0.0f;   // -1..1 (shorter .. longer low decay)
    float mod       = 0.4f;   // 0..1
    float special   = 0.5f;   // 0..1, meaning depends on type
    bool  freeze    = false;
    bool  on        = true;
};

//==============================================================================
// 8-line feedback delay network reverb with per-type front ends:
//   Room / Hall / Swell : early reflections + FDN (Swell adds auto volume swell)
//   Plate               : dense diffusion, bright, no early reflections
//   Spring              : three dispersive "springs" (chirpy all-pass chains)
//   Shimmer             : octave-up pitch shifter fed back into the tank
//   Cloud               : very long diffusion + heavy modulation
//   Bloom               : long all-pass chain that makes the tail swell in
class MultiReverb
{
public:
    static constexpr int N = 8;

    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        auto ms = [this] (float m) { return (int) (m * 0.001f * sr) + 8; };

        preL.prepare (ms (650.0f)); preR.prepare (ms (650.0f));
        for (int i = 0; i < kDiffStages; ++i) { diffL[i].prepare (ms (130.0f)); diffR[i].prepare (ms (130.0f)); }
        for (int i = 0; i < 4; ++i)            { bloomL[i].prepare (ms (180.0f)); bloomR[i].prepare (ms (180.0f)); }
        for (int i = 0; i < N; ++i)            { lines[i].prepare (ms (320.0f)); lenSmooth[i].prepare (250.0f, sr); }
        for (auto& s : springs)                 s.line.prepare (ms (60.0f));
        shifter.prepare (ms (120.0f));

        preSmooth.prepare (150.0f, sr);
        inGain.prepare (20.0f, sr);
        swellEnvFast.setCutoff (60.0f, sr);
        swellEnvSlow.setCutoff (4.0f, sr);
        shimHp.setCutoff (350.0f, sr);
        shimLp.setCutoff (7000.0f, sr);
        shimEnv.setCutoff (3.0f, sr);
        for (int i = 0; i < N; ++i) lowSplit[i].setCutoff (250.0f, sr);
        for (auto& s : springs) { s.hp.setCutoff (140.0f, sr); }

        trimSmooth.prepare (40.0f, sr);
        configure (params, true);
        trimSmooth.snap (typeTrim);
        reset();
    }

    void reset()
    {
        preL.clear(); preR.clear();
        for (auto& d : diffL) d.clear();
        for (auto& d : diffR) d.clear();
        for (auto& d : bloomL) d.clear();
        for (auto& d : bloomR) d.clear();
        for (int i = 0; i < N; ++i) { lines[i].clear(); damp[i].reset(); lowSplit[i].reset(); }
        for (auto& s : springs) { s.line.clear(); s.lp.reset(); s.hp.reset(); for (auto& a : s.ap) a.x1 = a.y1 = 0.0f; }
        shifter.clear();
        lastOutL = lastOutR = 0.0f;
        swellGain = 0.0f;
    }

    void setParams (const ReverbParams& p) { configure (p, false); }

    void process (float inL, float inR, float& outL, float& outR)
    {
        const auto type = (ReverbType) params.type;
        const float gin = inGain.next();
        inL *= gin; inR *= gin;

        // ---- swell: auto volume swell on each new note --------------------
        if (type == ReverbType::Swell)
        {
            const float lvl  = std::abs (inL) + std::abs (inR);
            const float fast = swellEnvFast.lp (lvl);
            const float slow = swellEnvSlow.lp (lvl);
            if (swellHold > 0) --swellHold;
            if (fast < 0.0015f)
                swellGain *= 0.995f;                         // silence: duck, ready for the next note
            else if (fast > 3.0f * slow + 0.002f && swellHold == 0)
            {
                swellGain = 0.0f;                            // new attack while playing: restart the swell
                swellHold = (int) (0.12f * sr);
            }
            else
                swellGain = std::min (1.0f, swellGain + swellRate);
            inL *= swellGain; inR *= swellGain;
        }

        // ---- shimmer feedback (octave up of the previous output) ----------
        if (type == ReverbType::Shimmer)
        {
            float s = shimmerShift (0.5f * (lastOutL + lastOutR));
            s = shimLp.lp (shimHp.hp (s));
            const float env = shimEnv.lp (std::abs (s));
            const float limit = 1.0f / std::max (1.0f, env / 0.12f);
            s *= shimmerAmount * limit;
            inL += s; inR += s;
        }

        // ---- pre-delay & early reflections -------------------------------
        preL.push (inL); preR.push (inR);
        const float pd = preSmooth.next();
        float xL = preL.read (pd), xR = preR.read (pd);

        float erL = 0.0f, erR = 0.0f;
        if (erLevel > 0.0f)
        {
            for (int t = 0; t < 6; ++t)
            {
                erL += erGains[t] * preL.read (pd + erTapL[t]);
                erR += erGains[t] * preR.read (pd + erTapR[t]);
            }
            erL *= erLevel; erR *= erLevel;
        }

        // ---- spring tank (replaces the diffusion front end) --------------
        float springL = 0.0f, springR = 0.0f;
        if (type == ReverbType::Spring)
        {
            const float x = softClip (0.5f * (xL + xR) * springDrive) * springMakeup;
            float o[3];
            for (int s = 0; s < 3; ++s)
            {
                auto& sp = springs[s];
                float y = sp.line.read (sp.len);
                for (auto& a : sp.ap) y = a.process (y);
                y = sp.hp.hp (sp.lp.lp (y));
                sp.line.push (undenormal (x + sp.g * y));
                o[s] = y;
            }
            springL = 0.8f * (o[0] + 0.6f * o[1]);
            springR = 0.8f * (o[2] + 0.6f * o[1]);
            xL = springL * 0.35f; xR = springR * 0.35f;
        }

        // ---- input diffusion ---------------------------------------------
        for (int i = 0; i < diffStages; ++i)
        {
            xL = diffL[i].process (xL);
            xR = diffR[i].process (xR);
        }
        if (type == ReverbType::Bloom)
            for (int i = 0; i < 4; ++i) { xL = bloomL[i].process (xL); xR = bloomR[i].process (xR); }

        // ---- FDN -----------------------------------------------------------
        std::array<float, N> s;
        modPhase += 1.0f / sr;
        if (modPhase > 1000.0f) modPhase -= 1000.0f;
        for (int i = 0; i < N; ++i)
        {
            const float len = lenSmooth[i].next();
            const float mod = modDepth * std::sin (kTwoPi * modRate * (1.0f + 0.137f * (float) i) * modPhase + (float) i * 0.79f);
            s[i] = lines[i].read (len + mod);
        }

        float tankL = s[0] + s[1] - s[2] - s[3] + s[4] + s[5] - s[6] - s[7];
        float tankR = s[0] - s[1] + s[2] - s[3] + s[4] - s[5] + s[6] - s[7];

        for (int i = 0; i < N; ++i)
        {
            if (params.freeze)
                continue;                                   // lossless: hold forever
            const float lo = lowSplit[i].lp (s[i]);
            const float v  = lo * gLow[i] + (s[i] - lo) * gHigh[i];
            s[i] = damp[i].lp (v);
        }

        hadamard (s);
        for (int i = 0; i < N; ++i)
        {
            const float in = (i & 1) ? xR : xL;
            const float sign = (i & 2) ? -1.0f : 1.0f;
            lines[i].push (undenormal (s[i] + sign * in));
        }

        outL = tankL * outScale + erL + springL * springOut;
        outR = tankR * outScale + erR + springR * springOut;
        lastOutL = outL; lastOutR = outR;
        const float trim = trimSmooth.next();
        outL *= trim; outR *= trim;
    }

private:
    static constexpr int kDiffStages = 6;

    struct TypeCfg { float size, diffG; int stages; float diffScale, modMs, modRate, damp, er, gain; };
    static const TypeCfg& cfgFor (int t)
    {
        static const TypeCfg cfgs[8] = {
            // size  diffG st  dScale modMs rate  damp  er    gain
            { 0.35f, 0.60f, 4, 0.60f, 0.12f, 0.60f, 1.00f, 0.70f, 1.00f },  // Room
            { 1.00f, 0.65f, 4, 1.00f, 0.30f, 0.45f, 0.90f, 0.35f, 0.80f },  // Hall
            { 0.55f, 0.75f, 6, 0.80f, 0.18f, 0.80f, 1.30f, 0.00f, 0.85f },  // Plate
            { 0.30f, 0.50f, 2, 0.50f, 0.10f, 0.50f, 0.70f, 0.00f, 0.70f },  // Spring
            { 1.15f, 0.70f, 4, 1.00f, 0.35f, 0.40f, 1.00f, 0.00f, 0.75f },  // Shimmer
            { 1.60f, 0.72f, 6, 2.60f, 0.90f, 0.25f, 0.65f, 0.00f, 0.70f },  // Cloud
            { 1.20f, 0.70f, 4, 1.20f, 0.45f, 0.35f, 0.85f, 0.00f, 0.75f },  // Bloom
            { 1.00f, 0.65f, 4, 1.00f, 0.30f, 0.45f, 0.90f, 0.20f, 0.80f },  // Swell
        };
        return cfgs[t < 0 ? 0 : (t > 7 ? 7 : t)];
    }

    void configure (const ReverbParams& p, bool snap)
    {
        params = p;
        const auto& c = cfgFor (p.type);
        const auto type = (ReverbType) p.type;
        const float msToS = 0.001f * sr;
        const float sizeScale = c.size * (0.5f + p.size);

        // pre-delay, input gain (freeze or bypass mute the input -> trails ring out)
        preSmooth.target = std::max (2.0f, p.predelay * msToS);
        inGain.target = (p.on && ! p.freeze) ? 1.0f : 0.0f;
        if (snap) { preSmooth.snap (preSmooth.target); inGain.snap (inGain.target); }

        // tank delay lengths and decay gains
        static const float baseMs[N] = { 29.7f, 37.1f, 41.1f, 43.7f, 53.0f, 59.9f, 67.7f, 79.3f };
        const float lowMul = std::pow (2.0f, p.lowEnd);
        const float t60 = std::max (0.1f, p.decay);
        for (int i = 0; i < N; ++i)
        {
            const float len = baseMs[i] * sizeScale * msToS;
            lenSmooth[i].target = len;
            if (snap) lenSmooth[i].snap (len);
            gHigh[i] = std::pow (10.0f, -3.0f * len / (t60 * sr));
            gLow[i]  = std::min (0.99995f, std::pow (10.0f, -3.0f * len / (t60 * lowMul * sr)));
        }

        // damping (tone)
        const float dampHz = clampf (1400.0f * std::pow (13.0f, p.tone) * c.damp, 500.0f, 0.45f * sr);
        for (auto& d : damp) d.setCutoff (dampHz, sr);

        // diffusion
        static const float diffMs[kDiffStages] = { 4.77f, 3.59f, 12.73f, 9.31f, 15.70f, 7.93f };
        float diffG = c.diffG;
        if (type == ReverbType::Plate) diffG = 0.55f + 0.27f * p.special;
        if (type == ReverbType::Cloud) diffG = 0.55f + 0.25f * p.special;
        diffStages = c.stages;
        for (int i = 0; i < kDiffStages; ++i)
        {
            diffL[i].delaySamples = diffMs[i] * c.diffScale * msToS;
            diffR[i].delaySamples = diffMs[i] * c.diffScale * 1.071f * msToS;
            diffL[i].g = diffR[i].g = diffG;
        }

        // bloom
        static const float bloomMs[4] = { 43.0f, 61.0f, 79.0f, 101.0f };
        for (int i = 0; i < 4; ++i)
        {
            const float scale = 0.7f + 0.6f * p.special;
            bloomL[i].delaySamples = bloomMs[i] * scale * msToS;
            bloomR[i].delaySamples = bloomMs[i] * scale * 1.09f * msToS;
            bloomL[i].g = bloomR[i].g = 0.3f + 0.45f * p.special;
        }

        // modulation
        modDepth = p.freeze ? 0.0f : p.mod * c.modMs * 2.0f * msToS;
        modRate  = c.modRate;

        // early reflections
        erLevel = 0.0f;
        if (c.er > 0.0f && ! p.freeze)
            erLevel = c.er * ((type == ReverbType::Room || type == ReverbType::Hall) ? 2.0f * p.special : 1.0f);
        static const float tapL[6] = { 7.1f, 11.3f, 17.9f, 23.5f, 31.7f, 41.3f };
        static const float tapR[6] = { 8.3f, 13.1f, 19.7f, 27.1f, 35.3f, 45.9f };
        static const float gains[6] = { 0.55f, -0.45f, 0.38f, -0.31f, 0.24f, -0.19f };
        for (int t = 0; t < 6; ++t)
        {
            erTapL[t] = tapL[t] * std::min (sizeScale, 1.6f) * msToS;
            erTapR[t] = tapR[t] * std::min (sizeScale, 1.6f) * msToS;
            erGains[t] = gains[t];
        }

        // springs
        static const float springMs[3] = { 33.1f, 41.7f, 47.3f };
        const float springT60 = std::min (t60, 8.0f);
        for (int s = 0; s < 3; ++s)
        {
            auto& sp = springs[s];
            sp.len = springMs[s] * (0.85f + 0.3f * p.size) * msToS;
            sp.g   = p.freeze ? 0.999f : std::pow (10.0f, -3.0f * sp.len / (springT60 * sr));
            sp.lp.setCutoff (clampf (1800.0f * std::pow (3.5f, p.tone), 800.0f, 9000.0f), sr);
            for (auto& a : sp.ap) a.a = 0.58f + 0.03f * (float) s;
        }
        springDrive  = 1.0f + 7.0f * p.special;
        springMakeup = 1.4f / (1.0f + 0.45f * springDrive);
        springOut    = type == ReverbType::Spring ? 0.55f : 0.0f;

        // shimmer & swell
        shimmerAmount = 0.15f + 0.5f * p.special;
        swellRate = 1.0f / ((0.05f + 2.45f * p.special) * sr);

        // per-type loudness trim so switching types keeps a similar level
        static const float trimDb[8] = { -7.0f, -2.0f, -4.8f, -4.0f, -1.6f, 0.7f, -0.3f, 4.0f };
        typeTrim = std::pow (10.0f, trimDb[p.type < 0 ? 0 : (p.type > 7 ? 7 : p.type)] / 20.0f);
        outScale = 0.30f * c.gain;
        trimSmooth.target = typeTrim;
    }

    // In-place fast Walsh-Hadamard transform (orthonormal)
    static void hadamard (std::array<float, N>& x)
    {
        for (int h = 1; h < N; h <<= 1)
            for (int i = 0; i < N; i += h << 1)
                for (int j = i; j < i + h; ++j)
                {
                    const float a = x[(size_t) j], b = x[(size_t) (j + h)];
                    x[(size_t) j] = a + b; x[(size_t) (j + h)] = a - b;
                }
        const float norm = 0.35355339f; // 1/sqrt(8)
        for (auto& v : x) v *= norm;
    }

    // Granular octave-up shifter: two crossfaded read heads
    float shimmerShift (float x)
    {
        shifter.push (x);
        const float window = 0.08f * sr;
        shiftPhase -= 1.0f / window;              // ratio 2 => read head moves 1 extra sample per sample
        if (shiftPhase < 0.0f) shiftPhase += 1.0f;
        float p2 = shiftPhase + 0.5f; if (p2 >= 1.0f) p2 -= 1.0f;
        const float w1 = std::sin (kPi * shiftPhase), w2 = std::sin (kPi * p2);
        return w1 * w1 * shifter.read (2.0f + window * shiftPhase) + w2 * w2 * shifter.read (2.0f + window * p2);
    }

    struct AP1
    {
        float a = 0.6f, x1 = 0.0f, y1 = 0.0f;
        float process (float x) { const float y = -a * x + x1 + a * y1; x1 = x; y1 = undenormal (y); return y; }
    };
    struct Spring
    {
        DelayLine line;
        std::array<AP1, 12> ap;
        OnePole lp, hp;
        float len = 1000.0f, g = 0.5f;
    };

    float sr = 44100.0f;
    ReverbParams params;

    DelayLine preL, preR, shifter;
    AllPass diffL[kDiffStages], diffR[kDiffStages], bloomL[4], bloomR[4];
    DelayLine lines[N];
    Smooth lenSmooth[N], preSmooth, inGain;
    OnePole damp[N], lowSplit[N];
    float gHigh[N] {}, gLow[N] {};
    int diffStages = 4;

    float modDepth = 0.0f, modRate = 0.5f, modPhase = 0.0f;
    float erLevel = 0.0f, erTapL[6] {}, erTapR[6] {}, erGains[6] {};
    std::array<Spring, 3> springs;
    float springDrive = 1.0f, springMakeup = 1.0f, springOut = 0.0f;

    float shimmerAmount = 0.4f, shiftPhase = 0.0f;
    OnePole shimHp, shimLp, shimEnv;

    float swellRate = 0.0001f, swellGain = 0.0f;
    int swellHold = 0;
    OnePole swellEnvFast, swellEnvSlow;

    float outScale = 0.3f, lastOutL = 0.0f, lastOutR = 0.0f, typeTrim = 1.0f;
    Smooth trimSmooth;
};
} // namespace chimera
