#include "Pages.h"
#include "SamplerCore.h"

namespace tk
{
namespace
{
class SamplerPage : public Page, private juce::Thread
{
public:
    SamplerPage (Engine& e) : Page (e, "sampler", "Auto-sampler",
                                    "Plays your synth note by note over MIDI, records it, and builds an instrument for the MPC and anything that reads SFZ."),
                              juce::Thread ("auto-sampler")
    {
        for (auto* s : { &cfg, &prog }) addAndMakeVisible (s);
        cfg.setTitle ("What to sample"); prog.setTitle ("Progress");
        auto addRow = [this] (const char* label, juce::Component& c) {
            auto* l = labels.add (makeLabel (label, 12.5f, col::text3).release());
            cfg.addAndMakeVisible (l); cfg.addAndMakeVisible (c); rows.push_back ({ l, &c });
        };
        name.setText (Settings::get().str ("smpName", "Hydra Dark Pad"));
        addRow ("Name", name);
        addRow ("MIDI out to the synth", midi);
        for (int c = 1; c <= 16; ++c) chan.addItem ("Channel " + juce::String (c), c);
        chan.setSelectedId (Settings::get().num ("smpChan", 1), juce::dontSendNotification);
        addRow ("MIDI channel", chan);
        addRow ("Record from", src);
        fillNotes (lo, 12, 108); fillNotes (hi, 12, 108);
        lo.setSelectedId (Settings::get().num ("smpLo", 36) + 1, juce::dontSendNotification);
        hi.setSelectedId (Settings::get().num ("smpHi", 84) + 1, juce::dontSendNotification);
        addRow ("Lowest note", lo); addRow ("Highest note", hi);
        step.addItemList ({ "Every note (1)", "Every 2 semitones", "Minor thirds (3)", "Major thirds (4)", "Tritones (6)", "Octaves (12)" }, 1);
        step.setSelectedId (Settings::get().num ("smpStep", 3), juce::dontSendNotification);
        addRow ("Sample every", step);
        vel.addItemList ({ "1 layer (110)", "2 layers (64 / 127)", "3 layers (40 / 90 / 127)", "4 layers (32 / 64 / 96 / 127)" }, 1);
        vel.setSelectedId (Settings::get().num ("smpVel", 1), juce::dontSendNotification);
        addRow ("Velocity layers", vel);
        for (auto* s : { &noteLen, &tail })
        {
            s->setSliderStyle (juce::Slider::LinearHorizontal); s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 22);
            s->setTextValueSuffix (" s");
        }
        noteLen.setRange (0.1, 10, 0.1); noteLen.setValue (Settings::get().dbl ("smpLen", 2.0));
        tail.setRange (0, 10, 0.1); tail.setValue (Settings::get().dbl ("smpTail", 1.5));
        addRow ("Hold each note", noteLen); addRow ("Release tail", tail);
        normalize.setButtonText ("Normalize (keeps velocity dynamics)"); normalize.setToggleState (true, juce::dontSendNotification);
        mpcCopy.setButtonText ("Also copy to the MPC folders"); mpcCopy.setToggleState (Settings::get().flag ("smpMpc", true), juce::dontSendNotification);
        cfg.addAndMakeVisible (normalize); cfg.addAndMakeVisible (mpcCopy);

        startBtn = makeButton ("Start sampling", [this] { start(); });
        stopBtn = makeButton ("Stop", [this] { signalThreadShouldExit(); });
        testBtn = makeButton ("Test note", [this] { testNote(); });
        openBtn = makeButton ("Open folder", [this] { lastDir.startAsProcess(); });
        for (auto* b : { startBtn.get(), stopBtn.get(), testBtn.get(), openBtn.get() }) prog.addAndMakeVisible (b);
        prog.addAndMakeVisible (bar);
        prog.addAndMakeVisible (wave); wave.editable = false;
        prog.addAndMakeVisible (levels);
        status = makeLabel ("Set it up on the left, hit Test note to check you hear the synth, then Start.", 14, col::text2);
        prog.addAndMakeVisible (*status);
    }

    ~SamplerPage() override { stopThread (4000); }

    void shown() override { src.refresh(); midi.refresh(); }

    void resized() override
    {
        auto r = getLocalBounds();
        cfg.setBounds (r.removeFromLeft (440));
        prog.setBounds (r);
        auto c = cfg.content();
        for (auto& [l, comp] : rows)
        {
            auto row = c.removeFromTop (36);
            l->setBounds (row.removeFromLeft (150));
            comp->setBounds (row.reduced (0, 2));
            c.removeFromTop (4);
        }
        c.removeFromTop (6);
        normalize.setBounds (c.removeFromTop (30)); mpcCopy.setBounds (c.removeFromTop (30));

        auto q = prog.content();
        auto b = q.removeFromTop (44);
        startBtn->setBounds (b.removeFromLeft (170)); b.removeFromLeft (6);
        stopBtn->setBounds (b.removeFromLeft (100)); b.removeFromLeft (6);
        testBtn->setBounds (b.removeFromLeft (120));
        openBtn->setBounds (b.removeFromRight (140));
        q.removeFromTop (10);
        status->setBounds (q.removeFromTop (24));
        bar.setBounds (q.removeFromTop (22));
        q.removeFromTop (10);
        wave.setBounds (q.removeFromTop (q.getHeight() / 2));
        q.removeFromTop (8);
        levels.setBounds (q);
    }

    SamplerPlan plan() const
    {
        SamplerPlan p;
        p.name = name.getText().trim().isEmpty() ? juce::String ("Instrument") : name.getText().trim();
        p.lo = juce::jmin (lo.getSelectedId(), hi.getSelectedId()) - 1;
        p.hi = juce::jmax (lo.getSelectedId(), hi.getSelectedId()) - 1;
        const int steps[] = { 1, 2, 3, 4, 6, 12 };
        p.step = steps[juce::jlimit (0, 5, step.getSelectedId() - 1)];
        const std::vector<int> v[] = { { 110 }, { 64, 127 }, { 40, 90, 127 }, { 32, 64, 96, 127 } };
        p.velocities = v[juce::jlimit (0, 3, vel.getSelectedId() - 1)];
        p.noteSeconds = noteLen.getValue(); p.tailSeconds = tail.getValue();
        p.normalize = normalize.getToggleState();
        p.outDir = Settings::get().folder ("samplesFolder", musicFolder().getChildFile ("Toolkit Samples")).getChildFile (safeName (p.name) + " " + stamp());
        if (mpcCopy.getToggleState()) p.copyTo = mpcExportFolders ("Toolkit Instruments");
        return p;
    }

    void save()
    {
        auto& s = Settings::get();
        s.set ("smpName", name.getText()); s.set ("smpChan", chan.getSelectedId()); s.set ("smpLo", lo.getSelectedId() - 1);
        s.set ("smpHi", hi.getSelectedId() - 1); s.set ("smpStep", step.getSelectedId()); s.set ("smpVel", vel.getSelectedId());
        s.set ("smpLen", noteLen.getValue()); s.set ("smpTail", tail.getValue()); s.set ("smpMpc", mpcCopy.getToggleState());
    }

    void testNote()
    {
        auto id = midi.id(); int ch = chan.getSelectedId();
        if (id.isEmpty()) { status->setText ("No MIDI output picked.", juce::dontSendNotification); return; }
        engine.sendNow (id, juce::MidiMessage::noteOn (ch, 60, (juce::uint8) 110));
        juce::Timer::callAfterDelay (900, [this, id, ch] { engine.sendNow (id, juce::MidiMessage::noteOff (ch, 60)); });
        status->setText ("Sent middle C. You should see the meter move on the right source.", juce::dontSendNotification);
    }

    void start()
    {
        if (isThreadRunning()) return;
        if (src.selected() == nullptr) { status->setText ("Pick what to record from.", juce::dontSendNotification); return; }
        if (midi.id().isEmpty()) { status->setText ("Pick the MIDI output that reaches the synth.", juce::dontSendNotification); return; }
        save();
        job = plan();
        jobSrc = src.selected(); jobMidi = midi.id(); jobChan = chan.getSelectedId();
        takes.clear(); bars.clear();
        progress = 0;
        startThread();
    }

    void run() override
    {
        auto notes = job.notes();
        int total = (int) (notes.size() * job.velocities.size()), done = 0;
        for (int note : notes)
            for (int v : job.velocities)
            {
                if (threadShouldExit()) break;
                setStatus ("Sampling " + noteName (note) + "  velocity " + juce::String (v) + "   (" + juce::String (done + 1) + " of " + juce::String (total) + ")");
                jobSrc->startCapture (job.noteSeconds + job.tailSeconds + 0.5);
                wait (60);
                engine.sendNow (jobMidi, juce::MidiMessage::noteOn (jobChan, note, (juce::uint8) v));
                wait ((int) (job.noteSeconds * 1000));
                engine.sendNow (jobMidi, juce::MidiMessage::noteOff (jobChan, note));
                wait ((int) (job.tailSeconds * 1000) + 100);
                auto b = jobSrc->stopCapture();
                {
                    const juce::ScopedLock sl (lock);
                    takes.push_back ({ note, v, b, jobSrc->sr });
                    last.makeCopyOf (b);
                    bars.push_back ({ noteName (note), juce::Decibels::gainToDecibels (peakOf (b), -60.0f), peakOf (b) > 1e-4f });
                    fresh = true;
                }
                progress = ++done / (double) total;
                wait (150);
            }
        engine.allNotesOff (jobMidi, jobChan);
        if (threadShouldExit()) { setStatus ("Stopped. Nothing was saved."); return; }
        setStatus ("Writing files...");
        std::vector<SampledNote> copy;
        { const juce::ScopedLock sl (lock); copy = takes; }
        auto res = finishSampling (copy, job);
        lastDir = job.outDir;
        setStatus (res.error.isNotEmpty() ? res.error
                   : "Done: " + juce::String (res.files.size()) + " samples + " + res.xpm.getFileName() + " (MPC) + " + res.sfz.getFileName()
                     + (job.copyTo.isEmpty() ? juce::String() : "  Copied to the MPC folders."));
    }

    void setStatus (const juce::String& s)
    {
        juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<SamplerPage> (this), s] { if (sp) sp->status->setText (s, juce::dontSendNotification); });
    }

    void tick() override
    {
        bar.setValue (progress.load());
        const juce::ScopedLock sl (lock);
        if (fresh)
        {
            fresh = false;
            shown_.makeCopyOf (last);
            wave.setAudio (&shown_);
            levels.setBarsRange (bars, -48, 0, " dB");
        }
        startBtn->setEnabled (! isThreadRunning());
    }

    void demo() override
    {
        src.refresh();
        shown_.setSize (1, 48000 * 3);
        for (int i = 0; i < shown_.getNumSamples(); ++i)
        {
            double t = i / 48000.0;
            shown_.setSample (0, i, (float) (0.7 * std::exp (-t * 1.2) * std::sin (2 * juce::MathConstants<double>::pi * 110 * t + 1.5 * std::sin (2 * juce::MathConstants<double>::pi * 220 * t))));
        }
        wave.setAudio (&shown_);
        std::vector<BarChart3D::Bar> b;
        for (int n = 36; n <= 84; n += 3) b.push_back ({ noteName (n), -6 - std::abs (n - 60) * 0.25 - (n > 72 ? (n - 72) * 0.4 : 0), true });
        levels.setBarsRange (b, -48, 0, " dB");
        progress = 0.62;
        status->setText ("Sampling D#4  velocity 110   (10 of 17)", juce::dontSendNotification);
    }

    struct Bar : public juce::Component
    {
        double v = 0;
        void setValue (double x) { if (std::abs (x - v) > 0.001) { v = x; repaint(); } }
        void paint (juce::Graphics& g) override { drawProgress (g, getWidth(), getHeight(), v, juce::String ((int) (v * 100)) + "%"); }
    };

    Slab cfg, prog;
    juce::OwnedArray<juce::Label> labels;
    std::vector<std::pair<juce::Label*, juce::Component*>> rows;
    juce::TextEditor name;
    MidiPicker midi { true, "smpMidi" };
    SourcePicker src { engine, "smpSource" };
    juce::ComboBox chan, lo, hi, step, vel;
    juce::Slider noteLen, tail;
    juce::ToggleButton normalize, mpcCopy;
    std::unique_ptr<juce::TextButton> startBtn, stopBtn, testBtn, openBtn;
    std::unique_ptr<juce::Label> status;
    Bar bar; WaveView wave; BarChart3D levels;
    juce::CriticalSection lock;
    std::vector<SampledNote> takes;
    std::vector<BarChart3D::Bar> bars;
    juce::AudioBuffer<float> last, shown_;
    bool fresh = false;
    std::atomic<double> progress { 0 };
    SamplerPlan job; Source* jobSrc = nullptr; juce::String jobMidi; int jobChan = 1;
    juce::File lastDir;
};
}

std::unique_ptr<Page> makeSamplerPage (Engine& e) { return std::make_unique<SamplerPage> (e); }
}
