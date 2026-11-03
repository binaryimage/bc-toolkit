#include "Mangle.h"
#include <random>
#include <cmath>

namespace tk::mangle
{
juce::AudioBuffer<float> stretch (const juce::AudioBuffer<float>& in, double sr, StretchOptions o, std::function<bool (float)> progress)
{
    int order = juce::jlimit (10, 17, (int) std::round (std::log2 (o.windowSeconds * sr)));
    const int W = 1 << order, half = W / 2;
    juce::dsp::FFT fft (order);
    std::vector<float> win ((size_t) W);
    for (int i = 0; i < W; ++i) win[(size_t) i] = (float) std::pow (1.0 - std::pow ((i - half) / (double) half, 2.0), 1.25); // paulstretch window
    const int chans = in.getNumChannels(), inLen = in.getNumSamples();
    const double hopOut = half, hopIn = hopOut / juce::jmax (1.0, o.factor);
    const int frames = (int) std::ceil (inLen / hopIn) + 1;
    juce::AudioBuffer<float> out (chans, (int) (frames * hopOut + W));
    out.clear();
    std::mt19937 rng (o.seed);
    std::uniform_real_distribution<float> ph (0.0f, juce::MathConstants<float>::twoPi);
    std::vector<float> buf ((size_t) W * 2);
    std::vector<float> phases ((size_t) half + 1);
    for (int f = 0; f < frames; ++f)
    {
        double pos = f * hopIn;
        for (auto& p : phases) p = ph (rng);              // same phases on both channels keeps the image stable
        for (int c = 0; c < chans; ++c)
        {
            const float* x = in.getReadPointer (c);
            for (int i = 0; i < W; ++i)
            {
                int k = (int) pos + i - half;
                buf[(size_t) i] = (k >= 0 && k < inLen) ? x[k] * win[(size_t) i] : 0.0f;
            }
            std::fill (buf.begin() + W, buf.end(), 0.0f);
            fft.performRealOnlyForwardTransform (buf.data(), true);
            for (int b = 0; b <= half; ++b)
            {
                float re = buf[(size_t) b * 2], im = buf[(size_t) b * 2 + 1];
                float mag = std::sqrt (re * re + im * im);
                buf[(size_t) b * 2] = mag * std::cos (phases[(size_t) b]);
                buf[(size_t) b * 2 + 1] = mag * std::sin (phases[(size_t) b]);
            }
            fft.performRealOnlyInverseTransform (buf.data());
            float* y = out.getWritePointer (c);
            int at = (int) (f * hopOut);
            for (int i = 0; i < W; ++i) y[at + i] += buf[(size_t) i] * win[(size_t) i];
        }
        if (progress && (f & 15) == 0 && ! progress ((float) f / frames)) break;
    }
    // trim the fade-in head and normalise to the input's peak
    float pin = 0, pout = 0;
    for (int c = 0; c < chans; ++c) { pin = std::max (pin, in.getMagnitude (c, 0, inLen)); pout = std::max (pout, out.getMagnitude (c, 0, out.getNumSamples())); }
    if (pout > 1e-6f) out.applyGain (juce::jmin (0.95f, pin) / pout);
    return out;
}

juce::AudioBuffer<float> tapeRot (const juce::AudioBuffer<float>& in, double sr, RotOptions o)
{
    const int chans = in.getNumChannels(), n = in.getNumSamples();
    const int maxDelay = (int) (sr * 0.05);
    juce::AudioBuffer<float> out (chans, n);
    std::mt19937 rng (o.seed);
    std::normal_distribution<float> gauss (0.0f, 1.0f);
    std::uniform_real_distribution<float> uni (0.0f, 1.0f);
    // shared modulation for both channels (tape moves as one)
    std::vector<float> delay ((size_t) n), gain ((size_t) n, 1.0f);
    double wowPh = 0, flPh = 0, driftV = 0, dropEnv = 1, dropTarget = 1;
    for (int i = 0; i < n; ++i)
    {
        wowPh += 0.55 / sr; flPh += 7.3 / sr;
        driftV += (gauss (rng) * 0.0004 - driftV * 0.00002);
        double d = 0.012 * sr + o.wow * 0.006 * sr * std::sin (wowPh * juce::MathConstants<double>::twoPi)
                 + o.flutter * 0.0006 * sr * std::sin (flPh * juce::MathConstants<double>::twoPi) + o.drift * 0.004 * sr * std::tanh (driftV * 50);
        delay[(size_t) i] = (float) juce::jlimit (2.0, maxDelay - 3.0, d);
        // dropouts: occasional quick dips in level and highs
        if (uni (rng) < o.dropouts * 2.0 / sr) dropTarget = 0.15 + 0.5 * uni (rng);
        dropEnv += (dropTarget - dropEnv) * 0.002;
        if (dropTarget < 1 && std::abs (dropEnv - dropTarget) < 0.01) dropTarget = 1;
        gain[(size_t) i] = (float) dropEnv;
    }
    double cutoff = 1500.0 * std::pow (12.0, o.tone);        // 1.5 kHz .. 18 kHz
    double a = std::exp (-juce::MathConstants<double>::twoPi * cutoff / sr);
    int holdN = o.crush > 0 ? 1 + (int) (o.crush * 6) : 1;
    double steps = o.crush > 0 ? std::pow (2.0, 16 - o.crush * 10) : 0;
    for (int c = 0; c < chans; ++c)
    {
        const float* x = in.getReadPointer (c);
        float* y = out.getWritePointer (c);
        std::vector<float> line ((size_t) maxDelay, 0.0f);
        int w = 0;
        double lp1 = 0, lp2 = 0, pink = 0, held = 0;
        for (int i = 0; i < n; ++i)
        {
            line[(size_t) w] = x[i];
            double rp = w - delay[(size_t) i];
            while (rp < 0) rp += maxDelay;
            int i1 = (int) rp; double t = rp - i1;
            auto at = [&] (int k) { return line[(size_t) ((k % maxDelay + maxDelay) % maxDelay)]; };
            double p0 = at (i1 - 1), p1 = at (i1), p2 = at (i1 + 1), p3 = at (i1 + 2);
            double v = p1 + 0.5 * t * (p2 - p0 + t * (2 * p0 - 5 * p1 + 4 * p2 - p3 + t * (3 * (p1 - p2) + p3 - p0)));
            w = (w + 1) % maxDelay;
            // saturation, then two-pole tone loss (dropouts also dull the sound)
            double drive = 1 + o.saturation * 4;
            v = std::tanh (v * drive) / std::tanh (drive);
            double aa = a + (1 - gain[(size_t) i]) * (1 - a) * 0.6;
            lp1 = v + (lp1 - v) * aa; lp2 = lp1 + (lp2 - lp1) * aa;
            v = lp2 * gain[(size_t) i];
            // hiss: lightly filtered noise
            pink = pink * 0.86 + gauss (rng) * 0.14;
            v += pink * o.hiss * 0.02;
            if (holdN > 1) { if (i % holdN == 0) held = v; v = held; }
            if (steps > 0) v = std::round (v * steps) / steps;
            y[i] = (float) v;
        }
    }
    return out;
}
}
