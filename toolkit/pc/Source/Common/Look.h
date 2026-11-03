#pragma once
#include "JuceHeader.h"

/** Black and white, but 3D: raised slabs with bevels and shadows, recessed screens, machined knobs. */
namespace tk
{
    namespace col
    {
        const juce::Colour bg { 0xff090909 }, slabTop { 0xff202020 }, slabBottom { 0xff131313 },
                           screen { 0xff030303 }, line { 0x1fffffff }, grid { 0x14ffffff },
                           text { 0xffeeeeee }, text2 { 0xb3ffffff }, text3 { 0x73ffffff }, faint { 0x26ffffff },
                           white { 0xffffffff }, black { 0xff000000 };
    }

    juce::Font font (float height, int weight = 0);  // 0 regular, 1 medium, 2 semibold, 3 mono
    juce::Font monoFont (float height);

    /** A raised slab: drop shadow, vertical gradient, light top edge, dark bottom edge. */
    void drawSlab (juce::Graphics& g, juce::Rectangle<float> r, float radius = 10.0f, bool raised = true, float lift = 1.0f);
    /** A recessed display: dark well with an inner shadow along the top and a light lip at the bottom. */
    void drawScreen (juce::Graphics& g, juce::Rectangle<float> r, float radius = 8.0f);
    /** A 3D key cap: white when on (lit), dark grey when off. */
    void drawKey (juce::Graphics& g, juce::Rectangle<float> r, bool on, bool down, bool hover, float radius = 7.0f);
    /** Engraved label: dark offset under light text, like lettering pressed into metal. */
    void drawEngraved (juce::Graphics& g, const juce::String& s, juce::Rectangle<int> r, juce::Justification j,
                       const juce::Font& f, juce::Colour c = col::text3);

    /** Pill-shaped progress bar (0..1). */
    void drawProgress (juce::Graphics& g, int w, int h, double progress, const juce::String& text);

    class Look : public juce::LookAndFeel_V4
    {
    public:
        Look();
        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool hover, bool down) override;
        void drawButtonText (juce::Graphics&, juce::TextButton&, bool hover, bool down) override;
        juce::Font getTextButtonFont (juce::TextButton&, int h) override;
        void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool hover, bool down) override;
        void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
        void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                               juce::Slider::SliderStyle, juce::Slider&) override;
        void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
        juce::Font getComboBoxFont (juce::ComboBox&) override;
        void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
        void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
        void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>&, bool sep, bool active, bool highlighted, bool ticked,
                                bool hasSub, const juce::String& text, const juce::String& shortcut, const juce::Drawable* icon,
                                const juce::Colour* textColour) override;
        juce::Font getPopupMenuFont() override;
        void fillTextEditorBackground (juce::Graphics&, int w, int h, juce::TextEditor&) override;
        void drawTextEditorOutline (juce::Graphics&, int w, int h, juce::TextEditor&) override;
        void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int thumbStart,
                            int thumbSize, bool over, bool down) override;
        void drawTableHeaderBackground (juce::Graphics&, juce::TableHeaderComponent&) override;
        void drawTableHeaderColumn (juce::Graphics&, juce::TableHeaderComponent&, const juce::String& name, int id, int w, int h,
                                    bool over, bool down, int flags) override;
        void drawProgressBar (juce::Graphics&, juce::ProgressBar&, int w, int h, double progress, const juce::String& text) override;
        void drawLabel (juce::Graphics&, juce::Label&) override;
        void drawAlertBox (juce::Graphics&, juce::AlertWindow&, const juce::Rectangle<int>& textArea, juce::TextLayout&) override;
    };
}
