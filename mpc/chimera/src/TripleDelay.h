#pragma once
#include "DspCommon.h"

namespace chimera
{
enum class DelayType { Digital = 0, Tape, Analog, LoFi, Mod, Reverse, PingPong };

struct DelayUnitParams
{
    bool  on     = true;
    int   type   = 0;
    float timeMs = 375.0f;
    float fb     = 0.35f;
    float tone   = 0.6f;
    float mod    = 0.2f;
    float level  = 0.8f;
};

//==============================================================================
// One delay "engine" of the triple delay. Each type colours the repeats in
// the feedback path, so the character builds up with every repeat.
class DelayUnit
{
public:
    static constexpr float maxTimeMs = 3000.0f;

    void prepare (double sampleRate, uint32_t seed)
    {
        sr = (float) sampleRate;
        // Reverse needs a window of twice the delay time
        const int maxSamples = (int) (sr * (2.0f * maxTimeMs / 1000.0f + 0.25f));
        lineL.prepare (maxSamples);
        lineR.prepare (maxSamples);
        time.prepare (180.0f, sr);
        inputGain.prepare (15.0f, sr);
        outGain.prepare (20.0f, sr);
        time.snap (params.timeMs * 0.001f * sr);
        inputGain.snap (params.on ? 1.0f : 0.0f);
        outGain.snap (params.level);
        noise.state ^= seed;
        drift.state ^= seed * 7919u;
        updateFilters();
        reset();
    }

    void reset()
    {
        lineL.clear(); lineR.clear();
        lpL.reset(); lpR.reset(); hpL.reset(); hpR.reset(); driftLp.reset(); envL.reset(); envR.reset();
        holdL = holdR = 0.0f; holdCount = 0;
        revPhase = 0.0f;
    }

    void setParams (const DelayUnitParams& p)
    {
        const bool filterChange = p.type != params.type || std::abs (p.tone - params.tone) > 0.001f;
        params = p;
        params.timeMs = clampf (p.timeMs, 1.0f, maxTimeMs);
        time.target      = params.timeMs * 0.001f * sr;
        inputGain.target = params.on ? 1.0f : 0.0f;
        outGain.target   = params.level;
        // Tape glides slowly between times (pitchy), others more quickly
        time.prepare (params.type == (int) DelayType::Tape ? 350.0f : 120.0f, sr);
        if (filterChange) updateFilters();
    }

    void process (float inL, float inR, float& outL, float& outR)
    {
        const float d    = time.next();
        const float gin  = inputGain.next();
        const float gout = outGain.next();
        const auto  type = (DelayType) params.type;
        inL *= gin; inR *= gin;

        // ---- modulation --------------------------------------------------
        modPhase += 1.0f / sr;
        if (modPhase > 1000.0f) modPhase -= 1000.0f;
        float offL = 0.0f, offR = 0.0f;
        const float m = params.mod;
        switch (type)
        {
            case DelayType::Tape:
            {
                const float wobble = driftLp.lp (drift.next()) * 40.0f;   // slow random drift
                const float wowL = std::sin (kTwoPi * 0.55f * modPhase);
                const float flt  = std::sin (kTwoPi * 6.3f * modPhase);
                offL = m * sr * (0.0022f * wowL + 0.00035f * flt + 0.0008f * wobble);
                offR = m * sr * (0.0022f * std::sin (kTwoPi * 0.55f * modPhase + 1.3f) + 0.00035f * flt + 0.0008f * wobble);
                break;
            }
            case DelayType::Mod:
                offL = m * sr * 0.005f * std::sin (kTwoPi * 1.1f * modPhase);
                offR = m * sr * 0.005f * std::sin (kTwoPi * 1.1f * modPhase + 1.57f);
                break;
            case DelayType::Analog:
                offL = offR = m * sr * 0.0015f * std::sin (kTwoPi * 0.7f * modPhase);
                break;
            case DelayType::Reverse:
                break;
            default:
                offL = m * sr * 0.0006f * std::sin (kTwoPi * 0.8f * modPhase);
                offR = m * sr * 0.0006f * std::sin (kTwoPi * 0.8f * modPhase + 1.57f);
                break;
        }

        // ---- read --------------------------------------------------------
        float yL, yR;
        if (type == DelayType::Reverse)
        {
            revPhase += 1.0f / std::max (d, 64.0f);
            if (revPhase >= 1.0f) revPhase -= 1.0f;
            float p2 = revPhase + 0.5f; if (p2 >= 1.0f) p2 -= 1.0f;
            const float w1 = std::sin (kPi * revPhase); const float w2 = std::sin (kPi * p2);
            const float g1 = w1 * w1, g2 = w2 * w2;
            yL = g1 * lineL.read (2.0f + 2.0f * d * revPhase) + g2 * lineL.read (2.0f + 2.0f * d * p2);
            yR = g1 * lineR.read (2.0f + 2.0f * d * revPhase) + g2 * lineR.read (2.0f + 2.0f * d * p2);
        }
        else
        {
            yL = lineL.read (d + offL);
            yR = lineR.read (d + offR);
        }

        // ---- colour the repeats & write --------------------------------
        const float fbL = colour (yL, lpL, hpL, holdL, envL);
        const float fbR = colour (yR, lpR, hpR, holdR, envR);
        if (++holdCount >= 4) holdCount = 0;

        if (type == DelayType::PingPong)
        {
            const float g = std::sqrt (params.fb);
            lineL.push (undenormal (0.5f * (inL + inR) + g * fbR));
            lineR.push (undenormal (g * fbL));
        }
        else
        {
            lineL.push (undenormal (inL + params.fb * fbL));
            lineR.push (undenormal (inR + params.fb * fbR));
        }

        // Output the coloured signal so the first repeat already has the character
        outL = fbL * gout;
        outR = fbR * gout;
    }

private:
    float colour (float x, OnePole& lp, OnePole& hp, float& hold, OnePole& env)
    {
        switch ((DelayType) params.type)
        {
            case DelayType::Tape:
                x = softClip (x * 1.4f) / 1.4f;
                break;
            case DelayType::Analog:
            {
                // BBD grit that rides on the repeats (like a compander's breathing):
                // ~40 dB under the signal and gone in silence, so it never hisses on its own.
                const float level = env.lp (std::abs (x));
                x = softClip (x * 1.7f) / 1.7f + noise.next() * 0.01f * level;
                break;
            }
            case DelayType::LoFi:
                if (holdCount == 0) hold = std::round (x * 96.0f) / 96.0f;   // ~7-bit, 1/4 sample rate
                x = hold;
                break;
            default:
                break;
        }
        x = hp.hp (lp.lp (x));
        return softClip (x);
    }

    void updateFilters()
    {
        const float t = params.tone;
        float lpHz = 1500.0f * std::pow (13.0f, t), hpHz = 35.0f;
        switch ((DelayType) params.type)
        {
            case DelayType::Tape:   lpHz = 1100.0f * std::pow (8.0f, t);  hpHz = 90.0f;  break;
            case DelayType::Analog: lpHz = 800.0f  * std::pow (6.0f, t);  hpHz = 60.0f;  break;
            case DelayType::LoFi:   lpHz = 700.0f  * std::pow (5.0f, t);  hpHz = 380.0f; break;
            default: break;
        }
        lpL.setCutoff (lpHz, sr); lpR.setCutoff (lpHz, sr);
        hpL.setCutoff (hpHz, sr); hpR.setCutoff (hpHz, sr);
        driftLp.setCutoff (0.8f, sr);
        envL.setCutoff (25.0f, sr); envR.setCutoff (25.0f, sr);
    }

    float sr = 44100.0f, modPhase = 0.0f, revPhase = 0.0f, holdL = 0.0f, holdR = 0.0f;
    int holdCount = 0;
    DelayUnitParams params;
    DelayLine lineL, lineR;
    OnePole lpL, lpR, hpL, hpR, driftLp, envL, envR;
    Smooth time, inputGain, outGain;
    Noise noise, drift;
};

//==============================================================================
// Three delay units, run in series (A -> B -> C, each adding its repeats)
// or in parallel (all fed from the input). Output is wet only.
class TripleDelay
{
public:
    void prepare (double sampleRate)
    {
        for (int i = 0; i < 3; ++i) units[i].prepare (sampleRate, 0x9E3779B9u * (uint32_t) (i + 1));
    }
    void reset()                                  { for (auto& u : units) u.reset(); }
    void setUnit (int i, const DelayUnitParams& p) { units[i].setParams (p); }
    void setSerial (bool s)                       { serial = s; }

    void process (float inL, float inR, float& outL, float& outR)
    {
        outL = outR = 0.0f;
        float sL = inL, sR = inR;
        for (auto& u : units)
        {
            float wL, wR;
            if (serial) u.process (sL, sR, wL, wR);
            else        u.process (inL, inR, wL, wR);
            outL += wL; outR += wR;
            if (serial) { sL += wL; sR += wR; }
        }
    }

private:
    DelayUnit units[3];
    bool serial = false;
};
} // namespace chimera
