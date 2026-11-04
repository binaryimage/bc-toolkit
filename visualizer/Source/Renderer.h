#pragma once
#include "Visualizers.h"

namespace bcv
{
    /** Everything about a video except the audio itself. */
    struct Look
    {
        int style = 0;
        juce::String title, artist { "binaryimage" };
        int tint = 0;               // see tintNames(); kMultiColour paints each style in a turning rainbow
        int background = 0;         // 0 black, 1 cover image (dimmed), 2 paper
        juce::Image cover;          // optional artwork
        int text = 0;               // 0 bottom left, 1 centred, 2 hidden
        bool fades = true;          // fade in from black and out at the end
        bool vignette = true;       // darker corners + light film grain
        bool progress = false;      // thin progress line along the bottom
        bool coverBadge = true;     // the cover art next to the title (bottom-left text only)
        float intensity = 1.0f;     // 0.25 .. 2
    };

    constexpr int kMultiColour = 6;   // rainbow
    constexpr int kIceColour = 7;     // the same idea in cold colours: cyan, blue, violet, frost
    juce::StringArray tintNames();
    juce::StringArray backgroundNames();
    juce::StringArray textNames();
    Palette paletteFor (const Look&);

    /** Draws one finished video frame: background, visualizer, titles, fades. */
    class Renderer
    {
    public:
        void render (juce::Graphics& g, juce::Rectangle<float> r, const Frame& f, Viz& viz, const Look& look);
    private:
        juce::Image backdrop;               // blurred, dimmed cover sized to the frame
        juce::Rectangle<int> backdropSize;
        juce::Image backdropSource;
        void makeBackdrop (juce::Rectangle<int> size, const juce::Image& cover);
        // multi-colour: the style is drawn white on black into a layer, then each pixel's brightness picks up
        // a colour from a rainbow that spirals out from the centre and turns with the music
        juce::Image layer;
        std::vector<float> hueField;
        juce::Rectangle<int> fieldSize;
        void colourise (const Frame& f, bool paper, bool icy);
    };

    /** Synthetic stereo demo track (kick, snare, hats, bass, pad) used for galleries and tests. */
    juce::AudioBuffer<float> makeDemoTrack (double sampleRate, double seconds);
}
