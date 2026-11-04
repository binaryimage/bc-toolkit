#pragma once
#include "Common/JuceHeader.h"
#include <array>
#include <deque>
#include <vector>

namespace bcv
{
    constexpr int kBands = 128;      // log-spaced, 30 Hz .. 16 kHz
    constexpr int kZones = 32;       // the 128 bands in 32 groups of 4: sub rumble up to air
    constexpr int kWave = 1024;      // samples of waveform per frame

    /** Everything a visualizer gets for one video frame. Values are 0..1 unless noted. */
    struct Frame
    {
        double t = 0, dt = 1.0 / 30;            // seconds
        int64_t index = 0;
        double duration = 1;                    // whole track, seconds
        std::array<float, kBands> bands {};     // this frame's spectrum (part auto-levelled)
        std::array<float, kBands> smooth {};    // fast attack, slow release
        float rms = 0, peak = 0;
        float bass = 0, mid = 0, treble = 0;    // smoothed band groups (part auto-levelled)
        float flux = 0;                         // spectral change
        float beat = 0;                         // 1 on a detected beat, decays
        bool onset = false;                     // true on the frame a beat lands
        float energy = 0;                       // slow overall loudness

        // ---- 32 ranges, each auto-levelled to its own recent floor and ceiling, so quiet ranges still move
        std::array<float, kZones> zones {};     // level of each range
        std::array<float, kZones> zoneHit {};   // 1 when that range jumps suddenly, decays fast
        uint32_t zoneOnsets = 0;                // bit z set on the frame range z jumps

        // ---- the character of the sound (all smoothed)
        float noisiness = 0;    // hiss, noise, distortion (1) vs clean tones (0)
        float brightness = 0;   // dark and muffled (0) vs sharp and bright (1)
        float width = 0;        // mono (0) vs wide stereo (1)
        float pan = 0;          // -1 left .. +1 right
        float pitchMove = 0;    // -1 falling (dives, tape stops) .. +1 rising (risers, sweeps)
        float drone = 0;        // a sound held steady for a long time
        float attack = 0;       // sharp hits recently (1) vs slow swells (0)
        float density = 0;      // how busy: hits per second across all ranges
        float sparse = 0;       // gaps and space
        float crunch = 0;       // squashed, clipped, distorted
        float tail = 0;         // long reverb tails / slow decays
        float silence = 0;      // ramps up during silence
        float swell = 0;        // building up over the last few seconds
        float change = 0;       // 1 when the sound changes section, decays over ~1.5 s
        float drop = 0;         // 1 when it slams back in after something quieter, decays
        int section = 0;        // counts section changes

        std::array<float, kWave> L {}, R {};    // the most recent waveform
    };

    /** Turns audio into Frames, one video frame at a time (keeps state for smoothing, beats and sections). */
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

        // ranges
        std::array<float, kZones> zFloor {}, zCeil {}, zPrev {}, zFluxAvg {}, zHit {}, zLevel {};
        std::array<double, kZones> zLastHit {};
        std::array<float, 3> gFloor {}, gCeil {};
        // character
        float noisiness = 0, brightness = 0, width = 0, pan = 0, pitchMove = 0, pitchRaw = 0, drone = 0, attack = 0, density = 0,
              sparse = 0, crunch = 0, tail = 0, silence = 0, change = 0, drop = 0, prevRms = 0, lastCentroid = -1, fallRate = 0;
        int section = 0;
        double lastChangeT = -10, lastDropT = -10;
        std::deque<double> hitTimes;
        std::deque<float> energyHist;                     // ~6 s of rms
        std::deque<std::array<float, kZones>> profileHist; // ~8 s of range levels
    };
}
