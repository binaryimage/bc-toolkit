#include "Pages.h"

namespace tk
{
namespace
{
/** One channel: name, LED meter, note + cents, LUFS, hum badge. Click to focus, double-click to rename. */
class Strip : public juce::Component
{
public:
    Strip (Engine& e, Source* s) : engine (e), src (s)
    {
        addAndMakeVisible (meter); meter.setInterceptsMouseClicks (false, false);
        // link this input with its neighbour (1+2, 3+4 ... 11+12) into one stereo source, or split a pair again
        pairBtn = makeButton ("Stereo", [this] { engine.setStereoPair (src, ! engine.isStereoPair (src)); });
        pairBtn->setClickingTogglesState (false);
        addChildComponent (*pairBtn);
    }
    void update (double a4, bool analysePitch, bool analyseHum)
    {
        meter.setLevels (src->peakDb.load(), src->rmsDb.load());
        const bool canPair = engine.canPairAsStereo (src), paired = engine.isStereoPair (src);
        if (canPair != pairBtn->isVisible()) { pairBtn->setVisible (canPair); resized(); }
        pairBtn->setToggleState (paired, juce::dontSendNotification);
        pairBtn->setButtonText (paired ? "Stereo: on" : "Stereo");
        pairBtn->setTooltip (paired ? "Split back into two mono inputs" : "Link with the next input as a stereo pair (X/Y, loudness and takes in stereo)");
        if (analysePitch)
        {
            float l[4096];
            src->copyLatest (l, nullptr, 4096);
            hz = src->rmsDb.load() > -55 ? yinPitch (l, 4096, src->sr) : -1;
            a = a4;
        }
        if (analyseHum)
        {
            std::vector<float> l (16384);
            src->copyLatest (l.data(), nullptr, 16384);
            hum = detectHum (l.data(), 16384, src->sr);
        }
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (3, 2);
        drawSlab (g, r, 9, true, selected ? 1.4f : 0.8f);
        if (selected) { g.setColour (juce::Colours::white.withAlpha (0.9f)); g.drawRoundedRectangle (r.reduced (1), 9, 1.5f); }
        auto in = getLocalBounds().reduced (10, 8);
        drawEngraved (g, engine.sourceLabel (src), in.removeFromTop (18), juce::Justification::centred, font (12, 2), col::text);
        in.removeFromTop (4);
        if (pairBtn->isVisible()) in.removeFromBottom (26);
        auto bottom = in.removeFromBottom (58);
        meter.setBounds (in.withSizeKeepingCentre (18, in.getHeight()));
        // note readout
        g.setFont (font (20, 2));
        if (hz > 0)
        {
            double m = midiFromHz (hz, a); int n = (int) std::lround (m); double c = (m - n) * 100;
            g.setColour (std::abs (c) < 5 ? col::white : col::text2);
            g.drawText (noteName (n), bottom.removeFromTop (24), juce::Justification::centred);
            g.setFont (monoFont (10.5f)); g.setColour (col::text3);
            g.drawText ((c >= 0 ? "+" : "") + juce::String ((int) std::lround (c)) + "c", bottom.removeFromTop (13), juce::Justification::centred);
        }
        else
        {
            g.setColour (col::text3); g.drawText ("-", bottom.removeFromTop (24), juce::Justification::centred);
            bottom.removeFromTop (13);
        }
        g.setFont (monoFont (10.5f)); g.setColour (col::text2);
        g.drawText (fmtDb (src->lufsS.load()) + " LU", bottom.removeFromTop (13), juce::Justification::centred);
        if (hum.present)
        {
            auto badge = juce::Rectangle<float> (0, 0, 54, 15).withCentre ({ getWidth() * 0.5f, 34.0f });
            g.setColour (col::white); g.fillRoundedRectangle (badge, 4);
            g.setColour (col::black); g.setFont (font (10, 2));
            g.drawText ("HUM " + juce::String (hum.mainsHz), badge.toNearestInt(), juce::Justification::centred);
        }
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.getNumberOfClicks() == 2 && src->kind == Source::input)
        {
            auto* w = new juce::AlertWindow ("Rename channel", "What's plugged into this input?", juce::MessageBoxIconType::NoIcon);
            w->addTextEditor ("n", src->name);
            w->addButton ("OK", 1); w->addButton ("Cancel", 0);
            auto self = juce::Component::SafePointer<Strip> (this);
            w->enterModalState (true, juce::ModalCallbackFunction::create ([self, w] (int r) {
                if (r == 1 && self != nullptr)
                {
                    auto all = self->engine.sources();
                    self->engine.renameInput (all.indexOf (self->src), w->getTextEditorContents ("n"));
                    self->repaint();
                }
            }), true);
            return;
        }
        if (onPick) onPick (src);
    }
    void resized() override
    {
        pairBtn->setBounds (getLocalBounds().reduced (10, 8).removeFromBottom (24));
    }
    Engine& engine; Source* src; LedMeter meter; std::unique_ptr<juce::TextButton> pairBtn;
    bool selected = false; double hz = -1, a = 440; HumResult hum;
    std::function<void (Source*)> onPick;
};

class MetersPage : public Page
{
public:
    MetersPage (Engine& e) : Page (e, "meters", "Meters", "Every input at once. Click a channel to focus it, double-click to name it, Stereo links it with its neighbour.")
    {
        addAndMakeVisible (strips);
        strips.setViewedComponent (&stripHolder, false);
        strips.setScrollBarsShown (false, true);
        addAndMakeVisible (focus);
        const char* modes[] = { "Tuner", "Scope", "3D spectrum", "X / Y", "Loudness" };
        for (int i = 0; i < 5; ++i)
        {
            auto* b = modeBtns.add (new juce::TextButton (modes[i]));
            b->setRadioGroupId (7); b->setClickingTogglesState (true);
            b->onClick = [this, i] { mode = i; Settings::get().set ("meterMode", i); layoutFocus(); };
            focus.addAndMakeVisible (b);
        }
        mode = Settings::get().num ("meterMode", 2);
        modeBtns[mode]->setToggleState (true, juce::dontSendNotification);
        for (auto* c : std::initializer_list<juce::Component*> { &tuner, &scope, &fall, &xy, &loud }) focus.addChildComponent (c);
        // one tuning reference for the whole app (Settings has the same control): this one only shows with the tuner
        a4.setRange (415, 466, 0.5); a4.setValue (Settings::get().dbl ("a4", 440), juce::dontSendNotification);
        a4.setSliderStyle (juce::Slider::LinearHorizontal); a4.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 22);
        a4.setTextValueSuffix (" Hz");
        a4.setScrollWheelEnabled (false);                 // scrolling past it never retunes anything
        a4.setDoubleClickReturnValue (true, 440.0);       // double-click: back to 440
        a4.onValueChange = [this] { Settings::get().set ("a4", a4.getValue()); };
        focus.addChildComponent (a4);
        a4Label = makeLabel ("Tuning A4 (double-click for 440)", 12, col::text3, 2);
        focus.addChildComponent (*a4Label);
        resetBtn = makeButton ("Reset integrated", [this] { if (focused) focused->resetLoudness = true; });
        focus.addAndMakeVisible (*resetBtn);
        xyNote = makeLabel ("", 12.5f, col::text2);
        focus.addChildComponent (*xyNote);
        layoutFocus();
    }

    void shown() override { rebuild(); }

    void rebuild()
    {
        auto list = engine.sources();
        bool same = list.size() == stripObjs.size();
        for (int i = 0; same && i < list.size(); ++i) same = stripObjs[i]->src == list[i];
        if (! same)
        {
            stripObjs.clear();
            for (auto* s : list)
            {
                auto* st = stripObjs.add (new Strip (engine, s));
                st->onPick = [this] (Source* src) { setFocus (src, true); };
                stripHolder.addAndMakeVisible (st);
            }
        }
        // the channel you last focused, found by name (the list can reorder when inputs are paired or a track links)
        if (focused == nullptr || ! list.contains (focused) || engine.sourceLabel (focused) != Settings::get().str ("meterFocus"))
        {
            Source* want = nullptr;
            for (auto* s : list) if (engine.sourceLabel (s) == Settings::get().str ("meterFocus")) want = s;
            if (want == nullptr && (focused == nullptr || ! list.contains (focused))) want = list.isEmpty() ? nullptr : list[0];
            if (want != nullptr) setFocus (want, false);
        }
        for (auto* x : stripObjs) x->selected = x->src == focused;
        resized();
        // bring the focused strip into view
        for (auto* x : stripObjs) if (x->selected && ! strips.getViewArea().contains (x->getBounds())) strips.setViewPosition (juce::jmax (0, x->getX() - 20), 0);
    }

    /** focus a channel; byYou = you clicked it, so it's remembered and Record starts on it too */
    void setFocus (Source* src, bool byYou)
    {
        if (focused != src) fall.clear();
        focused = src;
        for (auto* x : stripObjs) x->selected = x->src == src;
        if (byYou && src != nullptr)
        {
            Settings::get().set ("meterFocus", engine.sourceLabel (src));
            Settings::get().set ("recSource", engine.sourceLabel (src));
        }
        repaint();
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto top = r.removeFromTop (juce::jmax (220, r.getHeight() * 38 / 100));
        strips.setBounds (top);
        int w = 104;
        stripHolder.setSize (juce::jmax (top.getWidth(), w * stripObjs.size()), top.getHeight() - 10);
        for (int i = 0; i < stripObjs.size(); ++i) stripObjs[i]->setBounds (i * w, 0, w, stripHolder.getHeight());
        r.removeFromTop (6);
        focus.setBounds (r);
        layoutFocus();
    }

    void layoutFocus()
    {
        auto r = focus.getLocalBounds().reduced (14, 12);
        auto bar = r.removeFromTop (40);
        // the five tabs share the row (narrow windows included); per-view controls sit on a row of their own
        const int tabW = juce::jmin (118, (bar.getWidth() - 4 * 4) / 5);
        for (auto* b : modeBtns) { b->setBounds (bar.removeFromLeft (tabW)); bar.removeFromLeft (4); }
        r.removeFromTop (8);
        const bool tunerOn = mode == 0, loudOn = mode == 4, xyOn = mode == 3;
        xyNote->setVisible (xyOn);
        if (xyOn) { xyNote->setBounds (r.removeFromTop (24)); r.removeFromTop (6); }
        if (tunerOn || loudOn)
        {
            auto row = r.removeFromTop (30);
            r.removeFromTop (6);
            if (loudOn) resetBtn->setBounds (row.removeFromRight (juce::jmin (160, row.getWidth())));
            if (tunerOn)
            {
                auto s = row.removeFromRight (juce::jmin (300, row.getWidth() * 2 / 3));
                a4.setBounds (s.reduced (0, 3));
                a4Label->setBounds (row.withTrimmedRight (8));
            }
        }
        resetBtn->setVisible (loudOn);
        a4.setVisible (tunerOn); a4Label->setVisible (tunerOn);
        juce::Component* views[] = { &tuner, &scope, &fall, &xy, &loud };
        for (int i = 0; i < 5; ++i) { views[i]->setBounds (r); views[i]->setVisible (i == mode); }
    }

    void paint (juce::Graphics&) override {}

    void tick() override
    {
        ++frame;
        if (frame % 15 == 0 && ! a4.isMouseButtonDown())
        {
            const double want = Settings::get().dbl ("a4", 440);
            if (std::abs (want - a4.getValue()) > 0.01) a4.setValue (want, juce::dontSendNotification);
        }
        int n = stripObjs.size();
        for (int i = 0; i < n; ++i)
            stripObjs[i]->update (a4.getValue(), n > 0 && (frame % juce::jmax (1, n / 3 + 1)) == (i % juce::jmax (1, n / 3 + 1)), (frame + i * 7) % 45 == 0);
        if (focused == nullptr) return;
        if (focusedLbl != engine.sourceLabel (focused)) { focusedLbl = engine.sourceLabel (focused); }
        static std::vector<float> L (8192), R (8192);
        focused->copyLatest (L.data(), R.data(), 8192);
        double sr = focused->sr;
        switch (mode)
        {
            case 0: tuner.setPitch (focused->rmsDb.load() > -55 ? yinPitch (L.data() + 4096, 4096, sr) : -1, a4.getValue()); break;
            case 1: { double hz = yinPitch (L.data() + 4096, 4096, sr); scope.setData (L.data() + 4096, 4096, hz > 0 ? sr / hz : 0); break; }
            case 2: if (frame % 2 == 0) { auto& m = spec.analyse (L.data()); fall.push (m, spec.binHz (sr)); } break;
            case 3:
                xy.setData (L.data() + 6144, R.data() + 6144, 2048);
                xyNote->setText (focused->channels > 1 ? engine.sourceLabel (focused) + ": left vs right"
                                 : engine.sourceLabel (focused) + " is one mono channel, so X/Y is a straight line. "
                                   + (engine.canPairAsStereo (focused) ? "Press Stereo on it to link it with its neighbour." : juce::String()),
                                 juce::dontSendNotification);
                break;
            default: loud.repaint();
        }
    }

    void demo() override { rebuild(); if (stripObjs.size() > 0) stripObjs[0]->onPick (stripObjs[0]->src); }

    struct LoudView : public Screen
    {
        MetersPage& p;
        explicit LoudView (MetersPage& pg) : p (pg) {}
        void paintScreen (juce::Graphics& g, juce::Rectangle<float> r) override
        {
            if (p.focused == nullptr) return;
            auto* s = p.focused;
            struct Row { const char* label; double v; };
            Row rows[] = { { "MOMENTARY", s->lufsM.load() }, { "SHORT-TERM", s->lufsS.load() }, { "INTEGRATED", s->lufsI.load() }, { "TRUE PEAK", s->truePeak.load() } };
            float w = r.getWidth() / 4;
            for (int i = 0; i < 4; ++i)
            {
                auto cell = juce::Rectangle<float> (r.getX() + w * i, r.getY() + 10, w, r.getHeight() * 0.5f);
                drawEngraved (g, rows[i].label, cell.toNearestInt().removeFromTop (20), juce::Justification::centred, font (12, 2), col::text3);
                g.setFont (font (cell.getHeight() * 0.42f, i == 2 ? 2 : 0)); g.setColour (i == 2 ? col::white : col::text);
                g.drawText (fmtDb (rows[i].v), cell.toNearestInt().withTrimmedTop (20), juce::Justification::centred);
                g.setFont (font (12)); g.setColour (col::text3);
                g.drawText (i == 3 ? "dBTP" : "LUFS", cell.toNearestInt().removeFromBottom (18), juce::Justification::centred);
            }
            // targets ruler
            auto ruler = r.removeFromBottom (r.getHeight() * 0.32f).reduced (30, 10).withTrimmedBottom (24);   // room for two label rows
            g.setColour (col::grid); g.drawHorizontalLine ((int) ruler.getCentreY(), ruler.getX(), ruler.getRight());
            auto xOf = [&] (double lufs) { return ruler.getX() + (float) ((lufs + 30) / 26.0) * ruler.getWidth(); };
            struct T { const char* n; double v; };
            // -16 and -14 sit close together: -16's label hangs left of its line, -14's right of its line, on separate rows
            T targets[] = { { "Apple Music -16", -16 }, { "Spotify / YouTube / Tidal -14", -14 }, { "Loud club master -8", -8 } };
            g.setFont (font (11));
            for (int k = 0; k < 3; ++k)
            {
                const auto& t = targets[k];
                const float x = xOf (t.v);
                g.setColour (col::text3); g.drawVerticalLine ((int) x, ruler.getY(), ruler.getBottom() + (k == 1 ? 14.0f : 0.0f));
                const int tw = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), t.n) + 6;
                const int y = (int) ruler.getBottom() + (k == 1 ? 14 : 0);
                if (k == 0)      g.drawText (t.n, (int) x - tw, y, tw, 14, juce::Justification::centredRight);
                else if (k == 1) g.drawText (t.n, (int) x + 4, y, tw, 14, juce::Justification::centredLeft);
                else             g.drawText (t.n, juce::jmin ((int) x - tw / 2, (int) r.getRight() - tw), y, tw, 14, juce::Justification::centred);
            }
            double I = s->lufsI.load();
            if (I > -60)
            {
                float x = xOf (juce::jlimit (-30.0, -4.0, I));
                juce::Path tri; tri.addTriangle (x - 8, ruler.getY() - 4, x + 8, ruler.getY() - 4, x, ruler.getCentreY());
                g.setColour (col::white); g.fillPath (tri);
            }
        }
    };

    juce::Viewport strips; juce::Component stripHolder;
    juce::OwnedArray<Strip> stripObjs;
    Slab focus;
    juce::OwnedArray<juce::TextButton> modeBtns;
    TunerDial tuner; Scope scope; Waterfall3D fall; XYView xy; LoudView loud { *this };
    juce::Slider a4; std::unique_ptr<juce::Label> a4Label, xyNote; std::unique_ptr<juce::TextButton> resetBtn;
    Spectrum spec { 13 };
    Source* focused = nullptr; juce::String focusedLbl;
    int mode = 2; juce::int64 frame = 0;
};
}

std::unique_ptr<Page> makeMetersPage (Engine& e) { return std::make_unique<MetersPage> (e); }
}
