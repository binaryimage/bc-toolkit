#pragma once
#include "Visualizers.h"
#include "Parallel.h"
#include <map>

namespace bcv
{
    /** Everything about a video except the audio itself. */
    /** The reaction effects you can switch off one by one (they all follow the music when on). */
    enum Effect { fxMotion, fxSparkles, fxGlitch, fxShimmer, fxEchoes, fxGhosts, fxTrails, fxStatic, fxFlash, fxColourMotion, kEffectCount };
    /** mirror choices: 0 follows the song (changes each section, kaleidoscope on mid hits), 1 off, then fixed ones */
    enum Mirror { mirrorAuto = 0, mirrorOff, mirrorLeftRight, mirrorTopBottom, mirrorFourWay, mirrorFlipped, kMirrorCount };

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
        int reactions = 2;          // 0 off, 1 subtle, 2 wild: the frequency-driven layer on top of every style
        int mirror = mirrorAuto;    // see Mirror; a fixed choice applies even with reactions off
        int effectsOff = 0;         // bit per Effect that's switched off (0 = all on, as before)
        int flow = 0;               // how fast the colours run through the picture: 0 gentle (as before), 1 flowing, 2 racing
        // random palettes (Patterns: new colours on every switch). 0 = use tint. rollB fades in by rollMix.
        juce::uint32 rollA = 0, rollB = 0;
        float rollMix = 0;
        bool effectOn (Effect e) const { return (effectsOff & (1 << (int) e)) == 0; }
        void setEffect (Effect e, bool on) { effectsOff = on ? (effectsOff & ~(1 << (int) e)) : (effectsOff | (1 << (int) e)); }
    };

    constexpr int kMultiColour = 6;   // rainbow
    constexpr int kIceColour = 7;     // the same idea in cold colours: cyan, blue, violet, frost
    constexpr int kFirstMix = 8;      // from here on: the mixed palettes (gradients that move with the music)
    juce::StringArray tintNames();
    juce::StringArray reactionNames();
    juce::StringArray effectNames();         // one per Effect, short enough for a button
    juce::StringArray effectHints();         // what each one does, for tooltips
    juce::StringArray mirrorNames();         // one per Mirror
    juce::StringArray backgroundNames();
    juce::StringArray textNames();
    juce::StringArray flowNames();
    Palette paletteFor (const Look&);

    /** A palette made up from a seed: 3 to 8 colours, always one dark and one hot, so it never turns to mud. */
    struct RolledPalette { std::vector<juce::uint32> stops; int field = 1; float spatial = 0.5f, bright = 0.6f; };
    RolledPalette rollPalette (juce::uint32 seed);
    /** a colour from a rolled palette (x 0..1 round its loop), for showing it in the UI */
    juce::Colour rolledColour (juce::uint32 seed, float x);

    /** Draws one finished video frame: background, visualizer, reactions, colour, titles, fades.
        Keeps state between frames (reactions), so frames should come in order; reset() after a seek. */
    class Renderer
    {
    public:
        void render (juce::Graphics& g, juce::Rectangle<float> r, const Frame& f, Viz& viz, const Look& look);
        /** the same, crossfading from viz to next by mix (0 = all viz, 1 = all next); the reactions carry on across it */
        void render (juce::Graphics& g, juce::Rectangle<float> r, const Frame& f, Viz& viz, const Look& look, Viz* next, float mix);
        void reset();
        /** split the pixel work (and the two styles of a crossfade) across these threads; nullptr = all on this one */
        void setWorkers (Workers* w) { workers = w; }
    private:
        juce::Image backdrop;               // blurred, dimmed cover sized to the frame
        juce::Rectangle<int> backdropSize;
        juce::Image backdropSource;
        void makeBackdrop (juce::Rectangle<int> size, const juce::Image& cover);

        // the style is drawn white on black into a layer, the reactions work on it, then it's coloured
        juce::Image layer, layer2;          // layer2: the incoming style during a crossfade
        Workers* workers = nullptr;
        void extractLum (float mix);         // layer (and layer2, mixed in by mix) into lum
        std::vector<juce::uint8> lum, prevLum, cpy, cr, cg, cb, ca;
        std::vector<int> shift, ex1, ex2, ey1, ey2;
        bool lumReady = false;
        std::map<int, std::vector<float>> fields;          // spatial colour fields, by kind
        juce::Rectangle<int> fieldSize;
        const std::vector<float>& field (int kind);
        juce::AffineTransform reactTransform (const Frame& f, juce::Rectangle<float> r, float react);
        void postFx (const Frame& f, float react, const Look& look);
        void colourise (const Frame& f, const Look& look, float react);
        // reaction state
        float sway = 0, slide = 0, zoomS = 1, offset = 0, sectionShift = 0, flowPhase = 0;
        int lastSection = 0, mirrorMode = 0;
        juce::Random rnd { 77 };
    };

    /** Synthetic stereo demo track (kick, snare, hats, bass, pad) used for galleries and tests. */
    juce::AudioBuffer<float> makeDemoTrack (double sampleRate, double seconds);
}
