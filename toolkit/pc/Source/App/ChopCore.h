#pragma once
#include "Common/JuceHeader.h"
#include "Common/Dsp.h"
#include <map>
#include <random>
#include <tuple>

/** Chop station logic, independent of the UI so it can be tested. */
namespace tk::chop
{
    constexpr int kMaxSlices = 64;              // four banks of 16 pads (A-D)

    /** How one pad sounds: tune, level and direction. */
    struct SliceFx
    {
        double semitones = 0;
        float gainDb = 0;
        bool reverse = false;
        bool operator== (const SliceFx& o) const { return semitones == o.semitones && gainDb == o.gainDb && reverse == o.reverse; }
    };

    /** A rough read of what a slice is, shown on its pad and used to build flips. */
    enum class Hit { kick, snare, hat, tone, other };

    inline juce::String letter (Hit h)
    {
        switch (h) { case Hit::kick: return "K"; case Hit::snare: return "S"; case Hit::hat: return "H"; case Hit::tone: return "T"; default: return "-"; }
    }
    inline juce::String hitName (Hit h)
    {
        switch (h) { case Hit::kick: return "kick"; case Hit::snare: return "snare"; case Hit::hat: return "hat"; case Hit::tone: return "tone"; default: return "texture"; }
    }

    // ------------------------------------------------------------ markers

    /** Sorted, no duplicates, inside the sample, at least minGap apart, at most kMaxSlices. */
    inline std::vector<int> tidy (std::vector<int> m, int n, int minGap = 64)
    {
        std::vector<int> out;
        if (n <= 1) return out;
        for (auto& x : m) x = juce::jlimit (0, n - 2, x);
        std::sort (m.begin(), m.end());
        for (int x : m) if (out.empty() || x - out.back() >= minGap) out.push_back (x);
        if ((int) out.size() > kMaxSlices) out.resize ((size_t) kMaxSlices);
        return out;
    }

    /** Start and end of slice i (the end is the next marker, or the end of the sample). */
    inline std::pair<int, int> range (const std::vector<int>& m, int i, int n)
    {
        if (i < 0 || i >= (int) m.size()) return { 0, 0 };
        int a = juce::jlimit (0, n, m[(size_t) i]);
        int b = i + 1 < (int) m.size() ? juce::jlimit (a, n, m[(size_t) i + 1]) : n;
        return { a, b };
    }

    inline std::vector<int> equal (int n, int count)
    {
        std::vector<int> s;
        count = juce::jlimit (1, kMaxSlices, count);
        for (int i = 0; i < count; ++i) s.push_back ((int) ((juce::int64) n * i / count));
        return tidy (s, n, 1);
    }

    /** A marker every `beats` beats at bpm, starting from `offset` (usually the first hit). */
    inline std::vector<int> grid (int n, double sr, double bpm, double beats, int offset)
    {
        std::vector<int> s;
        if (bpm <= 0 || beats <= 0) return s;
        const double step = beats * 60.0 / bpm * sr;
        if (step < 32) return s;
        for (double p = juce::jmax (0, offset); p < n - 32 && (int) s.size() < kMaxSlices; p += step) s.push_back ((int) std::lround (p));
        return tidy (s, n, 1);
    }

    /** Nearest point where the (summed) waveform crosses zero going up, within radius samples. */
    inline int nearestZeroCrossing (const juce::AudioBuffer<float>& b, int at, int radius)
    {
        const int n = b.getNumSamples();
        auto v = [&] (int i) { float x = 0; for (int c = 0; c < b.getNumChannels(); ++c) x += b.getSample (c, i); return x; };
        for (int d = 0; d <= radius; ++d)
            for (int i : { at - d, at + d })
                if (i > 0 && i < n && v (i - 1) <= 0 && v (i) > 0) return i;
        return at;
    }

    inline std::vector<int> snapToZero (const juce::AudioBuffer<float>& b, std::vector<int> m, int radius)
    {
        for (auto& x : m) x = nearestZeroCrossing (b, x, radius);
        return tidy (m, b.getNumSamples(), 16);
    }

    // ------------------------------------------------------------ what's in a slice

    inline Hit classify (const juce::AudioBuffer<float>& b, double sr, int a, int e)
    {
        const int len = juce::jmin (e - a, (int) (0.12 * sr));
        if (len < 32) return Hit::other;
        // one-pole splits: below 150 Hz, 150 Hz..2 kHz, above 5 kHz
        auto coef = [sr] (double hz) { return (float) (1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * hz / sr)); };
        const float c150 = coef (150), c2k = coef (2000), c5k = coef (5000);
        float l150 = 0, l2k = 0, l5k = 0;
        double eAll = 0, eLow = 0, eMid = 0, eHigh = 0;
        std::vector<float> mono ((size_t) len);
        for (int i = 0; i < len; ++i)
        {
            float x = 0;
            for (int c = 0; c < b.getNumChannels(); ++c) x += b.getSample (c, a + i);
            x /= (float) juce::jmax (1, b.getNumChannels());
            mono[(size_t) i] = x;
            l150 += c150 * (x - l150); l2k += c2k * (x - l2k); l5k += c5k * (x - l5k);
            eAll += x * x; eLow += l150 * l150;
            float mid = l2k - l150, high = x - l5k;
            eMid += mid * mid; eHigh += high * high;
        }
        if (eAll / len < 1e-7) return Hit::other;
        const double low = eLow / eAll, midR = eMid / eAll, high = eHigh / eAll;
        if (low > 0.45) return Hit::kick;
        if (high > 0.35 && midR < 0.2) return Hit::hat;
        // a clear pitch with little hiss and some length: a note, a stab, a bass
        if (high < 0.08 && len >= 2048 && (e - a) > 0.15 * sr && yinPitch (mono.data(), juce::jmin (len, 4096), sr, 0.1) > 0) return Hit::tone;
        if (midR >= 0.2 || high > 0.15) return Hit::snare;
        return Hit::other;
    }

    /** The slice as the pad plays it: tuned (by resampling, like the MPC), reversed, levelled, with fades. */
    inline juce::AudioBuffer<float> render (const juce::AudioBuffer<float>& src, double sr, int a, int e, const SliceFx& fx,
                                            bool normalize, bool fades)
    {
        a = juce::jlimit (0, src.getNumSamples(), a); e = juce::jlimit (a, src.getNumSamples(), e);
        const int len = e - a, chans = juce::jmax (1, src.getNumChannels());
        if (len < 2) return juce::AudioBuffer<float> (chans, 0);
        const double ratio = std::pow (2.0, fx.semitones / 12.0);
        const int outLen = juce::jmax (1, (int) std::floor (len / ratio));
        juce::AudioBuffer<float> out (chans, outLen);
        for (int c = 0; c < chans; ++c)
        {
            const float* in = src.getReadPointer (c, a);
            if (std::abs (ratio - 1.0) < 1e-9) out.copyFrom (c, 0, in, juce::jmin (len, outLen));
            else
            {
                std::vector<float> padded ((size_t) len + 8, 0.0f);
                std::copy (in, in + len, padded.begin());
                juce::LagrangeInterpolator li;
                li.process (ratio, padded.data(), out.getWritePointer (c), outLen);
            }
            if (fx.reverse) std::reverse (out.getWritePointer (c), out.getWritePointer (c) + outLen);
        }
        if (normalize) { float pk = peakOf (out); if (pk > 1e-4f) out.applyGain (0.95f / pk); }
        out.applyGain (juce::Decibels::decibelsToGain (fx.gainDb));
        if (fades)
        {
            int fi = juce::jmin (outLen / 4, (int) (0.0007 * sr)), fo = juce::jmin (outLen / 4, (int) (0.004 * sr));
            if (fi > 0) out.applyGainRamp (0, fi, 0, 1);
            if (fo > 0) out.applyGainRamp (outLen - fo, fo, 1, 0);
        }
        return out;
    }

    // ------------------------------------------------------------ flips

    /** One hit in a flip: which pad, when (beats), how long, how hard, and any extra tune/reverse on top of the pad's own. */
    struct Step { int slice = 0; double beat = 0, length = 0.25; int velocity = 110; SliceFx fx; };

    inline juce::StringArray flipStyles()
    {
        return { "Original groove", "Boom bap", "Resequence", "Stutter", "Half-time", "Dragged (dark)" };
    }

    inline juce::StringArray flipBlurbs()
    {
        return { "The hits where they were in the sample, cut to the bar length.",
                 "Kicks and snares on a dusty boom bap grid, hats on the eighths. Uses what each pad sounds like.",
                 "Every pad back on a 16th grid in a new order, first hit kept on the one.",
                 "The original order with 32nd-note rolls and repeats thrown in.",
                 "Kick on the one, snare on the three, room to breathe.",
                 "The original order pitched down 3 to 7 semitones, some hits reversed, slowed onto the eighths." };
    }

    /** Builds a flip. markers/types/n/sr describe the chopped sample; bars is 1, 2 or 4. */
    inline std::vector<Step> makeFlip (int style, const std::vector<int>& markers, int n, double sr, double bpm,
                                       const std::vector<Hit>& types, int bars, int seed)
    {
        std::vector<Step> out;
        const int count = (int) markers.size();
        if (count == 0 || bpm <= 0) return out;
        const double total = bars * 4.0;
        std::mt19937 rng ((unsigned) seed * 2654435761u + 17u);
        auto chance = [&rng] (double p) { return std::uniform_real_distribution<double> (0, 1) (rng) < p; };
        auto pick = [&rng] (const std::vector<int>& v) { return v[(size_t) std::uniform_int_distribution<int> (0, (int) v.size() - 1) (rng)]; };
        auto pool = [&] (Hit h) { std::vector<int> v; for (int i = 0; i < count && i < (int) types.size(); ++i) if (types[(size_t) i] == h) v.push_back (i); return v; };
        auto beatOf = [&] (int sample) { return (sample - markers[0]) / sr * bpm / 60.0; };
        auto lenOf = [&] (int i) { auto r = range (markers, i, n); return (r.second - r.first) / sr * bpm / 60.0; };
        auto all = [&] { std::vector<int> v; for (int i = 0; i < count; ++i) v.push_back (i); return v; };

        auto kicks = pool (Hit::kick), snares = pool (Hit::snare), hats = pool (Hit::hat);
        // fall back to whatever's closest when the sample has no clear kick/snare/hat
        if (kicks.empty()) kicks = { 0 };
        if (snares.empty()) { auto t = pool (Hit::other); snares = t.empty() ? std::vector<int> { juce::jmin (count - 1, count / 2) } : t; }
        if (hats.empty())
        {
            int shortest = 0;
            for (int i = 1; i < count; ++i) if (lenOf (i) < lenOf (shortest)) shortest = i;
            hats = { shortest };
        }

        switch (style)
        {
            case 0:   // original groove
                for (int i = 0; i < count; ++i)
                {
                    double b = beatOf (markers[(size_t) i]);
                    if (b >= total - 1e-6) break;
                    out.push_back ({ i, b, juce::jmin (lenOf (i), total - b), 110 });
                }
                break;

            case 1:   // boom bap
                for (int bar = 0; bar < bars; ++bar)
                {
                    const double o = bar * 4.0;
                    std::vector<int> kickSteps { 0, 10 };
                    if (chance (0.6)) kickSteps.push_back (chance (0.5) ? 7 : 3);
                    if (bar % 2 == 1 && chance (0.5)) kickSteps.push_back (15);
                    for (int s : kickSteps) out.push_back ({ pick (kicks), o + s * 0.25, 0.5, s == 0 ? 120 : 104 });
                    for (int s : { 4, 12 }) out.push_back ({ pick (snares), o + s * 0.25, 1.0, 118 });
                    if (chance (0.35)) out.push_back ({ pick (snares), o + (chance (0.5) ? 14 : 6) * 0.25 + 0.25, 0.25, 58 });   // ghost
                    for (int s = 0; s < 16; s += 2) out.push_back ({ pick (hats), o + s * 0.25 + (s % 4 == 2 ? 0.04 : 0.0), 0.25, s % 4 == 0 ? 92 : 70 });
                }
                break;

            case 2:   // resequence on 16ths
            {
                auto everyone = all();
                for (int s = 0; s < bars * 16; ++s)
                {
                    if (s == 0) { out.push_back ({ 0, 0.0, 0.25, 118 }); continue; }
                    if (! chance (s % 4 == 0 ? 0.8 : 0.5)) continue;
                    out.push_back ({ pick (everyone), s * 0.25, 0.25, s % 4 == 0 ? 112 : 92 });
                }
                break;
            }

            case 3:   // stutter
            {
                double t = 0; int i = 0;
                while (t < total - 1e-6)
                {
                    double len = juce::jlimit (0.125, 1.0, std::round (lenOf (i) * 8.0) / 8.0);
                    if (chance (0.3))
                    {
                        int reps = chance (0.5) ? 4 : 2;
                        double each = reps == 4 ? 0.125 : 0.25;
                        for (int r = 0; r < reps && t < total - 1e-6; ++r) { out.push_back ({ i, t, each, 118 - r * 12 }); t += each; }
                    }
                    else { out.push_back ({ i, t, juce::jmin (len, total - t), 112 }); t += len; }
                    i = (i + 1) % count;
                }
                break;
            }

            case 4:   // half-time
                for (int bar = 0; bar < bars; ++bar)
                {
                    const double o = bar * 4.0;
                    out.push_back ({ pick (kicks), o, 1.0, 122 });
                    if (chance (0.5)) out.push_back ({ pick (kicks), o + 2.75, 0.5, 100 });
                    out.push_back ({ pick (snares), o + 2.0, 1.5, 120 });
                    for (int q = 0; q < 4; ++q) out.push_back ({ pick (hats), o + q + 0.5, 0.25, 74 });
                }
                break;

            default:  // dragged: original order, pitched down, slowed onto the eighths, some reversed
            {
                double t = 0; int i = 0;
                std::uniform_int_distribution<int> down (3, 7);
                while (t < total - 1e-6)
                {
                    Step s { i, t, 0.5, 108 };
                    s.fx.semitones = -down (rng);
                    s.fx.reverse = chance (0.2);
                    double len = juce::jlimit (0.5, 2.0, std::ceil (lenOf (i) * std::pow (2.0, -s.fx.semitones / 12.0) * 2.0) / 2.0);
                    s.length = juce::jmin (len, total - t);
                    out.push_back (s);
                    t += len;
                    i = (i + 1) % count;
                }
                break;
            }
        }
        std::stable_sort (out.begin(), out.end(), [] (const Step& x, const Step& y) { return x.beat < y.beat; });
        return out;
    }

    /** Renders a flip to audio, loop-ready: tails that run past the end wrap round to the start. */
    inline juce::AudioBuffer<float> renderFlip (const std::vector<Step>& steps, const juce::AudioBuffer<float>& src, double sr,
                                                const std::vector<int>& markers, const std::vector<SliceFx>& padFx,
                                                double bpm, int bars, bool fades)
    {
        const int chans = juce::jmax (1, juce::jmin (2, src.getNumChannels()));
        const int total = juce::jmax (1, (int) std::lround (bars * 4.0 * 60.0 / bpm * sr));
        juce::AudioBuffer<float> out (chans, total);
        out.clear();
        std::map<std::tuple<int, double, float, bool>, juce::AudioBuffer<float>> cache;
        for (auto& s : steps)
        {
            if (s.slice < 0 || s.slice >= (int) markers.size()) continue;
            SliceFx fx = s.slice < (int) padFx.size() ? padFx[(size_t) s.slice] : SliceFx {};
            fx.semitones += s.fx.semitones;
            fx.gainDb += s.fx.gainDb;
            fx.reverse = fx.reverse != s.fx.reverse;
            auto key = std::make_tuple (s.slice, fx.semitones, fx.gainDb, fx.reverse);
            auto it = cache.find (key);
            if (it == cache.end())
            {
                auto r = range (markers, s.slice, src.getNumSamples());
                it = cache.emplace (key, render (src, sr, r.first, r.second, fx, false, fades)).first;
            }
            const auto& hit = it->second;
            const int at = (int) std::lround (s.beat * 60.0 / bpm * sr);
            const int gate = juce::jmin (hit.getNumSamples(), juce::jmax (16, (int) std::lround (s.length * 60.0 / bpm * sr)));
            const int fo = juce::jmin (gate / 3, (int) (0.004 * sr));
            const float vel = s.velocity / 127.0f;
            for (int c = 0; c < chans; ++c)
            {
                const float* x = hit.getReadPointer (juce::jmin (c, hit.getNumChannels() - 1));
                float* y = out.getWritePointer (c);
                for (int i = 0; i < gate; ++i)
                {
                    float g = vel;
                    if (i >= gate - fo && gate < hit.getNumSamples()) g *= (float) (gate - i) / (float) juce::jmax (1, fo);   // cut cleanly
                    y[(at + i) % total] += x[i] * g;
                }
            }
        }
        float pk = peakOf (out);
        if (pk > 0.98f) out.applyGain (0.98f / pk);
        return out;
    }
}
