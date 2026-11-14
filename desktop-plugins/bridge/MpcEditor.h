// The plugin window: the same tabs, controls and skin art as the plugin's MPC screen.
// It reads the plugin's MPC skin (TUI.json + its images, built into the plugin) and draws it,
// so a change to the MPC skin shows up here too.
#pragma once
#include "MpcBridge.h"

namespace bcp
{
/** The MPC skin, parsed: tabs of drawable elements in MPC screen coordinates (1280 x 628 per tab). */
struct Skin
{
    enum class Kind { Focus, KnobStrip, KnobGeneric, Label, Button, Slider };
    enum class LabelType { Name, Value, Text };
    struct Element
    {
        Kind kind = Kind::Label;
        juce::Rectangle<int> bounds;
        int param = -1;                         // parameter index, -1 = none
        int slot = -1;                          // which control slot this belongs to (focus + knob + labels share one)
        // filmstrip knobs and faders
        juce::Image strip; int frames = 1; bool horizontal = false; bool invert = false; bool bipolar = false;
        // labels
        LabelType labelType = LabelType::Text; juce::String text; juce::Colour colour { 0xffffffff };
        float fontHeight = 26.0f; juce::Justification just { juce::Justification::centredLeft };
        // buttons
        juce::Image onImage, offImage; int buttonId = 0, groupSize = 1;
        // focus
        juce::Colour focusFill { 0x19ffffff }, focusOutline { 0xffd31145 };
        // the slider widget opens a menu of the choices
        bool menu = false;
        // IndexedEnabling: shown only while parameter visParam sits at step visIndex of visSteps
        int visParam = -1, visIndex = 0, visSteps = 2;
    };
    struct Tab
    {
        juce::String name;
        juce::Colour background { 0xff1e1d1e };
        juce::Image backgroundImage;
        std::vector<Element> elements;
        int numSlots = 0;
    };
    std::vector<Tab> tabs;
    juce::Colour accent { 0xffd31145 }, text { 0xffffffff }, muted { 0xff8a8a8c };
    juce::StringArray problems;     // anything in the skin that couldn't be drawn (missing image, unknown widget)

    /** loads TUI.json and its images from the plugin's built-in skin data */
    void load();
    static juce::Image image (const juce::String& file);
};

/** One tab's 1280 x 628 MPC page, scaled to fit. */
class PageView : public juce::Component, public juce::SettableTooltipClient
{
public:
    PageView (BridgeProcessor& p, Skin& s) : proc (p), skin (s) {}
    void setTab (int t) { tab = t; hoverSlot = dragSlot = -1; repaint(); }
    int getTab() const { return tab; }
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool visible (const Skin::Element& e) const;
    /** the element under a point (in MPC coordinates) that does something, or nullptr */
    const Skin::Element* hit (juce::Point<float> mpcPoint) const;
    float scale() const { return (float) getWidth() / (float) kTabW; }
private:
    BridgeProcessor& proc;
    Skin& skin;
    int tab = 0, hoverSlot = -1, dragSlot = -1;
    const Skin::Element* dragEl = nullptr;
    float dragStartValue = 0;
    juce::Point<float> dragStart;
    juce::Point<float> toMpc (juce::Point<float> p) const { return p / scale(); }
    void drawElement (juce::Graphics&, const Skin::Element&);
};

class BridgeEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit BridgeEditor (BridgeProcessor&);
    ~BridgeEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void showTab (int t);
    int numTabs() const { return (int) skin.tabs.size(); }
    const Skin& getSkin() const { return skin; }
    PageView& getPage() { return page; }

    static constexpr int kHeaderH = 52, kTabBarH = 40;
    static constexpr int baseWidth() { return kTabW; }
    static constexpr int baseHeight() { return kHeaderH + kTabBarH + kTabH; }
private:
    BridgeProcessor& proc;
    Skin skin;
    PageView page;
    juce::OwnedArray<juce::TextButton> tabButtons;
    juce::ComboBox presets;
    juce::TextButton prevBtn { "<" }, nextBtn { ">" };
    juce::TooltipWindow tooltips { this, 600 };
    juce::ComponentBoundsConstrainer constrainer;
    int lastProgramVersion = -1;
    std::vector<float> lastValues;
    void timerCallback() override;
    void refreshPresets();
    std::unique_ptr<juce::LookAndFeel_V4> lnf;
};
}
