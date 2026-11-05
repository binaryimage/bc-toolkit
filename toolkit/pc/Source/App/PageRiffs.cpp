#include "Pages.h"
#include "Common/Theory.h"
#include "Common/Dsp.h"

namespace tk
{
namespace
{
using theory::Riff;

/** Three-lane piano roll: chords, bass, lead. Notes are little 3D bricks. */
class RollView : public Screen
{
public:
    std::shared_ptr<const Riff> riff;
    double beat = -1;
    void paintScreen (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        if (riff == nullptr) return;
        const double beats = riff->bars * 4.0;
        const char* lanesN[] = { "CHORDS", "BASS", "LEAD" };
        const std::vector<NoteEvent>* lanes[] = { &riff->chords, &riff->bass, &riff->lead };
        auto area = r.withTrimmedLeft (64);
        float lh = area.getHeight() / 3;
        for (int b = 0; b <= riff->bars * 4; ++b)
        {
            float x = area.getX() + (float) (b / beats) * area.getWidth();
            g.setColour (b % 4 == 0 ? col::text3.withAlpha (0.6f) : col::grid);
            g.drawVerticalLine ((int) x, area.getY(), area.getBottom());
            if (b % 4 == 0 && b / 4 < riff->chordNames.size())
            {
                g.setFont (font (12, 1)); g.setColour (col::text2);
                g.drawText (riff->chordNames[b / 4], (int) x + 6, (int) area.getY() + 2, 120, 14, juce::Justification::left);
            }
        }
        for (int l = 0; l < 3; ++l)
        {
            auto lane = area.withY (area.getY() + l * lh).withHeight (lh).reduced (0, 6);
            if (l == 0) lane.removeFromTop (14);
            g.setFont (font (10.5f, 1)); g.setColour (col::text3);
            g.drawText (lanesN[l], (int) r.getX(), (int) lane.getCentreY() - 7, 60, 14, juce::Justification::left);
            g.setColour (col::grid); g.drawHorizontalLine ((int) (area.getY() + (l + 1) * lh), area.getX(), area.getRight());
            int lo = 127, hi = 0;
            for (auto& n : *lanes[l]) { lo = juce::jmin (lo, n.note); hi = juce::jmax (hi, n.note); }
            if (lo > hi) continue;
            lo -= 1; hi += 1;
            float nh = juce::jlimit (3.0f, 12.0f, lane.getHeight() / (hi - lo + 1));
            for (auto& n : *lanes[l])
            {
                float x0 = area.getX() + (float) (n.beat / beats) * area.getWidth();
                float w = juce::jmax (3.0f, (float) (n.lengthBeats / beats) * area.getWidth() - 1);
                float y = lane.getBottom() - (n.note - lo + 1) * (lane.getHeight() - nh) / (hi - lo + 1) - nh;
                bool on = beat >= n.beat && beat < n.beat + n.lengthBeats;
                auto b = juce::Rectangle<float> (x0, y, w, nh);
                float a = 0.45f + 0.55f * n.velocity / 127.0f;
                g.setColour (juce::Colour (0xff5a5a5a).withAlpha (a)); g.fillRect (b.translated (1.5f, 1.5f));
                g.setGradientFill (juce::ColourGradient (on ? col::white : juce::Colour (0xffd8d8d8).withAlpha (a), b.getX(), b.getY(),
                                                         on ? juce::Colour (0xffbdbdbd) : juce::Colour (0xff7a7a7a).withAlpha (a), b.getX(), b.getBottom(), false));
                g.fillRect (b);
            }
        }
        if (beat >= 0)
        {
            float x = area.getX() + (float) (std::fmod (beat, beats) / beats) * area.getWidth();
            g.setColour (col::white); g.fillRect (x - 1, area.getY(), 2.0f, area.getHeight());
        }
    }
};

/** Drag this into Ableton (or the desktop) to drop the riff as a MIDI file. */
class DragOut : public juce::Component
{
public:
    std::function<juce::File()> makeFile;
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (2, 3);
        drawKey (g, r, false, false, isMouseOver(), 8);
        g.setColour (col::text); g.setFont (font (14, 1));
        g.drawText ("Drag the MIDI into Ableton", r.toNearestInt(), juce::Justification::centred);
    }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging || e.getDistanceFromDragStart() < 6 || ! makeFile) return;
        auto f = makeFile();
        if (! f.existsAsFile()) return;
        dragging = true;
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this, [this] { dragging = false; });
    }
    bool dragging = false;
};

/** Built-in preview synth: mono bass with no glide, square-ish lead, soft pad. Runs on the audio thread. */
struct Voice { int note = -1, part = 0; double ph1 = 0, ph2 = 0, len = 0, age = 0; float vel = 0, env = 0, lp = 0; };

class RiffsPage : public Page, private juce::HighResolutionTimer
{
public:
    RiffsPage (Engine& e) : Page (e, "riffs", "Pattern generator",
                                  "Generates a chord progression, bass line and lead in minor and modal scales. Audition it on your synths, then export it as MIDI.")
    {
        for (auto* s : { &opts, &roll, &play }) addAndMakeVisible (s);
        opts.setTitle ("Settings"); roll.setTitle ("Pattern"); play.setTitle ("Playback");
        const char* pcs[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        for (int i = 0; i < 12; ++i) root.addItem (pcs[i], i + 1);
        root.setSelectedId (Settings::get().num ("riffRoot", 2) + 1, juce::dontSendNotification);
        for (int i = 0; i < (int) theory::scales().size(); ++i) scale.addItem (theory::scales()[(size_t) i].name, i + 1);
        scale.setSelectedId (Settings::get().num ("riffScale", 0) + 1, juce::dontSendNotification);
        bars.addItemList ({ "2 bars", "4 bars", "8 bars" }, 1); bars.setSelectedId (2, juce::dontSendNotification);
        bass.addItemList ({ "16th notes", "8th notes", "Sustained", "Hip-hop" }, 1);
        bass.setSelectedId (Settings::get().num ("riffBass", 0) + 1, juce::dontSendNotification);
        tempo.setRange (60, 180, 1); tempo.setValue (Settings::get().num ("riffBpm", 118), juce::dontSendNotification);
        tempo.setSliderStyle (juce::Slider::LinearHorizontal); tempo.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 22); tempo.setTextValueSuffix (" BPM");
        sevenths.setButtonText ("7th chords");
        for (auto* c : std::initializer_list<juce::ComboBox*> { &root, &scale, &bars, &bass }) { styleCombo (*c); opts.addAndMakeVisible (c); c->onChange = [this] { generate(); }; }
        tempo.onDragEnd = [this] { generate(); };
        sevenths.onClick = [this] { generate(); };
        opts.addAndMakeVisible (tempo); opts.addAndMakeVisible (sevenths);
        rollBtn = makeButton ("Generate", [this] { seed = (unsigned) juce::Random::getSystemRandom().nextInt (1 << 24); generate(); });
        opts.addAndMakeVisible (*rollBtn);
        // Random: a new key, scale and bass style (whichever aren't locked), then new notes
        randomBtn = makeButton ("Random", [this] { randomise(); });
        randomBtn->setTooltip ("New key, scale and bass style, then new notes. Lock any of them to keep it.");
        opts.addAndMakeVisible (*randomBtn);
        const char* lockNames[] = { "key", "scale", "bass style" };
        const char* lockKeys[] = { "riffLockKey", "riffLockScale", "riffLockBass" };
        for (int i = 0; i < 3; ++i)
        {
            auto* l = locks.add (new LockToggle (lockNames[i]));
            l->setToggleState (Settings::get().flag (lockKeys[i]), juce::dontSendNotification);
            juce::String k (lockKeys[i]);
            l->onClick = [l, k] { Settings::get().set (k, l->getToggleState()); };
            opts.addAndMakeVisible (l);
        }
        for (auto* n : { "Key", "Scale", "Length", "Bass", "Tempo" }) { auto l = makeLabel (n, 12, col::text3); opts.addAndMakeVisible (*l); lbls.push_back (std::move (l)); }
        roll.addAndMakeVisible (view);

        target.addItemList ({ "Built-in preview sound", "My synths over MIDI" }, 1);
        target.setSelectedId (1, juce::dontSendNotification);
        styleCombo (target); play.addAndMakeVisible (target);
        const char* parts[] = { "Chords", "Bass", "Lead" };
        const int defCh[] = { 1, 2, 3 };
        for (int p = 0; p < 3; ++p)
        {
            auto& pr = partRows[(size_t) p];
            pr.label = makeLabel (parts[p], 13, col::text2, 1);
            pr.out = std::make_unique<MidiPicker> (true, juce::String ("riffOut") + juce::String (p));
            styleCombo (*pr.out);
            for (int c = 1; c <= 16; ++c) pr.chan.addItem ("Ch " + juce::String (c), c);
            pr.chan.setSelectedId (Settings::get().num ("riffCh" + juce::String (p), defCh[p]), juce::dontSendNotification);
            pr.chan.onChange = [this, p] { Settings::get().set ("riffCh" + juce::String (p), partRows[(size_t) p].chan.getSelectedId()); };
            styleCombo (pr.chan);
            pr.on.setToggleState (true, juce::dontSendNotification); pr.on.setButtonText ({});
            for (auto* c : std::initializer_list<juce::Component*> { pr.label.get(), pr.out.get(), &pr.chan, &pr.on }) play.addAndMakeVisible (c);
        }
        playBtn = makeButton ("Play", [this] { start(); });
        stopBtn = makeButton ("Stop", [this] { stop(); });
        loopBtn = makeButton ("Loop", {}, true); loopBtn->setToggleState (true, juce::dontSendNotification);
        saveBtn = makeButton ("Save .mid", [this] { saveMidi().revealToUser(); });
        for (auto* b : { playBtn.get(), stopBtn.get(), loopBtn.get(), saveBtn.get() }) play.addAndMakeVisible (b);
        drag.makeFile = [this] { return saveMidi(); };
        play.addAndMakeVisible (drag);
        hint = makeLabel ("Bass notes are retriggered individually, with no glide. Set each synth to receive on the channel shown above.", 12, col::text3);
        hint->setJustificationType (juce::Justification::topLeft);
        play.addAndMakeVisible (*hint);
        seed = (unsigned) Settings::get().num ("riffSeed", 7);
        generate();
    }

    ~RiffsPage() override { stop(); }

    void generate()
    {
        theory::RiffOptions o;
        o.root = root.getSelectedId() - 1; o.scale = scale.getSelectedId() - 1;
        o.bars = bars.getSelectedId() == 1 ? 2 : bars.getSelectedId() == 3 ? 8 : 4;
        o.bpm = tempo.getValue(); o.bassStyle = bass.getSelectedId() - 1; o.sevenths = sevenths.getToggleState(); o.seed = seed;
        auto& s = Settings::get();
        s.set ("riffRoot", o.root); s.set ("riffScale", o.scale); s.set ("riffBass", o.bassStyle); s.set ("riffBpm", o.bpm); s.set ("riffSeed", (int) seed);
        auto r = std::make_shared<const Riff> (theory::makeRiff (o));
        view.riff = r; view.repaint();
        roll.setTitle ("Pattern  /  " + root.getText() + " " + scale.getText() + "  /  " + juce::String ((int) o.bpm) + " BPM  /  " + juce::String (o.bars) + " bars");
        const juce::SpinLock::ScopedLockType l (riffLock);
        current = r;
    }

    /** Random leans to the darker, less common scales: plain natural minor and Dorian come up least. */
    static int pickScale (juce::Random& rnd)
    {
        auto& all = theory::scales();
        std::vector<double> w;
        double total = 0;
        for (auto& sc : all)
        {
            juce::String n (sc.name);
            double x = n == "Natural minor" ? 0.35 : n == "Dorian" ? 0.5 : n == "Harmonic minor" ? 0.8 : 1.3;
            w.push_back (x); total += x;
        }
        double pick = rnd.nextDouble() * total;
        for (size_t i = 0; i < w.size(); ++i) { pick -= w[i]; if (pick <= 0) return (int) i; }
        return (int) w.size() - 1;
    }

    void randomise()
    {
        auto& rnd = juce::Random::getSystemRandom();
        if (! locks[0]->getToggleState()) root.setSelectedId (rnd.nextInt (12) + 1, juce::dontSendNotification);
        if (! locks[1]->getToggleState())
        {
            const int was = scale.getSelectedId();
            int next = pickScale (rnd) + 1;
            for (int t = 0; t < 4 && next == was; ++t) next = pickScale (rnd) + 1;   // a different one, most of the time
            scale.setSelectedId (next, juce::dontSendNotification);
        }
        if (! locks[2]->getToggleState()) bass.setSelectedId (rnd.nextInt (bass.getNumItems()) + 1, juce::dontSendNotification);
        seed = (unsigned) rnd.nextInt (1 << 24);
        generate();
    }

    juce::File saveMidi()
    {
        auto r = view.riff;
        if (r == nullptr) return {};
        auto dir = musicFolder().getChildFile ("Toolkit Patterns");
        dir.createDirectory();
        auto nm = safeName (root.getText() + " " + scale.getText() + " " + juce::String ((int) r->bpm) + " " + juce::String (seed % 10000));
        auto f = dir.getChildFile (nm + ".mid");
        writeMidiFile (f, { { "Chords", r->chords }, { "Bass", r->bass }, { "Lead", r->lead } }, r->bpm);
        for (auto& d : mpcExportFolders ("Toolkit Patterns")) { d.createDirectory(); f.copyFileTo (d.getChildFile (f.getFileName())); }
        return f;
    }

    // ------------------------------------------------------------ playback
    void start()
    {
        stop();
        midiMode = target.getSelectedId() == 2;
        loop = loopBtn->getToggleState();
        for (int p = 0; p < 3; ++p)
        {
            partOn[(size_t) p] = partRows[(size_t) p].on.getToggleState();
            partOut[(size_t) p] = partRows[(size_t) p].out->id();
            partCh[(size_t) p] = partRows[(size_t) p].chan.getSelectedId();
        }
        playing = true;
        if (midiMode)
        {
            t0 = juce::Time::getMillisecondCounterHiRes();
            lastBeat = -1e-9;
            startTimer (1);
        }
        else
        {
            sampleClock = 0; resetVoices = true;
            engine.setGenerator ([this] (float* L, float* R, int n, double sr) { synth (L, R, n, sr); });
        }
    }

    void stop()
    {
        if (isTimerRunning()) stopTimer();
        if (playing && midiMode)
            for (int p = 0; p < 3; ++p) if (partOut[(size_t) p].isNotEmpty()) engine.allNotesOff (partOut[(size_t) p], partCh[(size_t) p]);
        if (playing && ! midiMode) engine.setGenerator (nullptr);
        playing = false; beatNow = -1;
    }

    const std::vector<NoteEvent>& lane (const Riff& r, int p) { return p == 0 ? r.chords : p == 1 ? r.bass : r.lead; }

    void hiResTimerCallback() override
    {
        auto r = view.riff;
        if (r == nullptr) return;
        const double len = r->bars * 4.0;
        double beat = (juce::Time::getMillisecondCounterHiRes() - t0) / 60000.0 * r->bpm;
        if (! loop && beat >= len) { juce::MessageManager::callAsync ([this] { stop(); }); return; }
        double a = std::fmod (lastBeat, len), b = std::fmod (beat, len);
        auto crossed = [&] (double t) { return a <= b ? (t > a && t <= b) : (t > a || t <= b); };
        for (int p = 0; p < 3; ++p)
        {
            if (! partOn[(size_t) p] || partOut[(size_t) p].isEmpty()) continue;
            for (auto& n : lane (*r, p))
            {
                // note-offs before note-ons so repeated notes retrigger cleanly (no legato glide)
                if (crossed (std::fmod (n.beat + n.lengthBeats, len))) engine.sendNow (partOut[(size_t) p], juce::MidiMessage::noteOff (partCh[(size_t) p], n.note));
            }
            for (auto& n : lane (*r, p))
                if (crossed (n.beat == 0 ? len : n.beat) || (lastBeat < 0 && n.beat == 0))
                    engine.sendNow (partOut[(size_t) p], juce::MidiMessage::noteOn (partCh[(size_t) p], n.note, (juce::uint8) n.velocity));
        }
        lastBeat = beat;
        beatNow = beat;
    }

    void synth (float* L, float* R, int n, double sr)
    {
        std::shared_ptr<const Riff> r;
        {
            const juce::SpinLock::ScopedTryLockType l (riffLock);
            if (l.isLocked()) cachedRiff = current;
            r = cachedRiff;
        }
        if (r == nullptr) return;
        if (resetVoices.exchange (false)) for (auto& v : voices) v.note = -1;
        const double len = r->bars * 4.0, spb = sr * 60.0 / r->bpm;
        for (int i = 0; i < n; ++i)
        {
            double beat = sampleClock / spb;
            if (! loop && beat >= len) { if (i == 0) beatNow = -1; break; }
            double b = std::fmod (beat, len), prev = std::fmod ((sampleClock - 1) / spb, len);
            auto crossed = [&] (double t) { return sampleClock == 0 ? t == 0 : (prev <= b ? (t > prev && t <= b) : (t > prev || t <= b)); };
            for (int p = 0; p < 3; ++p)
            {
                if (! partOn[(size_t) p]) continue;
                for (auto& e : lane (*r, p))
                    if (crossed (e.beat == 0 && sampleClock != 0 ? len : e.beat) ) trigger (p, e, spb);
            }
            float out = 0;
            for (auto& v : voices)
            {
                if (v.note < 0) continue;
                double hz = 440.0 * std::pow (2.0, (v.note - 69) / 12.0);
                float gate = v.age < v.len ? 1.0f : 0.0f;
                float atk = v.part == 0 ? 0.0006f : 0.02f, rel = v.part == 1 ? 0.004f : v.part == 0 ? 0.0002f : 0.0015f;
                v.env += gate > 0 ? (1.0f - v.env) * atk : -v.env * rel;
                if (gate == 0 && v.env < 1e-4f) { v.note = -1; continue; }
                float s;
                if (v.part == 1)   // bass: saw + sub, filter closes with the env
                {
                    s = (float) (2 * v.ph1 - 1) * 0.6f + (float) std::sin (juce::MathConstants<double>::twoPi * v.ph2) * 0.6f;
                    float cut = 0.03f + 0.25f * v.env * v.env;
                    v.lp += (s - v.lp) * cut; s = v.lp;
                }
                else if (v.part == 2)   // lead: hollow square
                {
                    s = (v.ph1 < 0.5 ? 0.5f : -0.5f) + (float) (2 * v.ph2 - 1) * 0.25f;
                    v.lp += (s - v.lp) * 0.12f; s = v.lp;
                }
                else   // pad: two detuned saws, dark
                {
                    s = (float) ((2 * v.ph1 - 1) + (2 * v.ph2 - 1)) * 0.3f;
                    v.lp += (s - v.lp) * 0.05f; s = v.lp;
                }
                out += s * v.env * v.vel * (v.part == 0 ? 0.12f : v.part == 1 ? 0.35f : 0.18f);
                v.ph1 += hz / sr; if (v.ph1 >= 1) v.ph1 -= 1;
                double h2 = v.part == 1 ? hz * 0.5 : v.part == 0 ? hz * 1.004 : hz * 2.0;
                v.ph2 += h2 / sr; if (v.ph2 >= 1) v.ph2 -= 1;
                v.age += 1;
            }
            out = std::tanh (out * 1.4f) * 0.7f;
            L[i] += out; if (R) R[i] += out;
            sampleClock += 1;
        }
        beatNow = sampleClock / spb;
    }

    void trigger (int part, const NoteEvent& e, double spb)
    {
        Voice* v = nullptr;
        if (part == 1) for (auto& x : voices) if (x.part == 1 && x.note >= 0) { v = &x; break; }   // mono bass: hard retrigger
        if (v == nullptr) for (auto& x : voices) if (x.note < 0) { v = &x; break; }
        if (v == nullptr) v = &voices[0];
        float keepLp = v->lp;
        *v = Voice();
        v->note = e.note; v->part = part; v->len = e.lengthBeats * spb; v->vel = e.velocity / 127.0f; v->lp = part == 1 ? keepLp : 0;
    }

    void tick() override
    {
        double b = playing ? beatNow.load() : -1;
        if (std::abs (b - view.beat) > 0.01) { view.beat = b; view.repaint(); }
        playBtn->setToggleState (playing, juce::dontSendNotification);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto top = r.removeFromTop (164);
        opts.setBounds (top);
        r.removeFromTop (10);
        auto bottom = r.removeFromBottom (250);
        roll.setBounds (r);
        play.setBounds (bottom.withTrimmedTop (10));

        auto o = opts.content();
        auto labels = o.removeFromTop (18);
        auto row = o.removeFromTop (36);
        // key / scale / length / bass, and tempo takes what's left (so a narrow window never pushes anything off the edge)
        const int fixedW[] = { 90, 220, 120, 160 };
        const int tempoW = juce::jlimit (150, 360, row.getWidth() - (90 + 220 + 120 + 160) - 4 * 10);
        const int ws[] = { fixedW[0], fixedW[1], fixedW[2], fixedW[3], tempoW };
        juce::Component* cs[] = { &root, &scale, &bars, &bass, &tempo };
        int x = row.getX();
        for (int i = 0; i < 5; ++i)
        {
            lbls[(size_t) i]->setBounds (x, labels.getY(), ws[i], 18);
            cs[i]->setBounds (x, row.getY(), ws[i], 34);
            x += ws[i] + 10;
        }
        // padlocks sit at the right end of the Key, Scale and Bass labels
        const int lockFor[] = { 0, 1, 3 };
        for (int k = 0; k < 3; ++k) { auto lb = lbls[(size_t) lockFor[k]]->getBounds(); locks[k]->setBounds (lb.getRight() - 20, lb.getY(), 20, lb.getHeight()); }
        o.removeFromTop (8);
        auto row2 = o.removeFromTop (juce::jmin (40, o.getHeight()));
        sevenths.setBounds (row2.removeFromLeft (130));
        rollBtn->setBounds (row2.removeFromRight (juce::jmin (200, row2.getWidth() / 2)));
        row2.removeFromRight (8);
        randomBtn->setBounds (row2.removeFromRight (juce::jmin (150, row2.getWidth())));

        view.setBounds (roll.content());

        auto p = play.content();
        // the transport buttons need ~450 on the right; the synth rows get the rest (the MIDI picker shrinks first)
        auto left = p.removeFromLeft (juce::jlimit (380, 620, p.getWidth() - 470));
        target.setBounds (left.removeFromTop (34).withWidth (juce::jmin (300, left.getWidth())));
        left.removeFromTop (8);
        const int outW = juce::jmax (120, left.getWidth() - 44 - 70 - 90 - 3 * 8);
        for (auto& pr : partRows)
        {
            auto rr = left.removeFromTop (38);
            rowLayout (rr, { { &pr.on, 44 }, { pr.label.get(), 70 }, { pr.out.get(), outW }, { &pr.chan, 90 } });
            left.removeFromTop (4);
        }
        p.removeFromLeft (20);
        const int bh = flowLayout (p, { { playBtn.get(), 110 }, { stopBtn.get(), 90 }, { loopBtn.get(), 90 }, { saveBtn.get(), 130 } }, 46, 8, false);
        flowLayout (p.removeFromTop (bh), { { playBtn.get(), 110 }, { stopBtn.get(), 90 }, { loopBtn.get(), 90 }, { saveBtn.get(), 130 } }, 46, 8);
        p.removeFromTop (8);
        drag.setBounds (p.removeFromTop (50).withWidth (juce::jmin (440, p.getWidth())));
        p.removeFromTop (10);
        hint->setBounds (p);
    }

    void demo() override
    {
        seed = 4242;
        root.setSelectedId (3, juce::dontSendNotification);
        scale.setSelectedId (1, juce::dontSendNotification);
        tempo.setValue (122, juce::dontSendNotification);
        generate();
        view.beat = 5.25; view.repaint();
        target.setSelectedId (2, juce::dontSendNotification);
    }

    Slab opts, roll, play;
    juce::ComboBox root, scale, bars, bass, target;
    juce::Slider tempo;
    juce::ToggleButton sevenths;
    std::unique_ptr<juce::TextButton> rollBtn, randomBtn, playBtn, stopBtn, loopBtn, saveBtn;
    juce::OwnedArray<LockToggle> locks;   // key, scale, bass style
    std::vector<std::unique_ptr<juce::Label>> lbls;
    std::unique_ptr<juce::Label> hint;
    struct PartRow { std::unique_ptr<juce::Label> label; std::unique_ptr<MidiPicker> out; juce::ComboBox chan; juce::ToggleButton on; };
    std::array<PartRow, 3> partRows;
    RollView view;
    DragOut drag;
    unsigned seed = 7;

    juce::SpinLock riffLock;
    std::shared_ptr<const Riff> current, cachedRiff;
    std::array<bool, 3> partOn { true, true, true };
    std::array<juce::String, 3> partOut;
    std::array<int, 3> partCh { 1, 2, 3 };
    std::atomic<bool> playing { false }, resetVoices { false };
    std::atomic<double> beatNow { -1 };
    bool midiMode = false, loop = true;
    double t0 = 0, lastBeat = 0;
    juce::int64 sampleClock = 0;
    std::array<Voice, 16> voices;
};
}

std::unique_ptr<Page> makeRiffsPage (Engine& e) { return std::make_unique<RiffsPage> (e); }
}
