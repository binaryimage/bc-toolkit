#pragma once
#include "JuceHeader.h"
#include "Look.h"
#include <vector>
#include <deque>

namespace tk
{
    /** A raised panel with an engraved title. Children go inside content(). */
    class Slab : public juce::Component
    {
    public:
        explicit Slab (juce::String title = {}) : title (std::move (title)) {}
        void setTitle (juce::String t) { title = std::move (t); repaint(); }
        juce::Rectangle<int> content() const;
        void paint (juce::Graphics&) override;
        juce::String title;
    };

    /** Recessed display base: draws the screen well; subclasses paint inside screenArea(). */
    class Screen : public juce::Component
    {
    public:
        juce::Rectangle<float> screenArea() const { return getLocalBounds().toFloat().reduced (2); }
        void paint (juce::Graphics& g) override { drawScreen (g, screenArea(), 8); paintScreen (g, screenArea().reduced (6)); }
        virtual void paintScreen (juce::Graphics&, juce::Rectangle<float>) {}
    };

    /** Stacked spectrum lines in perspective, newest at the front (think "Unknown Pleasures"). */
    class Waterfall3D : public Screen
    {
    public:
        void push (const std::vector<float>& magsDb, double binHz);
        void clear() { lines.clear(); repaint(); }
        void paintScreen (juce::Graphics&, juce::Rectangle<float>) override;
        int depth = 48;
        float floorDb = -90, ceilDb = -6;
    private:
        std::deque<std::vector<float>> lines; // each: 160 log-spaced bands 0..1
    };

    /** Classic scope trace with a pitch-locked window when a period is known. */
    class Scope : public Screen
    {
    public:
        void setData (const float* x, int n, double periodSamples);
        void paintScreen (juce::Graphics&, juce::Rectangle<float>) override;
    private:
        std::vector<float> data; double period = 0;
    };

    /** Goniometer / X-Y view (mid-side rotated). */
    class XYView : public Screen
    {
    public:
        void setData (const float* l, const float* r, int n);
        void paintScreen (juce::Graphics&, juce::Rectangle<float>) override;
        double correlation = 1;
    private:
        std::vector<float> L, R;
    };

    /** Big tuner: note name, cents needle on an arc, frequency. */
    class TunerDial : public Screen
    {
    public:
        void setPitch (double hz, double a4);
        void paintScreen (juce::Graphics&, juce::Rectangle<float>) override;
    private:
        double hz = -1, a4 = 440, smooth = 0; bool has = false;
    };

    /** Vertical LED ladder with a peak-hold tick. Values in dBFS. */
    class LedMeter : public juce::Component
    {
    public:
        void setLevels (float peakDb, float rmsDb);
        void paint (juce::Graphics&) override;
    private:
        float peak = -120, rms = -120, hold = -120; juce::uint32 holdTime = 0;
    };

    /** Waveform overview with optional slice markers and a selected slice. */
    class WaveView : public Screen
    {
    public:
        void setAudio (const juce::AudioBuffer<float>* b) { audio = b; rebuild(); }
        void setSlices (std::vector<int> s) { slices = std::move (s); repaint(); }
        const std::vector<int>& getSlices() const { return slices; }
        void setPlayhead (int sample) { playhead = sample; repaint(); }
        void paintScreen (juce::Graphics&, juce::Rectangle<float>) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void resized() override { rebuild(); }
        std::function<void (int)> onSliceClicked;          // slice index
        std::function<void()> onSlicesEdited;
        int selected = -1;
        bool editable = true;
    private:
        void rebuild();
        int sampleAtX (float x) const;
        float xAtSample (int s) const;
        const juce::AudioBuffer<float>* audio = nullptr;
        std::vector<float> mins, maxs;
        std::vector<int> slices;
        int playhead = -1, dragging = -1;
    };

    /** A wavetable drawn as a 3D stack of frames, current position highlighted. */
    class StackView : public Screen
    {
    public:
        void setFrames (std::vector<std::vector<float>> f) { frames = std::move (f); repaint(); }
        void setPosition (float p) { pos = p; repaint(); }
        void paintScreen (juce::Graphics&, juce::Rectangle<float>) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent& e) override { mouseDrag (e); }
        std::function<void (float)> onScrub;
    private:
        std::vector<std::vector<float>> frames; float pos = 0;
    };

    /** Bars with a 3D extrusion, for cents error per note / per channel. */
    class BarChart3D : public Screen
    {
    public:
        struct Bar { juce::String label; double value; bool valid = true; };
        /** Bipolar chart: bars grow up or down from zero, -range..+range. */
        void setBars (std::vector<Bar> b, double range, juce::String unit);
        /** Unipolar chart: bars grow up from lo towards hi (e.g. -48..0 dBFS). */
        void setBarsRange (std::vector<Bar> b, double lo, double hi, juce::String unit);
        void paintScreen (juce::Graphics&, juce::Rectangle<float>) override;
    private:
        std::vector<Bar> bars; double range = 50, lo = 0, hi = 0; bool uni = false; juce::String unit;
    };

    /** A progress pill that can be updated from a timer. */
    class ProgressPill : public juce::Component
    {
    public:
        void setValue (double x, juce::String t = {}) { if (std::abs (x - v) > 0.001 || t != text) { v = x; text = std::move (t); repaint(); } }
        void paint (juce::Graphics& g) override { drawProgress (g, getWidth(), getHeight(), v, text.isNotEmpty() ? text : juce::String ((int) (v * 100)) + "%"); }
    private:
        double v = 0; juce::String text;
    };

    /** Small helpers to build pages quickly. */
    std::unique_ptr<juce::TextButton> makeButton (const juce::String& text, std::function<void()> onClick, bool toggle = false);
    std::unique_ptr<juce::Label> makeLabel (const juce::String& text, float size = 14, juce::Colour c = col::text2, int weight = 0);
    void styleCombo (juce::ComboBox&);
    juce::String fmtDb (double db);
    juce::String fmtTime (double seconds);
}
