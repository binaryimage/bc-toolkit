#pragma once
#include "Common/JuceHeader.h"
#include <array>
#include <vector>

namespace bcv
{
    constexpr int kBands = 128;      // log-spaced, 30 Hz .. 16 kHz
    constexpr int kWave = 1024;      // samples of waveform per frame

    /** Everything a visualizer gets for one video frame. Values are 0..1 unless noted. */
    struct Frame
    {
        double t = 0, dt = 1.0 / 30;            // seconds
        int64_t index = 0;
        double duration = 1;                    // whole track, seconds
        std::array<float, kBands> bands {};     // this frame's spectrum
        std::array<float, kBands> smooth {};    // fast attack, slow release
        float rms = 0, peak = 0;
        float bass = 0, mid = 0, treble = 0;    // smoothed band groups
        float flux = 0;                         // spectral change
        float beat = 0;                         // 1 on a detected beat, decays
        bool onset = false;                     // true on the frame a beat lands
        float energy = 0;                       // slow overall loudness
        std::array<float, kWave> L {}, R {};    // the most recent waveform
    };

    /** Turns audio into Frames, one video frame at a time (keeps state for smoothing and beats). */
    class Analyzer
    {
    public:
        Analyzer();
        void reset();
        /** analyse the moment t in buf (any channel count) at frame rate fps */
        void analyse (const juce::AudioBuffer<float>& buf, double sampleRate, double t, double fps, Frame& out);

    private:
        juce::dsp::FFT fft { 11 };               // 2048
        std::vector<float> window, work;
        std::array<int, kBands + 1> edges {};
        std::array<float, kBands> prev {}, smooth {};
        std::vector<float> fluxHistory;
        double lastBeatT = -10, lastT = -1;
        float beat = 0, energy = 0, bass = 0, mid = 0, treble = 0;
        double edgeRate = 0;
        void makeEdges (double sampleRate);
    };
}
