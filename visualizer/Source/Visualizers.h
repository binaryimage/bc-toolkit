#pragma once
#include "Analysis.h"
#include <memory>

namespace bcv
{
    /** Colours a visualizer draws with. Default is white on black; a tint swaps the white. */
    struct Palette
    {
        juce::Colour bg { 0xff000000 }, fg { 0xffffffff };
        juce::Colour fgA (float a) const { return fg.withAlpha (juce::jlimit (0.0f, 1.0f, a)); }
        /** fg mixed toward bg: 1 = full ink, 0 = background */
        juce::Colour ink (float amount) const { return bg.interpolatedWith (fg, juce::jlimit (0.0f, 1.0f, amount)); }
    };

    /** One visual style. Instances keep state between frames (particles, trails), so frames must come in order. */
    class Viz
    {
    public:
        virtual ~Viz() = default;
        virtual juce::String name() const = 0;
        virtual juce::String blurb() const = 0;
        /** called before the first frame and after a seek */
        virtual void reset() {}
        /** draw over the (already filled) background; r is the frame; intensity 0.25..2 scales the motion */
        virtual void draw (juce::Graphics& g, juce::Rectangle<float> r, const Frame& f, const Palette& p, float intensity) = 0;
    protected:
        /** line width that looks the same at 720p and 2160p */
        static float px (juce::Rectangle<float> r, float w) { return w * r.getHeight() / 1080.0f; }
    };

    /** Every style, in menu order. */
    std::vector<std::unique_ptr<Viz>> makeAll();
    /** styles 51-80 (Trippy.cpp) */
    void addTrippyStyles (std::vector<std::unique_ptr<Viz>>& v);
}
