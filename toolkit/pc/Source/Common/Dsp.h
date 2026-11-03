#pragma once
#include "JuceHeader.h"
#include <vector>

/** Analysis building blocks shared by the app, the plugin and the tests. */
namespace tk
{
    // ------------------------------------------------------------ notes
    juce::String noteName (int midi);            // "C#3"
    juce::String pitchClass (int midi);          // "C#"
    double midiFromHz (double hz, double a4 = 440.0);
    double hzFromMidi (double midi, double a4 = 440.0);
    extern const char* const kNoteNames[12];

    // ------------------------------------------------------------ pitch (YIN)
    /** Fundamental in Hz, or -1 when there's no clear pitch. Works 25 Hz .. 4 kHz. */
    double yinPitch (const float* x, int n, double sampleRate, double threshold = 0.12);

    // ------------------------------------------------------------ spectrum
    class Spectrum
    {
    public:
        explicit Spectrum (int order);            // size = 2^order
        int size() const noexcept { return n; }
        /** Hann-windowed magnitudes in dBFS, n/2 bins. */
        const std::vector<float>& analyse (const float* x);
        double binHz (double sr) const noexcept { return sr / n; }
    private:
        int n;
        juce::dsp::FFT fft;
        std::vector<float> window, work, mags;
    };

    // ------------------------------------------------------------ loudness (BS.1770)
    class Loudness
    {
    public:
        void prepare (double sampleRate);
        void reset();
        /** Feed one channel (mono) or two (stereo). right may be nullptr. */
        void process (const float* left, const float* right, int n);
        double momentary() const noexcept { return lufsM; }
        double shortTerm() const noexcept { return lufsS; }
        double integrated() const noexcept { return lufsI; }
        double truePeakDb() const noexcept { return tpDb; }
    private:
        struct Bq { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
                    double run (double x) { double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; } };
        Bq shelfL, hpL, shelfR, hpR;
        double sr = 48000, acc = 0; int fill = 0, blockLen = 4800;
        std::vector<double> blocks; int blockCount = 0;
        std::vector<double> gated;
        double lufsM = -120, lufsS = -120, lufsI = -120, tpDb = -120;
        float prevL = 0, prevR = 0;
    };

    // ------------------------------------------------------------ hum (50/60 Hz mains + harmonics)
    struct HumResult { int mainsHz = 0; double ratioDb = -120; bool present = false; };
    /** Compares the energy at mains frequency + harmonics against the broadband level. */
    HumResult detectHum (const float* x, int n, double sampleRate);
    double goertzel (const float* x, int n, double sampleRate, double hz);

    // ------------------------------------------------------------ onsets, tempo, key
    struct OnsetOptions { double sensitivity = 0.5; int minGapMs = 60; int maxSlices = 64; };
    std::vector<int> detectOnsets (const juce::AudioBuffer<float>& audio, double sampleRate, OnsetOptions opt = {});
    /** Tempo in BPM (70..180 folded), or 0 when there's no clear pulse. */
    double estimateBpm (const juce::AudioBuffer<float>& audio, double sampleRate);
    struct KeyResult { int root = -1; bool minor = false; double confidence = 0; juce::String name() const; };
    KeyResult estimateKey (const juce::AudioBuffer<float>& audio, double sampleRate);
    /** Spectral centroid in Hz over the whole file. */
    double spectralCentroid (const juce::AudioBuffer<float>& audio, double sampleRate);
    /** 0 = very dark .. 1 = very bright, from the centroid (log scale). */
    double brightness (double centroidHz);

    // ------------------------------------------------------------ helpers
    float peakOf (const juce::AudioBuffer<float>& b);
    double rmsDb (const float* x, int n);
    juce::AudioBuffer<float> mixToMono (const juce::AudioBuffer<float>& b);
    /** first sample above threshold (linear), or -1 */
    int firstAbove (const juce::AudioBuffer<float>& b, float threshold);
    /** last sample above threshold (linear), or -1 */
    int lastAbove (const juce::AudioBuffer<float>& b, float threshold);
}
