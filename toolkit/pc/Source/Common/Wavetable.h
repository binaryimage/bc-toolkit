#pragma once
#include "JuceHeader.h"
#include <vector>

/** Wavetables: 2048-sample frames, morphing keyframes, MiniFreak/Pigments/Serum export, sample import. */
namespace tk::wt
{
    constexpr int N = 2048;
    using Frame = std::vector<float>;

    Frame shape (int kind);                 // 0 sine 1 triangle 2 saw 3 square 4 pulse 5 noise
    extern const char* const kShapeNames[6];
    void removeDc (Frame&);
    void normalize (Frame&);
    void smooth (Frame&);
    void fold (Frame&);
    void mirror (Frame&);
    void crush (Frame&);
    void bandlimit (std::vector<float>& w, int harmonics);
    Frame resampleCycle (const float* src, int len, int n);

    std::vector<Frame> render (const std::vector<Frame>& keys, int total);
    /** mono PCM WAV, frames back to back at pts points each (512 = MiniFreak, 2048 = Pigments/Serum/Vital) */
    juce::MemoryBlock wav (const std::vector<Frame>& frames, int pts, int bits);
    /** Read a wavetable or single-cycle WAV into keyframes (detects 256/512/1024/2048-point frames). */
    std::vector<Frame> framesFromSamples (const std::vector<float>& s);
    /** Pull `count` single cycles evenly across a pitched recording (uses YIN for the period). */
    std::vector<Frame> framesFromRecording (const juce::AudioBuffer<float>& b, double sr, int count, double* detectedHz = nullptr);
}
