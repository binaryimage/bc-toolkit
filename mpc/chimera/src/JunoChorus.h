#pragma once
#include "DspCommon.h"

namespace chimera
{
// Juno-60 style stereo BBD chorus.
// Mono-summed input feeds one bucket-brigade line read by two taps whose
// triangle LFOs run in opposite phase (left vs right). Modes follow the
// original: I = slow, II = faster, I+II = fast & shallow vibrato-chorus.
// Output is wet only; the mixer supplies the dry signal (Juno = 50/50).
class JunoChorus
{
public:
    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        line.prepare ((int) (0.02f * sr));
        centre.prepare (60.0f, sr);
        depth.prepare (60.0f, sr);
        rate.prepare (60.0f, sr);
        centre.snap (3.5f); depth.snap (1.85f); rate.snap (0.513f);
        reset();
        updateFilters();
    }

    void reset()
    {
        line.clear();
        preL.reset(); postL.reset(); postR.reset(); hissFilter.reset();
        phase = 0.0f;
    }

    void setParams (int mode, float rateScale, float depthScale, float widthAmount, float toneHz, float hissAmount)
    {
        // Juno-60 timings (ms) and LFO rates (Hz)
        static const float modeRate[3]   = { 0.513f, 0.863f, 9.75f };
        static const float modeCentre[3] = { 3.50f,  3.50f,  3.30f };
        static const float modeDepth[3]  = { 1.85f,  1.85f,  0.30f };
        mode = mode < 0 ? 0 : (mode > 2 ? 2 : mode);

        rate.target   = modeRate[mode] * rateScale;
        centre.target = modeCentre[mode];
        depth.target  = modeDepth[mode] * depthScale;
        width = widthAmount;
        hiss  = hissAmount;
        if (std::abs (toneHz - tone) > 1.0f) { tone = toneHz; updateFilters(); }
    }

    // onGain: the chorus on/off fade (0..1). The hiss follows it, so a switched-off
    // chorus is silent.
    void process (float inL, float inR, float& outL, float& outR, float onGain = 1.0f)
    {
        const float r = rate.next(), c = centre.next(), d = depth.next();

        phase += r / sr;
        if (phase >= 1.0f) phase -= 1.0f;
        const float tri = 4.0f * std::abs (phase - 0.5f) - 1.0f;   // -1..1

        // Input anti-alias filter of the BBD, then into the bucket line
        const float mono = preL.process (0.5f * (inL + inR));
        line.push (mono);

        const float lfoL = tri;
        const float lfoR = lerp (tri, -tri, width);                 // width 1 = classic inverted right channel
        const float msToSamples = 0.001f * sr;
        float wl = line.read (std::max (0.6f, c + d * lfoL) * msToSamples);
        float wr = line.read (std::max (0.6f, c + d * lfoR) * msToSamples);

        // Compander-style gentle saturation + output reconstruction filter
        wl = postL.process (softClip (wl * 1.15f) * 0.87f);
        wr = postR.process (softClip (wr * 1.15f) * 0.87f);

        if (hiss > 0.0f && onGain > 0.0f)
        {
            // Juno-style BBD hiss, deliberately subtle: about -66 dBFS at 100%
            const float h = hissFilter.process (noise.next()) * hiss * onGain * 0.0015f;
            wl += h;
            wr += h * 0.9f;
        }
        outL = wl;
        outR = wr;
    }

    float getLfo() const { return 4.0f * std::abs (phase - 0.5f) - 1.0f; }

private:
    void updateFilters()
    {
        preL.setCutoff (tone * 1.3f, sr);
        postL.setCutoff (tone, sr);
        postR.setCutoff (tone, sr);
        hissFilter.setCutoff (tone * 0.8f, sr);
    }

    float sr = 44100.0f, phase = 0.0f, width = 1.0f, hiss = 0.0f, tone = 9000.0f;
    DelayLine line;
    TwoPole preL, postL, postR, hissFilter;
    Smooth centre, depth, rate;
    Noise noise;
};
} // namespace chimera
