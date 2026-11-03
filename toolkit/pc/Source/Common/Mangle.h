#pragma once
#include "JuceHeader.h"

/** Offline sound mangling: extreme time stretch and tape/VHS rot. */
namespace tk::mangle
{
    struct StretchOptions { double factor = 8.0; double windowSeconds = 0.25; unsigned seed = 1; };
    /** Paulstretch-style smear: magnitude-only resynthesis with random phases. */
    juce::AudioBuffer<float> stretch (const juce::AudioBuffer<float>& in, double sampleRate, StretchOptions o,
                                      std::function<bool (float)> progress = nullptr);

    struct RotOptions
    {
        double wow = 0.4, flutter = 0.3, drift = 0.3;   // pitch wobble amounts 0..1
        double hiss = 0.25, dropouts = 0.2;             // 0..1
        double tone = 0.5;                              // 0 = muffled .. 1 = bright
        double saturation = 0.4, crush = 0.0;           // 0..1
        unsigned seed = 1;
    };
    juce::AudioBuffer<float> tapeRot (const juce::AudioBuffer<float>& in, double sampleRate, RotOptions o);
}
