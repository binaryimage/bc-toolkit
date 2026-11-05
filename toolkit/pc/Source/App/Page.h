#pragma once
#include "Common/JuceHeader.h"
#include "Common/Look.h"
#include "Common/Widgets.h"
#include "Common/Files.h"
#include "Engine.h"
#include "Settings.h"

namespace tk
{
    class MainComponent;

    /** Base for every screen. Pages are laid out on the black background with Slabs. */
    class Page : public juce::Component
    {
    public:
        Page (Engine& e, juce::String id, juce::String title, juce::String blurb)
            : engine (e), id (std::move (id)), title (std::move (title)), blurb (std::move (blurb)) {}
        virtual void tick() {}                    // ~30 Hz while visible
        virtual void demo() {}                    // fill with example content (screenshots)
        virtual void shown() {}
        /** audio handed over from another page (Record -> Chop etc.) */
        virtual void receiveAudio (const juce::AudioBuffer<float>&, double, const juce::String&) {}
        Engine& engine;
        juce::String id, title, blurb;
        MainComponent* main = nullptr;
    };

    /** ComboBox listing every active source (interface inputs, Ableton tracks, PC audio). */
    class SourcePicker : public juce::ComboBox
    {
    public:
        SourcePicker (Engine& e, juce::String key) : engine (e), settingsKey (std::move (key)) { refresh(); onChange = [this] { remember(); }; }
        void refresh();
        Source* selected() const;
        void remember();
    private:
        Engine& engine;
        juce::String settingsKey;
        juce::Array<Source*> list;
    };

    /** ComboBox of MIDI outputs (or inputs), remembering the choice. */
    class MidiPicker : public juce::ComboBox
    {
    public:
        MidiPicker (bool outputs, juce::String key) : outs (outputs), settingsKey (std::move (key)) { refresh(); onChange = [this] { remember(); }; }
        void refresh();
        juce::String id() const;
        void remember();
        /** Windows' GM synth, a controller's control/DAW port and the like: never picked by default */
        static bool isUtilityPort (const juce::String& name);
    private:
        bool outs; juce::String settingsKey;
        juce::Array<juce::MidiDeviceInfo> list;
    };

    /** Note-name combo (C0..C8). */
    void fillNotes (juce::ComboBox&, int lo = 12, int hi = 108);
    /** Lay out a row of components with fixed widths, left to right. */
    void rowLayout (juce::Rectangle<int> r, std::initializer_list<std::pair<juce::Component*, int>> items, int gap = 8);
    /** Like rowLayout, but wraps onto more rows instead of running off the edge (narrow windows).
        Lays out from the top of r; returns the height used. Pass place=false to only measure. */
    int flowLayout (juce::Rectangle<int> r, std::initializer_list<std::pair<juce::Component*, int>> items, int rowHeight, int gap = 8, bool place = true);
}
