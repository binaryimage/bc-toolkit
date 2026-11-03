#include "Look.h"

namespace tk
{
static juce::Typeface::Ptr face (int w)
{
    static juce::Typeface::Ptr f[4];
    if (f[w] == nullptr)
    {
        switch (w)
        {
            case 1: f[1] = juce::Typeface::createSystemTypefaceFor (BinaryData::IBMPlexSansMedium_ttf, BinaryData::IBMPlexSansMedium_ttfSize); break;
            case 2: f[2] = juce::Typeface::createSystemTypefaceFor (BinaryData::IBMPlexSansSemiBold_ttf, BinaryData::IBMPlexSansSemiBold_ttfSize); break;
            case 3: f[3] = juce::Typeface::createSystemTypefaceFor (BinaryData::IBMPlexMonoMedium_ttf, BinaryData::IBMPlexMonoMedium_ttfSize); break;
            default: f[0] = juce::Typeface::createSystemTypefaceFor (BinaryData::IBMPlexSansRegular_ttf, BinaryData::IBMPlexSansRegular_ttfSize);
        }
    }
    return f[w];
}

juce::Font font (float h, int w) { return juce::Font (juce::FontOptions (face (juce::jlimit (0, 3, w))).withHeight (h)); }
juce::Font monoFont (float h) { return font (h, 3); }

// ------------------------------------------------------------ shapes

void drawSlab (juce::Graphics& g, juce::Rectangle<float> r, float rad, bool raised, float lift)
{
    if (raised)
    {
        // soft drop shadow below the slab
        juce::Path sp; sp.addRoundedRectangle (r.translated (0, 3.0f * lift), rad);
        juce::DropShadow (juce::Colours::black.withAlpha (0.85f), (int) (14 * lift), { 0, (int) (5 * lift) }).drawForPath (g, sp);
    }
    juce::ColourGradient grad (raised ? col::slabTop : juce::Colour (0xff101010), r.getX(), r.getY(),
                               raised ? col::slabBottom : juce::Colour (0xff1a1a1a), r.getX(), r.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (r, rad);
    // bevel: light top edge, dark bottom edge
    juce::Path top; top.addRoundedRectangle (r.reduced (0.5f), rad);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (raised ? 0.16f : 0.03f), r.getX(), r.getY(),
                                             juce::Colours::white.withAlpha (0.0f), r.getX(), r.getY() + juce::jmin (r.getHeight(), 24.0f), false));
    g.strokePath (top, juce::PathStrokeType (1.0f));
    g.setColour (juce::Colours::black.withAlpha (0.9f));
    g.drawRoundedRectangle (r.expanded (0.5f), rad + 0.5f, 1.0f);
}

void drawScreen (juce::Graphics& g, juce::Rectangle<float> r, float rad)
{
    g.setColour (col::screen);
    g.fillRoundedRectangle (r, rad);
    // inner shadow from the top lip
    juce::Graphics::ScopedSaveState s (g);
    juce::Path clip; clip.addRoundedRectangle (r, rad);
    g.reduceClipRegion (clip);
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.9f), r.getX(), r.getY(),
                                             juce::Colours::transparentBlack, r.getX(), r.getY() + 14, false));
    g.fillRect (r.withHeight (14));
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.035f), r.getCentreX(), r.getY(),
                                             juce::Colours::transparentWhite, r.getCentreX(), r.getBottom(), false));
    g.fillRect (r);
    g.setColour (juce::Colours::white.withAlpha (0.09f));
    g.drawLine (r.getX() + rad, r.getBottom() - 0.5f, r.getRight() - rad, r.getBottom() - 0.5f, 1.0f);
    g.setColour (juce::Colours::black);
    g.drawRoundedRectangle (r.reduced (0.5f), rad, 1.0f);
}

void drawKey (juce::Graphics& g, juce::Rectangle<float> r, bool on, bool down, bool hover, float rad)
{
    if (! down)
    {
        juce::Path sp; sp.addRoundedRectangle (r.translated (0, 2), rad);
        juce::DropShadow (juce::Colours::black.withAlpha (0.9f), 8, { 0, 3 }).drawForPath (g, sp);
    }
    auto face = down ? r.translated (0, 1.5f) : r;
    juce::Colour top = on ? juce::Colour (0xfff4f4f4) : juce::Colour (hover ? 0xff303030 : 0xff272727);
    juce::Colour bot = on ? juce::Colour (0xffbdbdbd) : juce::Colour (0xff151515);
    g.setGradientFill (juce::ColourGradient (top, face.getX(), face.getY(), bot, face.getX(), face.getBottom(), false));
    g.fillRoundedRectangle (face, rad);
    // lit keys glow a little
    if (on)
    {
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.drawRoundedRectangle (face.expanded (1.5f), rad + 1.5f, 2.0f);
    }
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (on ? 0.9f : 0.22f), face.getX(), face.getY(),
                                             juce::Colours::transparentWhite, face.getX(), face.getY() + face.getHeight() * 0.5f, false));
    g.drawRoundedRectangle (face.reduced (0.5f), rad, 1.0f);
    g.setColour (juce::Colours::black);
    g.drawRoundedRectangle (face.expanded (0.5f), rad + 0.5f, 1.0f);
}

void drawEngraved (juce::Graphics& g, const juce::String& s, juce::Rectangle<int> r, juce::Justification j, const juce::Font& f, juce::Colour c)
{
    g.setFont (f);
    g.setColour (juce::Colours::black);
    g.drawText (s, r.translated (0, 1), j, true);
    g.setColour (c);
    g.drawText (s, r, j, true);
}

// ------------------------------------------------------------ LookAndFeel

Look::Look()
{
    setDefaultSansSerifTypeface (face (0));
    setColour (juce::ResizableWindow::backgroundColourId, col::bg);
    setColour (juce::DocumentWindow::backgroundColourId, col::bg);
    setColour (juce::TextButton::textColourOffId, col::text);
    setColour (juce::TextButton::textColourOnId, col::black);
    setColour (juce::Label::textColourId, col::text);
    setColour (juce::TextEditor::textColourId, col::text);
    setColour (juce::TextEditor::highlightColourId, juce::Colours::white.withAlpha (0.25f));
    setColour (juce::TextEditor::highlightedTextColourId, col::white);
    setColour (juce::CaretComponent::caretColourId, col::white);
    setColour (juce::ComboBox::textColourId, col::text);
    setColour (juce::ComboBox::arrowColourId, col::text2);
    setColour (juce::PopupMenu::textColourId, col::text);
    setColour (juce::PopupMenu::highlightedTextColourId, col::black);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, col::white);
    setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::ListBox::textColourId, col::text);
    setColour (juce::TableListBox::backgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::ScrollBar::thumbColourId, juce::Colours::white.withAlpha (0.3f));
    setColour (juce::Slider::textBoxTextColourId, col::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::ToggleButton::textColourId, col::text);
    setColour (juce::ToggleButton::tickColourId, col::black);
    setColour (juce::AlertWindow::backgroundColourId, juce::Colour (0xff161616));
    setColour (juce::AlertWindow::textColourId, col::text);
    setColour (juce::AlertWindow::outlineColourId, col::line);
    setColour (juce::ProgressBar::foregroundColourId, col::white);
    setColour (juce::ProgressBar::backgroundColourId, col::screen);
    setColour (juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour (0xffe9e9e9));
    setColour (juce::MidiKeyboardComponent::blackNoteColourId, juce::Colour (0xff111111));
    setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, juce::Colour (0xff777777));
    setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, juce::Colours::black.withAlpha (0.35f));
    setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, juce::Colours::black.withAlpha (0.12f));
    setColour (juce::MidiKeyboardComponent::shadowColourId, juce::Colours::black.withAlpha (0.5f));
    setColour (juce::MidiKeyboardComponent::upDownButtonBackgroundColourId, col::slabBottom);
    setColour (juce::MidiKeyboardComponent::upDownButtonArrowColourId, col::text2);
    setColour (juce::TreeView::backgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::DirectoryContentsDisplayComponent::highlightColourId, juce::Colours::white.withAlpha (0.2f));
    setColour (juce::FileChooserDialogBox::titleTextColourId, col::text);
}

void Look::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool hover, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (2.0f, 3.0f);
    drawKey (g, r, b.getToggleState(), down, hover, juce::jmin (8.0f, r.getHeight() * 0.3f));
    if (! b.isEnabled())
    {
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRoundedRectangle (r, 7.0f);
    }
}

juce::Font Look::getTextButtonFont (juce::TextButton&, int h) { return font (juce::jmin (15.0f, h * 0.42f), 1); }

void Look::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool down)
{
    auto r = b.getLocalBounds().reduced (6, 3);
    if (down) r = r.translated (0, 1);
    g.setFont (getTextButtonFont (b, b.getHeight()));
    if (! b.getToggleState())
    {
        g.setColour (juce::Colours::black);
        g.drawFittedText (b.getButtonText(), r.translated (0, 1), juce::Justification::centred, 2);
    }
    g.setColour (b.getToggleState() ? col::black : (b.isEnabled() ? col::text : col::text3));
    g.drawFittedText (b.getButtonText(), r, juce::Justification::centred, 2);
}

void Look::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool hover, bool down)
{
    auto r = b.getLocalBounds().toFloat();
    auto sw = r.removeFromLeft (40).withSizeKeepingCentre (36, 20);
    drawScreen (g, sw, 10);
    bool on = b.getToggleState();
    if (on)
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe8e8e8), sw.getX(), sw.getY(), juce::Colour (0xff9c9c9c), sw.getX(), sw.getBottom(), false));
        g.fillRoundedRectangle (sw.reduced (2), 8);
    }
    auto knob = juce::Rectangle<float> (16, 16).withCentre ({ on ? sw.getRight() - 10 : sw.getX() + 10, sw.getCentreY() });
    drawKey (g, knob, false, down, hover, 8);
    g.setFont (font (14));
    g.setColour (b.isEnabled() ? col::text : col::text3);
    g.drawText (b.getButtonText(), r.reduced (6, 0).toNearestInt(), juce::Justification::centredLeft);
}

void Look::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&)
{
    auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (6);
    float d = juce::jmin (r.getWidth(), r.getHeight());
    auto c = r.withSizeKeepingCentre (d, d);
    // track ring engraved into the panel
    float ang = a0 + pos * (a1 - a0);
    juce::Path ring; ring.addCentredArc (c.getCentreX(), c.getCentreY(), d / 2, d / 2, 0, a0, a1, true);
    g.setColour (juce::Colours::black); g.strokePath (ring, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    juce::Path val; val.addCentredArc (c.getCentreX(), c.getCentreY(), d / 2, d / 2, 0, a0, ang, true);
    g.setColour (juce::Colours::white.withAlpha (0.85f)); g.strokePath (val, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    // the knob body: shadow, machined gradient, bevel
    auto k = c.reduced (d * 0.16f);
    juce::Path kp; kp.addEllipse (k);
    juce::DropShadow (juce::Colours::black, 10, { 0, 4 }).drawForPath (g, kp);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3a3a), k.getCentreX(), k.getY(), juce::Colour (0xff0f0f0f), k.getCentreX(), k.getBottom(), false));
    g.fillEllipse (k);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.35f), k.getCentreX(), k.getY(), juce::Colours::transparentWhite, k.getCentreX(), k.getCentreY(), false));
    g.drawEllipse (k.reduced (0.5f), 1.0f);
    auto inner = k.reduced (k.getWidth() * 0.18f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff141414), inner.getCentreX(), inner.getY(), juce::Colour (0xff2b2b2b), inner.getCentreX(), inner.getBottom(), false));
    g.fillEllipse (inner);
    // pointer
    juce::Point<float> p1 (k.getCentreX() + std::sin (ang) * k.getWidth() * 0.15f, k.getCentreY() - std::cos (ang) * k.getWidth() * 0.15f);
    juce::Point<float> p2 (k.getCentreX() + std::sin (ang) * k.getWidth() * 0.44f, k.getCentreY() - std::cos (ang) * k.getWidth() * 0.44f);
    g.setColour (col::white);
    g.drawLine ({ p1, p2 }, 2.5f);
}

void Look::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle st, juce::Slider& s)
{
    if (st != juce::Slider::LinearHorizontal && st != juce::Slider::LinearVertical)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0, 0, st, s);
        return;
    }
    bool horiz = st == juce::Slider::LinearHorizontal;
    auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    auto slot = horiz ? r.withSizeKeepingCentre (r.getWidth(), 6) : r.withSizeKeepingCentre (6, r.getHeight());
    drawScreen (g, slot, 3);
    g.setColour (juce::Colours::white.withAlpha (0.7f));
    if (horiz) g.fillRoundedRectangle (slot.withRight (pos).reduced (1.5f), 2);
    else g.fillRoundedRectangle (slot.withTop (pos).reduced (1.5f), 2);
    auto cap = horiz ? juce::Rectangle<float> (14, juce::jmin (26.0f, r.getHeight() - 2)).withCentre ({ pos, r.getCentreY() })
                     : juce::Rectangle<float> (juce::jmin (30.0f, r.getWidth() - 2), 14).withCentre ({ r.getCentreX(), pos });
    drawKey (g, cap, false, false, false, 3);
    g.setColour (col::white);
    if (horiz) g.drawLine (cap.getCentreX(), cap.getY() + 4, cap.getCentreX(), cap.getBottom() - 4, 1.5f);
    else g.drawLine (cap.getX() + 4, cap.getCentreY(), cap.getRight() - 4, cap.getCentreY(), 1.5f);
}

void Look::drawComboBox (juce::Graphics& g, int w, int h, bool down, int, int, int, int, juce::ComboBox& cb)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (1.5f, 2.5f);
    drawKey (g, r, false, down, cb.isMouseOver (true), 7);
    auto a = juce::Rectangle<float> ((float) w - 22, 0, 12, (float) h);
    juce::Path p; p.addTriangle (a.getX(), a.getCentreY() - 3, a.getRight(), a.getCentreY() - 3, a.getCentreX(), a.getCentreY() + 4);
    g.setColour (col::text2); g.fillPath (p);
}

juce::Font Look::getComboBoxFont (juce::ComboBox&) { return font (14, 1); }
void Look::positionComboBoxText (juce::ComboBox& cb, juce::Label& l)
{
    l.setBounds (8, 1, cb.getWidth() - 30, cb.getHeight() - 2);
    l.setFont (getComboBoxFont (cb));
}

void Look::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (juce::Colour (0xff151515));
    g.setColour (col::line); g.drawRect (0, 0, w, h);
}

juce::Font Look::getPopupMenuFont() { return font (15); }

void Look::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool sep, bool active, bool hl, bool ticked, bool,
                              const juce::String& text, const juce::String& shortcut, const juce::Drawable*, const juce::Colour*)
{
    if (sep) { g.setColour (col::line); g.fillRect (area.reduced (8, 0).withHeight (1).withY (area.getCentreY())); return; }
    auto r = area.reduced (3, 1);
    if (hl && active) { g.setColour (col::white); g.fillRoundedRectangle (r.toFloat(), 4); }
    g.setColour (hl && active ? col::black : (active ? col::text : col::text3));
    g.setFont (getPopupMenuFont());
    g.drawText ((ticked ? juce::String (juce::CharPointer_UTF8 ("\xe2\x9c\x93  ")) : "     ") + text, r.reduced (8, 0), juce::Justification::centredLeft);
    if (shortcut.isNotEmpty()) g.drawText (shortcut, r.reduced (8, 0), juce::Justification::centredRight);
}

void Look::fillTextEditorBackground (juce::Graphics& g, int w, int h, juce::TextEditor&)
{
    drawScreen (g, { 0, 0, (float) w, (float) h }, 6);
}

void Look::drawTextEditorOutline (juce::Graphics& g, int w, int h, juce::TextEditor& t)
{
    if (t.hasKeyboardFocus (true)) { g.setColour (juce::Colours::white.withAlpha (0.35f)); g.drawRoundedRectangle (0.5f, 0.5f, w - 1.0f, h - 1.0f, 6, 1); }
}

void Look::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int ts, int tsz, bool over, bool down)
{
    auto t = vertical ? juce::Rectangle<int> (x + 2, ts, w - 4, tsz) : juce::Rectangle<int> (ts, y + 2, tsz, h - 4);
    g.setColour (juce::Colours::white.withAlpha (down ? 0.5f : over ? 0.35f : 0.2f));
    g.fillRoundedRectangle (t.toFloat().reduced (1), 3);
}

void Look::drawTableHeaderBackground (juce::Graphics& g, juce::TableHeaderComponent& h)
{
    auto r = h.getLocalBounds().toFloat();
    g.setGradientFill (juce::ColourGradient (col::slabTop, 0, 0, col::slabBottom, 0, r.getBottom(), false));
    g.fillRect (r);
    g.setColour (juce::Colours::black); g.fillRect (r.removeFromBottom (1));
}

void Look::drawTableHeaderColumn (juce::Graphics& g, juce::TableHeaderComponent&, const juce::String& name, int, int w, int h, bool over, bool, int flags)
{
    g.setColour (juce::Colours::black.withAlpha (0.6f)); g.fillRect (w - 1, 4, 1, h - 8);
    juce::String arrow = (flags & juce::TableHeaderComponent::sortedForwards) ? juce::String (juce::CharPointer_UTF8 (" \xe2\x96\xb4"))
                       : (flags & juce::TableHeaderComponent::sortedBackwards) ? juce::String (juce::CharPointer_UTF8 (" \xe2\x96\xbe")) : juce::String();
    drawEngraved (g, name.toUpperCase() + arrow, { 8, 0, w - 12, h }, juce::Justification::centredLeft, font (11.5f, 1), over ? col::text : col::text2);
}

void Look::drawProgressBar (juce::Graphics& g, juce::ProgressBar&, int w, int h, double p, const juce::String& text) { drawProgress (g, w, h, p, text); }

void drawProgress (juce::Graphics& g, int w, int h, double p, const juce::String& text)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h);
    drawScreen (g, r, h * 0.5f);
    if (p >= 0 && p <= 1)
    {
        auto f = r.reduced (3).withWidth ((float) (juce::jmax (0.0, (w - 6) * p)));
        g.setGradientFill (juce::ColourGradient (col::white, 0, f.getY(), juce::Colour (0xffaaaaaa), 0, f.getBottom(), false));
        g.fillRoundedRectangle (f, (h - 6) * 0.5f);
    }
    if (text.isNotEmpty())
    {
        g.setFont (font (12, 1));
        g.setColour (p > 0.5 ? col::black : col::text);
        g.drawText (text, r.toNearestInt(), juce::Justification::centred);
    }
}

void Look::drawLabel (juce::Graphics& g, juce::Label& l)
{
    if (l.isBeingEdited()) return;
    g.setFont (getLabelFont (l));
    g.setColour (l.findColour (juce::Label::textColourId).withMultipliedAlpha (l.isEnabled() ? 1.0f : 0.5f));
    g.drawFittedText (l.getText(), l.getBorderSize().subtractedFrom (l.getLocalBounds()), l.getJustificationType(),
                      juce::jmax (1, (int) ((float) l.getHeight() / g.getCurrentFont().getHeight())), l.getMinimumHorizontalScale());
}

void Look::drawAlertBox (juce::Graphics& g, juce::AlertWindow& a, const juce::Rectangle<int>& textArea, juce::TextLayout& tl)
{
    auto r = a.getLocalBounds().toFloat();
    g.fillAll (juce::Colour (0xff141414));
    g.setColour (col::line); g.drawRect (r, 1.0f);
    tl.draw (g, textArea.toFloat());
}
}
