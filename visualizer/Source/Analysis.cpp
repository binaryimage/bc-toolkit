#include "Analysis.h"

namespace bcv
{
Analyzer::Analyzer()
{
    window.resize (2048);
    for (int i = 0; i < 2048; ++i) window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * i / 2047.0f);
    work.resize (4096);
}

void Analyzer::reset()
{
    prev.fill (0); smooth.fill (0); fluxHistory.clear();
    lastBeatT = -10; lastT = -1; beat = energy = bass = mid = treble = 0;
}

void Analyzer::makeEdges (double sr)
{
    edgeRate = sr;
    const double lo = 30, hi = juce::jmin (16000.0, sr * 0.45);
    for (int b = 0; b <= kBands; ++b)
    {
        double hz = lo * std::pow (hi / lo, (double) b / kBands);
        edges[(size_t) b] = juce::jlimit (1, 1023, (int) std::round (hz / sr * 2048));
    }
    for (int b = 1; b <= kBands; ++b) if (edges[(size_t) b] <= edges[(size_t) b - 1]) edges[(size_t) b] = juce::jmin (1024, edges[(size_t) b - 1] + 1);
}

void Analyzer::analyse (const juce::AudioBuffer<float>& buf, double sr, double t, double fps, Frame& f)
{
    if (sr != edgeRate) makeEdges (sr);
    if (lastT >= 0 && (t < lastT || t - lastT > 1.0)) reset();   // jumped: start fresh
    const double dt = lastT < 0 ? 1.0 / fps : juce::jmax (1e-3, t - lastT);
    lastT = t;
    f.t = t; f.dt = dt;
    const int n = buf.getNumSamples(), chans = buf.getNumChannels();
    const int end = (int) (t * sr);
    auto sample = [&] (int ch, int i) -> float { return (i >= 0 && i < n && chans > 0) ? buf.getSample (juce::jmin (ch, chans - 1), i) : 0.0f; };

    // waveform: the last kWave samples
    float pk = 0; double sq = 0;
    for (int i = 0; i < kWave; ++i)
    {
        float l = sample (0, end - kWave + i), r = sample (1, end - kWave + i);
        f.L[(size_t) i] = l; f.R[(size_t) i] = r;
        pk = juce::jmax (pk, std::abs (l), std::abs (r));
        sq += 0.5 * (l * l + r * r);
    }
    f.peak = juce::jlimit (0.0f, 1.0f, pk);
    f.rms = juce::jlimit (0.0f, 1.0f, (float) std::sqrt (sq / kWave) * 2.5f);

    // spectrum of the 2048 samples centred just before t
    std::fill (work.begin(), work.end(), 0.0f);
    for (int i = 0; i < 2048; ++i) work[(size_t) i] = 0.5f * (sample (0, end - 2048 + i) + sample (1, end - 2048 + i)) * window[(size_t) i];
    fft.performFrequencyOnlyForwardTransform (work.data());
    float flux = 0;
    const float attack = 0.6f, release = (float) std::exp (-dt * 6.0);
    for (int b = 0; b < kBands; ++b)
    {
        float m = 0;
        for (int k = edges[(size_t) b]; k < juce::jmax (edges[(size_t) b] + 1, edges[(size_t) b + 1]); ++k) m = juce::jmax (m, work[(size_t) k]);
        float db = juce::Decibels::gainToDecibels (m / 512.0f, -100.0f);
        // gentle tilt so the top end isn't always flat on the floor
        db += 3.0f * std::log2 (1.0f + b * 0.12f);
        float v = juce::jlimit (0.0f, 1.0f, (db + 72.0f) / 66.0f);
        f.bands[(size_t) b] = v;
        flux += juce::jmax (0.0f, v - prev[(size_t) b]);
        prev[(size_t) b] = v;
        smooth[(size_t) b] = v > smooth[(size_t) b] ? smooth[(size_t) b] + (v - smooth[(size_t) b]) * attack : smooth[(size_t) b] * release + v * (1 - release);
    }
    f.smooth = smooth;
    auto group = [&] (int a, int z) { float s = 0; for (int b = a; b < z; ++b) s += smooth[(size_t) b]; return s / (z - a); };
    const float k = (float) std::exp (-dt * 8.0);
    bass = bass * k + group (0, 26) * (1 - k);
    mid = mid * k + group (26, 90) * (1 - k);
    treble = treble * k + group (90, kBands) * (1 - k);
    f.bass = bass; f.mid = mid; f.treble = treble;
    f.flux = juce::jlimit (0.0f, 1.0f, flux / 12.0f);

    // beats: flux well above its recent average, at least 180 ms apart
    fluxHistory.push_back (flux);
    const size_t keep = (size_t) juce::jmax (8, (int) (fps * 1.5));
    if (fluxHistory.size() > keep) fluxHistory.erase (fluxHistory.begin(), fluxHistory.begin() + (long) (fluxHistory.size() - keep));
    float mean = 0; for (float x : fluxHistory) mean += x; mean /= (float) fluxHistory.size();
    f.onset = flux > mean * 1.6f + 0.8f && t - lastBeatT > 0.18 && f.rms > 0.02f;
    if (f.onset) { lastBeatT = t; beat = 1; }
    else beat *= (float) std::exp (-dt * 7.0);
    f.beat = beat;
    const float ke = (float) std::exp (-dt * 1.5);
    energy = energy * ke + f.rms * (1 - ke);
    f.energy = juce::jlimit (0.0f, 1.0f, energy * 1.4f);
    f.index = (int64_t) std::llround (t * fps);
}
}
