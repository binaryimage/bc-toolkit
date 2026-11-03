#include "Pages.h"
#include <algorithm>

namespace tk
{
namespace
{
/** Median pitch over a few windows, measured live from a source. Runs on a worker thread. */
static double measurePitch (juce::Thread& th, Source* s, double seconds)
{
    std::vector<double> hz;
    std::vector<float> buf (4096);
    auto end = juce::Time::getMillisecondCounterHiRes() + seconds * 1000;
    while (juce::Time::getMillisecondCounterHiRes() < end && ! th.threadShouldExit())
    {
        th.wait (60);
        s->copyLatest (buf.data(), nullptr, 4096);
        if (rmsDb (buf.data(), 4096) < -60) continue;
        double h = yinPitch (buf.data(), 4096, s->sr);
        if (h > 0) hz.push_back (h);
    }
    if (hz.size() < 3) return -1;
    std::sort (hz.begin(), hz.end());
    return hz[hz.size() / 2];
}

class GearCheckPage : public Page, private juce::Thread
{
public:
    GearCheckPage (Engine& e) : Page (e, "gear", "Gear check", "Warm-up tuning for every synth, round-trip latency, and 2600 VCO tracking."),
                                juce::Thread ("gear check")
    {
        const char* tabs[] = { "Warm-up check", "Latency", "2600 tracking" };
        for (int i = 0; i < 3; ++i)
        {
            auto* b = tabBtns.add (new juce::TextButton (tabs[i]));
            b->setRadioGroupId (21); b->setClickingTogglesState (true);
            b->onClick = [this, i] { tab = i; layout(); };
            addAndMakeVisible (b);
        }
        tabBtns[0]->setToggleState (true, juce::dontSendNotification);
        addAndMakeVisible (panel);
        addAndMakeVisible (results);
        // warm-up rows
        const char* defaults[] = { "Hydrasynth", "Leviasynth", "MiniFreak", "Messenger", "2600", "Synth 6" };
        for (int i = 0; i < 6; ++i)
        {
            auto* r = rows.add (new Row (engine, i, defaults[i]));
            panel.addChildComponent (r);
        }
        // latency + tracking controls
        for (int c = 1; c <= 16; ++c) { latChan.addItem ("Ch " + juce::String (c), c); trkChan.addItem ("Ch " + juce::String (c), c); }
        latChan.setSelectedId (Settings::get().num ("latChan", 1), juce::dontSendNotification);
        trkChan.setSelectedId (Settings::get().num ("trkChan", 1), juce::dontSendNotification);
        trkVco.addItemList ({ "VCO 1", "VCO 2", "VCO 3" }, 1); trkVco.setSelectedId (1, juce::dontSendNotification);
        trkFrom.addItemList ({ noteName (24), noteName (36), noteName (48) }, 1); trkFrom.setSelectedId (1, juce::dontSendNotification);
        trkTo.addItemList ({ noteName (60), noteName (72), noteName (84), noteName (96) }, 1); trkTo.setSelectedId (3, juce::dontSendNotification);
        for (auto* c : std::initializer_list<juce::Component*> { &latMidi, &latChan, &latSrc, &trkMidi, &trkChan, &trkSrc, &trkVco, &trkFrom, &trkTo })
            panel.addChildComponent (c);
        runBtn = makeButton ("Run", [this] { if (! isThreadRunning()) startThread(); });
        stopBtn = makeButton ("Stop", [this] { signalThreadShouldExit(); });
        panel.addAndMakeVisible (*runBtn); panel.addAndMakeVisible (*stopBtn);
        hint = makeLabel ("", 13.5f, col::text2);
        panel.addAndMakeVisible (*hint);
        verdict = makeLabel ("", 15, col::text, 1);
        verdict->setJustificationType (juce::Justification::topLeft);
        results.addAndMakeVisible (*verdict);
        results.addAndMakeVisible (chart);
        results.setTitle ("Results");
        for (auto* l : { "MIDI out", "Channel", "Listen on" }) { latLbls.add (makeLabel (l, 12, col::text3).release()); panel.addChildComponent (latLbls.getLast()); }
        for (auto* l : { "MIDI out (2600 USB)", "Channel", "Listen on", "Which VCO is up", "From", "To" }) { trkLbls.add (makeLabel (l, 12, col::text3).release()); panel.addChildComponent (trkLbls.getLast()); }
        layout();
    }

    ~GearCheckPage() override { stopThread (4000); }

    struct Row : public juce::Component
    {
        Row (Engine& e, int i, const char* def) : idx (i), src (e, "wuSrc" + juce::String (i)), midi (true, "wuMidi" + juce::String (i))
        {
            on.setToggleState (Settings::get().flag ("wuOn" + juce::String (i), i < 5), juce::dontSendNotification);
            on.onClick = [this] { Settings::get().set ("wuOn" + juce::String (idx), on.getToggleState()); };
            name.setText (Settings::get().str ("wuName" + juce::String (i), def), juce::dontSendNotification);
            name.onTextChange = [this] { Settings::get().set ("wuName" + juce::String (idx), name.getText()); };
            for (int c = 1; c <= 16; ++c) chan.addItem ("Ch " + juce::String (c), c);
            chan.setSelectedId (Settings::get().num ("wuChan" + juce::String (i), 1), juce::dontSendNotification);
            chan.onChange = [this] { Settings::get().set ("wuChan" + juce::String (idx), chan.getSelectedId()); };
            fillNotes (note, 24, 96);
            note.setSelectedId (Settings::get().num ("wuNote" + juce::String (i), 57) + 1, juce::dontSendNotification);
            note.onChange = [this] { Settings::get().set ("wuNote" + juce::String (idx), note.getSelectedId() - 1); };
            for (auto* c : std::initializer_list<juce::Component*> { &on, &name, &midi, &chan, &src, &note, &res }) addAndMakeVisible (c);
            res.setFont (font (14, 2)); res.setJustificationType (juce::Justification::centred);
        }
        void resized() override
        {
            rowLayout (getLocalBounds().reduced (0, 3), { { &on, 34 }, { &name, 140 }, { &midi, 220 }, { &chan, 84 }, { &src, 200 }, { &note, 80 }, { &res, 120 } }, 6);
        }
        int idx; juce::ToggleButton on; juce::TextEditor name; SourcePicker src; MidiPicker midi; juce::ComboBox chan, note; juce::Label res;
    };

    void shown() override
    {
        for (auto* r : rows) { r->src.refresh(); r->midi.refresh(); }
        for (auto* p : { &latSrc, &trkSrc }) p->refresh();
        for (auto* p : { &latMidi, &trkMidi }) p->refresh();
    }

    void layout()
    {
        for (int i = 0; i < 3; ++i) tabBtns[i]->setToggleState (i == tab, juce::dontSendNotification);
        for (auto* r : rows) r->setVisible (tab == 0);
        for (auto* c : std::initializer_list<juce::Component*> { &latMidi, &latChan, &latSrc }) c->setVisible (tab == 1);
        for (auto* l : latLbls) l->setVisible (tab == 1);
        for (auto* c : std::initializer_list<juce::Component*> { &trkMidi, &trkChan, &trkSrc, &trkVco, &trkFrom, &trkTo }) c->setVisible (tab == 2);
        for (auto* l : trkLbls) l->setVisible (tab == 2);
        const char* hints[] = {
            "Play the note on each synth and see how far it has drifted. Run it after the gear has warmed up (about 15 minutes for the 2600 and Messenger).",
            "Sends a note, listens for it to come back, and tells you how late your hardware is so you can line it up with the grid in Ableton.",
            "Plays C notes across the range through the 2600's MIDI-to-CV and measures one VCO at a time. Turn the other two VCOs down in the mixer first." };
        hint->setText (hints[tab], juce::dontSendNotification);
        panel.setTitle (tab == 0 ? "Synths" : tab == 1 ? "Latency" : "2600 tracking");
        resized();
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto tb = r.removeFromTop (46);
        for (auto* b : tabBtns) { b->setBounds (tb.removeFromLeft (170)); tb.removeFromLeft (6); }
        r.removeFromTop (4);
        panel.setBounds (r.removeFromTop (tab == 0 ? 380 : 230));
        results.setBounds (r);
        auto c = panel.content();
        hint->setBounds (c.removeFromTop (24));
        c.removeFromTop (8);
        auto bottom = c.removeFromBottom (44);
        runBtn->setBounds (bottom.removeFromLeft (140)); bottom.removeFromLeft (6); stopBtn->setBounds (bottom.removeFromLeft (100));
        if (tab == 0) for (auto* row : rows) row->setBounds (c.removeFromTop (44));
        if (tab == 1)
        {
            auto lr = c.removeFromTop (20), cr = c.removeFromTop (36);
            juce::Component* cs[] = { &latMidi, &latChan, &latSrc }; int ws[] = { 260, 100, 260 };
            for (int i = 0; i < 3; ++i) { latLbls[i]->setBounds (lr.removeFromLeft (ws[i])); lr.removeFromLeft (8); cs[i]->setBounds (cr.removeFromLeft (ws[i])); cr.removeFromLeft (8); }
        }
        if (tab == 2)
        {
            auto lr = c.removeFromTop (20), cr = c.removeFromTop (36);
            juce::Component* cs[] = { &trkMidi, &trkChan, &trkSrc, &trkVco, &trkFrom, &trkTo }; int ws[] = { 250, 90, 220, 140, 80, 80 };
            for (int i = 0; i < 6; ++i) { trkLbls[i]->setBounds (lr.removeFromLeft (ws[i])); lr.removeFromLeft (8); cs[i]->setBounds (cr.removeFromLeft (ws[i])); cr.removeFromLeft (8); }
        }
        auto rr = results.content();
        verdict->setBounds (rr.removeFromTop (rr.getHeight() > 300 ? 90 : 60));
        chart.setBounds (rr);
    }

    // ------------------------------------------------------------ the checks (worker thread)

    void run() override
    {
        if (tab == 0) runWarmup();
        else if (tab == 1) runLatency();
        else runTracking();
    }

    void post (std::function<void()> f)
    {
        juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<GearCheckPage> (this), f] { if (sp) f(); });
    }

    void runWarmup()
    {
        std::vector<BarChart3D::Bar> bars;
        juce::StringArray lines;
        double a4 = Settings::get().dbl ("a4", 440);
        for (auto* r : rows)
        {
            if (threadShouldExit()) break;
            if (! r->on.getToggleState()) continue;
            auto* s = r->src.selected(); auto id = r->midi.id();
            int ch = r->chan.getSelectedId(), note = r->note.getSelectedId() - 1;
            if (s == nullptr || id.isEmpty()) continue;
            post ([r] { r->res.setText ("listening...", juce::dontSendNotification); });
            engine.sendNow (id, juce::MidiMessage::noteOn (ch, note, (juce::uint8) 100));
            wait (600);
            double hz = measurePitch (*this, s, 1.4);
            engine.sendNow (id, juce::MidiMessage::noteOff (ch, note));
            wait (400);
            juce::String nm = r->name.getText();
            if (hz <= 0)
            {
                bars.push_back ({ nm, 0, false });
                post ([r] { r->res.setText ("no pitch", juce::dontSendNotification); });
                lines.add (nm + ": couldn't hear a clear pitch.");
                continue;
            }
            double m = midiFromHz (hz, a4);
            double cents = (m - note) * 100;
            // allow octave mistakes (oscillator set an octave off)
            int oct = (int) std::lround (cents / 1200.0);
            cents -= oct * 1200;
            bars.push_back ({ nm, cents, true });
            juce::String txt = (cents >= 0 ? "+" : "") + juce::String (cents, 1) + "c";
            post ([r, txt] { r->res.setText (txt, juce::dontSendNotification); });
            lines.add (nm + ": " + txt + (std::abs (cents) < 3 ? "  in tune" : cents > 0 ? "  sharp, tune down" : "  flat, tune up")
                       + (oct != 0 ? "  (sounds " + juce::String (std::abs (oct)) + " octave" + (std::abs (oct) > 1 ? "s " : " ") + (oct > 0 ? "up)" : "down)") : juce::String()));
        }
        post ([this, bars, lines] { chart.setBars (bars, 50, "c"); verdict->setText (lines.joinIntoString ("\n"), juce::dontSendNotification); });
    }

    void runLatency()
    {
        auto* s = latSrc.selected(); auto id = latMidi.id(); int ch = latChan.getSelectedId();
        if (s == nullptr || id.isEmpty()) { post ([this] { verdict->setText ("Pick a MIDI output and the input the synth comes back on.", juce::dontSendNotification); }); return; }
        Settings::get().set ("latChan", ch);
        std::vector<double> ms;
        std::vector<BarChart3D::Bar> bars;
        for (int k = 0; k < 8 && ! threadShouldExit(); ++k)
        {
            s->startCapture (1.5);
            wait (250);
            auto sent = s->totalFrames();
            engine.sendNow (id, juce::MidiMessage::noteOn (ch, 60, (juce::uint8) 127));
            wait (450);
            engine.sendNow (id, juce::MidiMessage::noteOff (ch, 60));
            wait (150);
            auto start = s->captureStartFrame();
            auto b = s->stopCapture();
            if (b.getNumSamples() < 100 || start < 0) continue;
            // noise floor from the part before the note, then the first sample well above it
            int pre = (int) juce::jlimit ((juce::int64) 1, (juce::int64) b.getNumSamples(), sent - start);
            float floor = b.getMagnitude (0, 0, pre);
            float th = juce::jmax (floor * 6, 0.01f);
            int on = -1;
            for (int i = pre; i < b.getNumSamples(); ++i) if (std::abs (b.getSample (0, i)) > th) { on = i; break; }
            if (on < 0) continue;
            double lat = (on - pre) * 1000.0 / s->sr;
            ms.push_back (lat);
            bars.push_back ({ "#" + juce::String (k + 1), lat, true });
            post ([this, bars] { chart.setBarsRange (bars, 0, 40, " ms"); });
        }
        if (ms.empty()) { post ([this] { verdict->setText ("Didn't hear anything come back. Check the MIDI channel and that the synth is patched to this input.", juce::dontSendNotification); }); return; }
        auto sorted = ms; std::sort (sorted.begin(), sorted.end());
        double med = sorted[sorted.size() / 2], jit = sorted.back() - sorted.front();
        juce::String t;
        t << "Round trip: " << juce::String (med, 1) << " ms  (spread " << juce::String (jit, 1) << " ms over " << (int) ms.size() << " hits)\n"
          << "In Ableton, set Hardware Latency on the External Instrument to " << juce::String (med, 1) << " ms, or Track Delay on its audio track to -"
          << juce::String (med, 1) << " ms, so it lands on the grid. (Accuracy: about one audio buffer.)";
        post ([this, t] { verdict->setText (t, juce::dontSendNotification); });
    }

    void runTracking()
    {
        auto* s = trkSrc.selected(); auto id = trkMidi.id(); int ch = trkChan.getSelectedId();
        if (s == nullptr || id.isEmpty()) { post ([this] { verdict->setText ("Pick the 2600's MIDI port and the input it's plugged into.", juce::dontSendNotification); }); return; }
        Settings::get().set ("trkChan", ch);
        int from = 24 + 12 * (trkFrom.getSelectedId() - 1), to = 60 + 12 * (trkTo.getSelectedId() - 1);
        std::vector<int> notes;
        for (int n = from; n <= to; n += 12) notes.push_back (n);
        std::vector<double> cents ((size_t) notes.size(), 0.0);
        std::vector<bool> ok (notes.size(), false);
        double a4 = Settings::get().dbl ("a4", 440);
        for (size_t i = 0; i < notes.size() && ! threadShouldExit(); ++i)
        {
            engine.sendNow (id, juce::MidiMessage::noteOn (ch, notes[i], (juce::uint8) 100));
            wait (700);
            double hz = measurePitch (*this, s, 1.2);
            engine.sendNow (id, juce::MidiMessage::noteOff (ch, notes[i]));
            wait (300);
            if (hz > 0) { cents[i] = (midiFromHz (hz, a4) - notes[i]) * 100; ok[i] = true; }
        }
        // reference: the C nearest the middle that we heard
        int ref = -1;
        for (size_t i = 0; i < notes.size(); ++i) if (ok[i] && (ref < 0 || std::abs (notes[i] - 48) < std::abs (notes[(size_t) ref] - 48))) ref = (int) i;
        if (ref < 0) { post ([this] { verdict->setText ("No pitch heard. Make sure only one VCO is up in the mixer and the filter is open.", juce::dontSendNotification); }); return; }
        double base = cents[(size_t) ref];
        base -= 1200 * std::round (base / 1200);
        std::vector<BarChart3D::Bar> bars;
        double sx = 0, sy = 0, sxx = 0, sxy = 0; int n = 0;
        for (size_t i = 0; i < notes.size(); ++i)
        {
            double e = cents[i] - cents[(size_t) ref];
            bars.push_back ({ noteName (notes[i]), e, (bool) ok[i] });
            if (! ok[i]) continue;
            double x = (notes[i] - notes[(size_t) ref]) / 12.0;
            sx += x; sy += e; sxx += x * x; sxy += x * e; ++n;
        }
        double slope = n > 1 ? (n * sxy - sx * sy) / (n * sxx - sx * sx) : 0;
        int goodLo = -1, goodHi = -1;
        for (size_t i = 0; i < notes.size(); ++i)
            if (ok[i] && std::abs (cents[i] - cents[(size_t) ref]) <= 5) { if (goodLo < 0) goodLo = notes[i]; goodHi = notes[i]; }
        juce::String t;
        t << trkVco.getText() << ": octaves are " << (slope >= 0 ? "+" : "") << juce::String (slope, 1) << " cents "
          << (std::abs (slope) < 2 ? "per octave. That's tracking well." : slope > 0 ? "per octave too wide (sharp going up)." : "per octave too narrow (flat going up).") << "\n";
        if (goodLo >= 0) t << "Within 5 cents from " << noteName (goodLo) << " to " << noteName (goodHi) << ". ";
        if (std::abs (slope) >= 2)
            t << "The VCO's scale (V/oct) needs " << (slope > 0 ? "less" : "more") << " range: adjust its scale trim and run this again, retuning with Fine "
              << "tune at " << noteName (notes[(size_t) ref]) << " each pass. If you'd rather not open it up, tune at the octave you play most.";
        post ([this, bars, t] { chart.setBars (bars, 50, "c"); verdict->setText (t, juce::dontSendNotification); });
    }

    void tick() override { runBtn->setEnabled (! isThreadRunning()); }

    void demo() override
    {
        tab = 2; layout();
        std::vector<BarChart3D::Bar> b = { { noteName (24), -7.5, true }, { noteName (36), -3.1, true }, { noteName (48), 0, true }, { noteName (60), 3.6, true }, { noteName (72), 8.2, true }, { noteName (84), 14.9, true } };
        chart.setBars (b, 50, "c");
        verdict->setText ("VCO 1: octaves are +4.4 cents per octave too wide (sharp going up).\nWithin 5 cents from C1 to C3. The VCO's scale (V/oct) needs less range: adjust its scale trim and run this again, "
                          "retuning with Fine tune at C2 each pass. If you'd rather not open it up, tune at the octave you play most.", juce::dontSendNotification);
    }

    juce::OwnedArray<juce::TextButton> tabBtns;
    Slab panel, results;
    juce::OwnedArray<Row> rows;
    MidiPicker latMidi { true, "latMidi" }, trkMidi { true, "trkMidi" };
    SourcePicker latSrc { engine, "latSrc" }, trkSrc { engine, "trkSrc" };
    juce::ComboBox latChan, trkChan, trkVco, trkFrom, trkTo;
    juce::OwnedArray<juce::Label> latLbls, trkLbls;
    std::unique_ptr<juce::TextButton> runBtn, stopBtn;
    std::unique_ptr<juce::Label> hint, verdict;
    BarChart3D chart;
    int tab = 0;
};
}

std::unique_ptr<Page> makeGearCheckPage (Engine& e) { return std::make_unique<GearCheckPage> (e); }
}
