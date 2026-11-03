#include "Dsp.h"
#include <cmath>
#include <algorithm>
#include <numeric>

namespace tk
{
const char* const kNoteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

juce::String pitchClass (int midi) { return kNoteNames[((midi % 12) + 12) % 12]; }
// Ableton / MPC naming: middle C (60) is C3
juce::String noteName (int midi) { return pitchClass (midi) + juce::String (midi / 12 - 2); }
double midiFromHz (double hz, double a4) { return 69.0 + 12.0 * std::log2 (hz / a4); }
double hzFromMidi (double m, double a4) { return a4 * std::pow (2.0, (m - 69.0) / 12.0); }

// ------------------------------------------------------------ YIN
double yinPitch (const float* x, int n, double sr, double threshold)
{
    const int maxTau = std::min (n / 2, (int) (sr / 25.0));
    const int minTau = std::max (2, (int) (sr / 4200.0));
    if (maxTau <= minTau + 2) return -1;
    const int w = n - maxTau;
    std::vector<double> d ((size_t) maxTau + 1, 0.0);
    for (int tau = 1; tau <= maxTau; ++tau)
    {
        double s = 0;
        for (int i = 0; i < w; ++i) { double diff = x[i] - x[i + tau]; s += diff * diff; }
        d[(size_t) tau] = s;
    }
    // cumulative mean normalised difference
    double run = 0; d[0] = 1;
    for (int tau = 1; tau <= maxTau; ++tau) { run += d[(size_t) tau]; d[(size_t) tau] = run > 0 ? d[(size_t) tau] * tau / run : 1; }
    int best = -1;
    for (int tau = minTau; tau < maxTau; ++tau)
        if (d[(size_t) tau] < threshold)
        {
            while (tau + 1 < maxTau && d[(size_t) tau + 1] < d[(size_t) tau]) ++tau;
            best = tau; break;
        }
    if (best < 0) return -1;
    double b = best;
    if (best > 1 && best < maxTau)
    {
        double a = d[(size_t) best - 1], c = d[(size_t) best + 1], m = d[(size_t) best];
        double den = a + c - 2 * m;
        if (std::abs (den) > 1e-12) b += 0.5 * (a - c) / den;
    }
    return sr / b;
}

// ------------------------------------------------------------ spectrum
Spectrum::Spectrum (int order) : n (1 << order), fft (order)
{
    window.resize ((size_t) n);
    for (int i = 0; i < n; ++i) window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * i / (n - 1));
    work.assign ((size_t) n * 2, 0.0f);
    mags.assign ((size_t) n / 2, -120.0f);
}

const std::vector<float>& Spectrum::analyse (const float* x)
{
    for (int i = 0; i < n; ++i) work[(size_t) i] = x[i] * window[(size_t) i];
    std::fill (work.begin() + n, work.end(), 0.0f);
    fft.performFrequencyOnlyForwardTransform (work.data(), true);
    const float scale = 4.0f / n; // Hann coherent gain 0.5, one-sided x2
    for (int i = 0; i < n / 2; ++i) mags[(size_t) i] = 20.0f * std::log10 (work[(size_t) i] * scale + 1e-9f);
    return mags;
}

// ------------------------------------------------------------ loudness
void Loudness::prepare (double sampleRate)
{
    sr = sampleRate;
    // K-weighting coefficients for any rate (BS.1770-4 / pyloudnorm derivation)
    auto setShelf = [&] (Bq& f) {
        double G = 3.999843853973347, Q = 0.7071752369554196, fc = 1681.974450955533;
        double K = std::tan (juce::MathConstants<double>::pi * fc / sr);
        double Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
        double a0 = 1.0 + K / Q + K * K;
        f.b0 = (Vh + Vb * K / Q + K * K) / a0; f.b1 = 2.0 * (K * K - Vh) / a0; f.b2 = (Vh - Vb * K / Q + K * K) / a0;
        f.a1 = 2.0 * (K * K - 1.0) / a0; f.a2 = (1.0 - K / Q + K * K) / a0; f.z1 = f.z2 = 0;
    };
    auto setHp = [&] (Bq& f) {
        double Q = 0.5003270373238773, fc = 38.13547087602444;
        double K = std::tan (juce::MathConstants<double>::pi * fc / sr);
        double a0 = 1.0 + K / Q + K * K;
        f.b0 = 1.0; f.b1 = -2.0; f.b2 = 1.0;
        f.a1 = 2.0 * (K * K - 1.0) / a0; f.a2 = (1.0 - K / Q + K * K) / a0; f.z1 = f.z2 = 0;
    };
    setShelf (shelfL); setShelf (shelfR); setHp (hpL); setHp (hpR);
    blockLen = (int) std::round (sr * 0.1);
    reset();
}

void Loudness::reset()
{
    acc = 0; fill = 0; blockCount = 0;
    blocks.assign (30, 0.0);
    gated.clear();
    lufsM = lufsS = lufsI = tpDb = -120;
    shelfL.z1 = shelfL.z2 = hpL.z1 = hpL.z2 = shelfR.z1 = shelfR.z2 = hpR.z1 = hpR.z2 = 0;
}

void Loudness::process (const float* L, const float* R, int n)
{
    double tp = std::pow (10.0, tpDb / 20.0);
    for (int i = 0; i < n; ++i)
    {
        double l = L[i], r = R != nullptr ? R[i] : L[i];
        // 4x oversampled true-peak estimate (linear interpolation between samples, cheap but useful)
        for (int k = 1; k <= 4; ++k)
        {
            double t = k / 4.0;
            tp = std::max (tp, std::abs (prevL + (l - prevL) * t));
            tp = std::max (tp, std::abs (prevR + (r - prevR) * t));
        }
        prevL = (float) l; prevR = (float) r;
        double kl = hpL.run (shelfL.run (l)), kr = hpR.run (shelfR.run (r));
        acc += R != nullptr ? kl * kl + kr * kr : 2.0 * kl * kl;
        if (++fill >= blockLen)
        {
            blocks[(size_t) (blockCount % 30)] = acc / fill;
            ++blockCount; acc = 0; fill = 0;
            int nb = std::min (blockCount, 30);
            if (blockCount >= 4)
            {
                double m4 = 0;
                for (int b = 0; b < 4; ++b) m4 += blocks[(size_t) ((blockCount - 1 - b + 30) % 30)];
                m4 /= 4;
                lufsM = -0.691 + 10 * std::log10 (m4 + 1e-12);
                if (lufsM > -70) gated.push_back (m4);
                double s = 0;
                for (int b = 0; b < nb; ++b) s += blocks[(size_t) ((blockCount - 1 - b + 30) % 30)];
                lufsS = -0.691 + 10 * std::log10 (s / nb + 1e-12);
                if (! gated.empty())
                {
                    double mean = std::accumulate (gated.begin(), gated.end(), 0.0) / (double) gated.size();
                    double rel = -0.691 + 10 * std::log10 (mean + 1e-12) - 10;
                    double a = 0; int c = 0;
                    for (double g : gated) if (-0.691 + 10 * std::log10 (g + 1e-12) > rel) { a += g; ++c; }
                    lufsI = c > 0 ? -0.691 + 10 * std::log10 (a / c + 1e-12) : -120;
                }
            }
        }
    }
    tpDb = 20 * std::log10 (tp + 1e-12);
}

// ------------------------------------------------------------ hum
double goertzel (const float* x, int n, double sr, double hz)
{
    double w = 2 * juce::MathConstants<double>::pi * hz / sr, c = 2 * std::cos (w), s1 = 0, s2 = 0;
    for (int i = 0; i < n; ++i) { double s0 = x[i] + c * s1 - s2; s2 = s1; s1 = s0; }
    double p = s1 * s1 + s2 * s2 - c * s1 * s2;
    return std::sqrt (std::max (0.0, p)) * 2.0 / n; // amplitude of a sine at hz
}

HumResult detectHum (const float* x, int n, double sr)
{
    HumResult r;
    double rms = 0;
    for (int i = 0; i < n; ++i) rms += (double) x[i] * x[i];
    rms = std::sqrt (rms / std::max (1, n));
    if (rms < 1e-6) return r;
    for (int mains : { 50, 60 })
    {
        double e = 0;
        for (int h = 1; h <= 5; ++h) { double a = goertzel (x, n, sr, mains * h); e += a * a / 2; }
        double ratio = 10 * std::log10 (e / (rms * rms) + 1e-12);
        if (ratio > r.ratioDb) { r.ratioDb = ratio; r.mainsHz = mains; }
    }
    // hum is "present" when mains + harmonics carry a big share of the energy and the level isn't music-loud
    r.present = r.ratioDb > -6.0 && 20 * std::log10 (rms) < -20.0;
    return r;
}

// ------------------------------------------------------------ helpers
float peakOf (const juce::AudioBuffer<float>& b)
{
    float p = 0;
    for (int c = 0; c < b.getNumChannels(); ++c) p = std::max (p, b.getMagnitude (c, 0, b.getNumSamples()));
    return p;
}

double rmsDb (const float* x, int n)
{
    double s = 0;
    for (int i = 0; i < n; ++i) s += (double) x[i] * x[i];
    return 10 * std::log10 (s / std::max (1, n) + 1e-12);
}

juce::AudioBuffer<float> mixToMono (const juce::AudioBuffer<float>& b)
{
    juce::AudioBuffer<float> m (1, b.getNumSamples());
    m.clear();
    for (int c = 0; c < b.getNumChannels(); ++c) m.addFrom (0, 0, b, c, 0, b.getNumSamples(), 1.0f / b.getNumChannels());
    return m;
}

int firstAbove (const juce::AudioBuffer<float>& b, float th)
{
    for (int i = 0; i < b.getNumSamples(); ++i)
        for (int c = 0; c < b.getNumChannels(); ++c)
            if (std::abs (b.getSample (c, i)) > th) return i;
    return -1;
}

int lastAbove (const juce::AudioBuffer<float>& b, float th)
{
    for (int i = b.getNumSamples() - 1; i >= 0; --i)
        for (int c = 0; c < b.getNumChannels(); ++c)
            if (std::abs (b.getSample (c, i)) > th) return i;
    return -1;
}

// ------------------------------------------------------------ onsets (spectral flux)
static std::vector<float> fluxEnvelope (const juce::AudioBuffer<float>& mono, int hop, int order)
{
    Spectrum sp (order);
    const int n = sp.size(), len = mono.getNumSamples();
    std::vector<float> prev ((size_t) n / 2, 0.0f), cur ((size_t) n / 2, 0.0f), env;
    std::vector<float> frame ((size_t) n, 0.0f);
    env.reserve ((size_t) juce::jmax (0, len / hop + 1));
    const float* x = mono.getReadPointer (0);
    const float k = std::log (10.0f) / 20.0f;
    for (int pos = 0; pos + hop <= len; pos += hop)
    {
        for (int i = 0; i < n; ++i) { int j = pos + i - n / 2; frame[(size_t) i] = (j >= 0 && j < len) ? x[j] : 0.0f; }
        const auto& m = sp.analyse (frame.data());
        // Octave bands, each averaged over its bins, so a kick (a few low bins) counts as much as a hat (hundreds of high ones).
        double f = 0;
        for (int lo = 2; lo < n / 2; lo *= 2)
        {
            int hi = juce::jmin (n / 2, lo * 2);
            double band = 0;
            for (int i = lo; i < hi; ++i)
            {
                cur[(size_t) i] = std::log1p (1000.0f * std::exp (m[(size_t) i] * k));   // log-compressed magnitude
                float d = cur[(size_t) i] - prev[(size_t) i];
                if (d > 0) band += d;
            }
            f += band / (hi - lo);
        }
        std::swap (prev, cur);
        env.push_back ((float) f);
    }
    return env;
}

OnsetEnvelope onsetEnvelope (const juce::AudioBuffer<float>& audio, double)
{
    OnsetEnvelope e;
    auto mono = mixToMono (audio);
    e.env = fluxEnvelope (mono, e.hop, 10);
    e.firstSound = firstAbove (mono, 0.01f);
    return e;
}

std::vector<int> detectOnsets (const juce::AudioBuffer<float>& audio, double sr, OnsetOptions opt)
{
    return pickOnsets (onsetEnvelope (audio, sr), sr, opt);
}

std::vector<int> pickOnsets (const OnsetEnvelope& e, double sr, OnsetOptions opt)
{
    const auto& env = e.env;
    const int hop = e.hop;
    std::vector<int> out;
    if (env.size() < 3) return out;
    // adaptive threshold: local median + offset scaled by sensitivity
    const int W = 12;
    double mx = *std::max_element (env.begin(), env.end());
    if (mx <= 0) return out;
    double delta = juce::jmap (juce::jlimit (0.0, 1.0, opt.sensitivity), 0.35, 0.03) * mx;
    int minGap = std::max (1, (int) (opt.minGapMs * sr / 1000.0 / hop));
    int last = -minGap;
    std::vector<float> win;
    win.reserve ((size_t) W * 2);
    for (int i = 1; i + 1 < (int) env.size(); ++i)
    {
        int a = std::max (0, i - W), b = std::min ((int) env.size(), i + W);
        win.assign (env.begin() + a, env.begin() + b);
        std::nth_element (win.begin(), win.begin() + (long) win.size() / 2, win.end());
        double th = win[win.size() / 2] + delta;
        if (env[(size_t) i] > th && env[(size_t) i] >= env[(size_t) i - 1] && env[(size_t) i] >= env[(size_t) i + 1] && i - last >= minGap)
        {
            // walk back to where the attack starts in the waveform
            int s = std::max (0, i * hop - hop);
            out.push_back (s);
            last = i;
        }
    }
    // make sure the very start is a slice if audio begins immediately
    int fa = e.firstSound;
    if (fa >= 0 && (out.empty() || out.front() - fa > (int) (0.05 * sr))) out.insert (out.begin(), std::max (0, fa - 64));
    if ((int) out.size() > opt.maxSlices && opt.maxSlices > 0)
    {
        // keep the strongest onsets
        std::vector<std::pair<float, int>> s;
        for (int p : out) s.push_back ({ env[(size_t) std::min ((int) env.size() - 1, p / hop + 1)], p });
        std::sort (s.begin(), s.end(), [] (auto& x, auto& y) { return x.first > y.first; });
        s.resize ((size_t) opt.maxSlices);
        out.clear();
        for (auto& x : s) out.push_back (x.second);
        std::sort (out.begin(), out.end());
    }
    return out;
}

double estimateBpm (const juce::AudioBuffer<float>& audio, double sr)
{
    if (audio.getNumSamples() < sr * 2) return 0;
    auto mono = mixToMono (audio);
    const int hop = 512;
    auto env = fluxEnvelope (mono, hop, 10);
    if (env.size() < 64) return 0;
    double mean = std::accumulate (env.begin(), env.end(), 0.0) / (double) env.size();
    for (auto& e : env) e = (float) std::max (0.0, e - mean);
    const double fps = sr / hop;
    double bestScore = 0, bestBpm = 0;
    for (double bpm = 60; bpm <= 200; bpm += 0.5)
    {
        double lag = fps * 60.0 / bpm;
        double s = 0;
        for (size_t i = 0; i < env.size(); ++i)
        {
            double j = (double) i + lag;
            size_t j0 = (size_t) j;
            if (j0 + 1 >= env.size()) break;
            double fr = j - (double) j0;
            s += env[i] * (env[j0] * (1 - fr) + env[j0 + 1] * fr);
            double j2 = (double) i + 2 * lag; size_t k0 = (size_t) j2;
            if (k0 + 1 < env.size()) s += 0.5 * env[i] * env[k0];
        }
        // gentle preference for 85..150 BPM (most music) to resolve half/double ambiguity
        double w = std::exp (-0.5 * std::pow (std::log2 (bpm / 115.0) / 0.9, 2));
        if (s * w > bestScore) { bestScore = s * w; bestBpm = bpm; }
    }
    // confidence check: must beat the average lag score clearly
    return bestScore > 0 ? std::round (bestBpm * 2) / 2 : 0;
}

juce::String KeyResult::name() const
{
    if (root < 0) return "-";
    return juce::String (kNoteNames[root]) + (minor ? "m" : "");
}

KeyResult estimateKey (const juce::AudioBuffer<float>& audio, double sr)
{
    KeyResult r;
    auto mono = mixToMono (audio);
    const int order = 13;
    Spectrum sp (order);
    const int n = sp.size(), len = mono.getNumSamples();
    if (len < n) return r;
    double chroma[12] = {};
    std::vector<float> frame ((size_t) n);
    const float* x = mono.getReadPointer (0);
    int frames = 0;
    for (int pos = 0; pos + n <= len && frames < 400; pos += n / 2, ++frames)
    {
        std::copy (x + pos, x + pos + n, frame.begin());
        const auto& m = sp.analyse (frame.data());
        for (int i = 1; i < n / 2; ++i)
        {
            double hz = i * sr / n;
            if (hz < 55 || hz > 2000) continue;
            double a = std::pow (10.0, m[(size_t) i] / 20.0);
            int pc = ((int) std::lround (midiFromHz (hz)) % 12 + 12) % 12;
            chroma[pc] += a * a;
        }
    }
    static const double major[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
    static const double minorP[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
    auto corr = [&] (const double* prof, int root) {
        double mx = 0, my = 0;
        for (int i = 0; i < 12; ++i) { mx += chroma[(i + root) % 12]; my += prof[i]; }
        mx /= 12; my /= 12;
        double sxy = 0, sxx = 0, syy = 0;
        for (int i = 0; i < 12; ++i)
        {
            double a = chroma[(i + root) % 12] - mx, b = prof[i] - my;
            sxy += a * b; sxx += a * a; syy += b * b;
        }
        return sxx > 0 ? sxy / std::sqrt (sxx * syy) : 0.0;
    };
    double best = -2, second = -2;
    for (int root = 0; root < 12; ++root)
        for (int mi = 0; mi < 2; ++mi)
        {
            double c = corr (mi ? minorP : major, root);
            if (c > best) { second = best; best = c; r.root = root; r.minor = mi == 1; }
            else if (c > second) second = c;
        }
    r.confidence = juce::jlimit (0.0, 1.0, (best - second) * 4 + std::max (0.0, best) * 0.5);
    return r;
}

double spectralCentroid (const juce::AudioBuffer<float>& audio, double sr)
{
    auto mono = mixToMono (audio);
    Spectrum sp (11);
    const int n = sp.size(), len = mono.getNumSamples();
    std::vector<float> frame ((size_t) n);
    double num = 0, den = 0;
    const float* x = mono.getReadPointer (0);
    int frames = 0;
    for (int pos = 0; pos + n <= len && frames < 300; pos += n, ++frames)
    {
        std::copy (x + pos, x + pos + n, frame.begin());
        const auto& m = sp.analyse (frame.data());
        for (int i = 1; i < n / 2; ++i)
        {
            double a = std::pow (10.0, m[(size_t) i] / 20.0);
            num += a * i * sr / n; den += a;
        }
    }
    return den > 0 ? num / den : 0;
}

double brightness (double c)
{
    if (c <= 0) return 0;
    return juce::jlimit (0.0, 1.0, (std::log2 (c) - std::log2 (300.0)) / (std::log2 (6000.0) - std::log2 (300.0)));
}
}
