#pragma once
#include "Visualizers.h"

namespace bcv
{
    /** How a video moves between styles. Single = one style all the way through (the old behaviour). */
    struct Patterns
    {
        enum Mode { single = 0, picked, random };
        enum When { followSong = 0, timer };
        int mode = single;
        std::vector<int> picks;          // style indices, in the order you picked them
        bool shuffle = false;            // play the picks in a shuffled order
        int when = followSong;
        double minSeconds = 8;           // follow the song: never switch sooner than this...
        double maxSeconds = 30;          // ...and never hold a style longer than this
        double everySeconds = 16;        // timer
        double fadeSeconds = 2.5;        // crossfade length
        bool cutOnSlams = true;          // hard cut instead of a fade when it slams back in after a quieter bit
        juce::uint32 seed = 1;           // random and shuffle orders; the same seed gives the same video

        static juce::StringArray modeNames()  { return { "Single", "Picked", "Random" }; }
        static juce::StringArray whenNames()  { return { "Follow the song", "Every few seconds" }; }
        static const std::vector<double>& everyChoices() { static const std::vector<double> v { 4, 8, 16, 30, 60 }; return v; }
    };

    /** The moments in a track where the sound changes: section changes and slams (from a quick analysis pass). */
    struct SongEvents
    {
        struct Event { double t; bool slam; float strength; };
        std::vector<Event> events;
        double duration = 0;
        bool empty() const { return duration <= 0; }
    };
    /** Listens through the whole track (analysis only, no drawing; a few seconds for a long track).
        shouldStop lets a background thread give up early. */
    SongEvents scanSong (const juce::AudioBuffer<float>& audio, double sampleRate, std::function<bool()> shouldStop = {});

    /** Which style plays when. Each step starts a fade at fadeStart; fade 0 = hard cut. */
    struct Plan
    {
        struct Step { double fadeStart = 0, fade = 0; int style = 0; };
        std::vector<Step> steps;
        bool switches() const { return steps.size() > 1; }
        /** the step playing at t (the newer one while a fade is under way) */
        int stepAt (double t) const;
    };
    Plan makePlan (const Patterns&, const SongEvents&, int singleStyle, int numStyles);

    /** Plays a Plan: hands out the style (or the two styles and the mix during a fade) for each moment,
        resetting a style when it comes on so its trails and particles start fresh. */
    class StylePlayer
    {
    public:
        struct Now { Viz* a = nullptr; Viz* b = nullptr; float mix = 0; int styleA = 0, styleB = -1; };
        /** frames must come in order; call reset() after a seek */
        Now at (double t, const Plan& plan, std::vector<std::unique_ptr<Viz>>& all);
        void reset() { onA = onB = -1; }
    private:
        int onA = -1, onB = -1;
    };
}
