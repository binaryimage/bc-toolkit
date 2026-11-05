#include "Pages.h"
#include "MainWindow.h"
#include "Common/Mangle.h"
#include "Common/Dsp.h"

namespace tk
{
namespace
{
class ManglePage : public Page, public juce::FileDragAndDropTarget, private juce::Thread
{
public:
    ManglePage (Engine& e) : Page (e, "mangle", "Mangle",
                                   "Smear a sound into a drone with an extreme time stretch, then rot it like an old cassette or a chewed VHS."),
                             juce::Thread ("mangle")
    {
        for (auto* s : { &src, &fx, &res }) addAndMakeVisible (s);
        src.setTitle ("Source"); fx.setTitle ("Treatment"); res.setTitle ("Result");
        openBtn = makeButton ("Open file...", [this] { open(); });
        playIn = makeButton ("Play source", [this] { if (input.getNumSamples()) engine.play (input, sr); });
        for (auto* b : { openBtn.get(), playIn.get() }) src.addAndMakeVisible (b);
        info = makeLabel ("Drop a file here, or send a take from Record.", 12.5f, col::text2);
        src.addAndMakeVisible (*info);
        src.addAndMakeVisible (inView); inView.editable = false;

        stretchOn.setButtonText ("Time stretch"); stretchOn.setToggleState (true, juce::dontSendNotification);
        rotOn.setButtonText ("Tape rot"); rotOn.setToggleState (true, juce::dontSendNotification);
        reverse.setButtonText ("Reverse first");
        for (auto* t : { &stretchOn, &rotOn, &reverse }) fx.addAndMakeVisible (t);
        addKnob (factor, "Stretch", 1, 50, 8, "x");
        addKnob (window, "Smear", 0.05, 1.0, 0.25, " s");
        addKnob (wow, "Wow", 0, 1, 0.4);
        addKnob (flutter, "Flutter", 0, 1, 0.3);
        addKnob (drift, "Drift", 0, 1, 0.3);
        addKnob (hiss, "Hiss", 0, 1, 0.25);
        addKnob (drop, "Dropouts", 0, 1, 0.2);
        addKnob (tone, "Tone", 0, 1, 0.5);
        addKnob (sat, "Saturate", 0, 1, 0.4);
        addKnob (crush, "Crush", 0, 1, 0);
        factor.setSkewFactorFromMidPoint (8);

        presetLbl = makeLabel ("Starting points", 12, col::text3); fx.addAndMakeVisible (*presetLbl);
        const char* names[] = { "Dust cassette", "VHS ghost", "Deep freeze", "Dead battery", "Clean stretch" };
        for (int p = 0; p < 5; ++p) { auto b = makeButton (names[p], [this, p] { preset (p); }); fx.addAndMakeVisible (*b); presets.push_back (std::move (b)); }
        renderBtn = makeButton ("Mangle it", [this] { go(); });
        fx.addAndMakeVisible (*renderBtn);
        fx.addAndMakeVisible (pill);

        playOut = makeButton ("Play result", [this] { if (output.getNumSamples()) engine.play (output, sr); });
        stopBtn = makeButton ("Stop", [this] { engine.stopPlayback(); });
        saveBtn = makeButton ("Save WAV", [this] { save(); });
        again = makeButton ("Mangle again (use result)", [this] { if (output.getNumSamples()) { auto o = output; setInput (o, sr, name + " x2"); } });
        toChop = makeButton ("To chop station", [this] { if (output.getNumSamples() && main) main->sendAudio ("chop", output, sr, name + " mangled"); });
        toWaves = makeButton ("To wavetables", [this] { if (output.getNumSamples() && main) main->sendAudio ("waves", output, sr, name + " mangled"); });
        for (auto* b : { playOut.get(), stopBtn.get(), saveBtn.get(), again.get(), toChop.get(), toWaves.get() }) res.addAndMakeVisible (b);
        res.addAndMakeVisible (outView); outView.editable = false;
        saved = makeLabel ("", 12, col::text3); res.addAndMakeVisible (*saved);
    }

    ~ManglePage() override { stopThread (4000); }

    void addKnob (juce::Slider& s, const juce::String& label, double lo, double hi, double def, const juce::String& suffix = {})
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setRange (lo, hi, 0.01); s.setValue (def, juce::dontSendNotification);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 18);
        s.setTextValueSuffix (suffix);
        s.setDoubleClickReturnValue (true, def);
        fx.addAndMakeVisible (s);
        auto l = makeLabel (label, 12, col::text2);
        l->setJustificationType (juce::Justification::centred);
        fx.addAndMakeVisible (*l);
        knobs.push_back ({ &s, std::move (l) });
    }

    void preset (int p)
    {
        auto set = [] (juce::Slider& s, double v) { s.setValue (v, juce::dontSendNotification); };
        switch (p)
        {
            case 0: stretchOn.setToggleState (false, juce::dontSendNotification); rotOn.setToggleState (true, juce::dontSendNotification);
                    set (wow, 0.35); set (flutter, 0.45); set (drift, 0.2); set (hiss, 0.35); set (drop, 0.25); set (tone, 0.35); set (sat, 0.5); set (crush, 0); break;
            case 1: stretchOn.setToggleState (false, juce::dontSendNotification); rotOn.setToggleState (true, juce::dontSendNotification);
                    set (wow, 0.7); set (flutter, 0.2); set (drift, 0.6); set (hiss, 0.2); set (drop, 0.55); set (tone, 0.2); set (sat, 0.3); set (crush, 0.25); break;
            case 2: stretchOn.setToggleState (true, juce::dontSendNotification); rotOn.setToggleState (true, juce::dontSendNotification);
                    set (factor, 20); set (window, 0.5); set (wow, 0.2); set (flutter, 0.1); set (drift, 0.4); set (hiss, 0.15); set (drop, 0.05); set (tone, 0.3); set (sat, 0.25); set (crush, 0); break;
            case 3: stretchOn.setToggleState (false, juce::dontSendNotification); rotOn.setToggleState (true, juce::dontSendNotification);
                    set (wow, 1.0); set (flutter, 0.8); set (drift, 1.0); set (hiss, 0.4); set (drop, 0.7); set (tone, 0.15); set (sat, 0.8); set (crush, 0.5); break;
            case 4: stretchOn.setToggleState (true, juce::dontSendNotification); rotOn.setToggleState (false, juce::dontSendNotification);
                    set (factor, 8); set (window, 0.25); break;
        }
    }

    bool isInterestedInFileDrag (const juce::StringArray& f) override { return f.size() == 1; }
    void filesDropped (const juce::StringArray& f, int, int) override { load (juce::File (f[0])); }

    void open()
    {
        chooser = std::make_unique<juce::FileChooser> ("Open a sound", musicFolder(), formats().getWildcardForAllFormats());
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc) {
            if (fc.getResult().existsAsFile()) load (fc.getResult());
        });
    }

    void load (const juce::File& f)
    {
        juce::AudioBuffer<float> b; double s;
        if (readAudio (f, b, s, 120)) setInput (b, s, f.getFileNameWithoutExtension());
    }

    void receiveAudio (const juce::AudioBuffer<float>& b, double s, const juce::String& n) override { setInput (b, s, n); }

    void setInput (const juce::AudioBuffer<float>& b, double s, const juce::String& n)
    {
        if (isThreadRunning()) return;
        juce::AudioBuffer<float> copy; copy.makeCopyOf (b);
        input = std::move (copy); sr = s; name = n;
        inView.setAudio (&input);
        info->setText (n + "   " + fmtTime (input.getNumSamples() / sr) + "   " + juce::String (sr / 1000, 1) + " kHz", juce::dontSendNotification);
    }

    void go()
    {
        if (input.getNumSamples() == 0 || isThreadRunning()) return;
        job.stretch = stretchOn.getToggleState(); job.rot = rotOn.getToggleState(); job.rev = reverse.getToggleState();
        job.so = { factor.getValue(), window.getValue(), (unsigned) juce::Random::getSystemRandom().nextInt (1 << 20) };
        job.ro.wow = wow.getValue(); job.ro.flutter = flutter.getValue(); job.ro.drift = drift.getValue();
        job.ro.hiss = hiss.getValue(); job.ro.dropouts = drop.getValue(); job.ro.tone = tone.getValue();
        job.ro.saturation = sat.getValue(); job.ro.crush = crush.getValue(); job.ro.seed = job.so.seed;
        if (job.stretch && input.getNumSamples() / sr * job.so.factor > 600)
        {
            pill.setValue (0, "That would be over 10 minutes long. Lower the stretch or trim the source.");
            return;
        }
        progress = 0;
        startThread();
    }

    void run() override
    {
        juce::AudioBuffer<float> x; x.makeCopyOf (input);
        if (job.rev) for (int c = 0; c < x.getNumChannels(); ++c) x.reverse (c, 0, x.getNumSamples());
        if (job.stretch)
            x = mangle::stretch (x, sr, job.so, [this] (float p) { progress = p * (job.rot ? 0.85 : 1.0); return ! threadShouldExit(); });
        if (threadShouldExit()) return;
        if (job.rot) { progress = job.stretch ? 0.88 : 0.3; x = mangle::tapeRot (x, sr, job.ro); }
        float pk = peakOf (x);
        if (pk > 1e-5f) x.applyGain (0.9f / pk);
        const juce::ScopedLock sl (lock);
        result = std::move (x);
        progress = 1;
        fresh = true;
    }

    void tick() override
    {
        double p = progress.load();
        pill.setValue (p, isThreadRunning() ? "Working... " + juce::String ((int) (p * 100)) + "%" : p >= 1 ? "Done" : juce::String());
        renderBtn->setEnabled (! isThreadRunning());
        {
            const juce::ScopedLock sl (lock);
            if (fresh)
            {
                fresh = false;
                output = std::move (result);
                outView.setAudio (&output);
                saved->setText (fmtTime (output.getNumSamples() / sr) + " long. Nothing is saved until you press Save WAV.", juce::dontSendNotification);
            }
        }
        if (engine.isPlaying()) { outView.setPlayhead (engine.playPosition()); }
    }

    void save()
    {
        if (output.getNumSamples() == 0) return;
        auto dir = musicFolder().getChildFile ("Toolkit Mangled");
        dir.createDirectory();
        auto f = dir.getNonexistentChildFile (safeName (name) + " mangled", ".wav");
        writeWav (f, output, sr, 24);
        saved->setText ("Saved " + f.getFullPathName(), juce::dontSendNotification);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        // the result panel needs ~180 for its buttons and waveform; source and effects give up height to make it
        const int spare = r.getHeight() - 20 - 180;
        auto top = r.removeFromTop (juce::jlimit (150, 230, spare - 290));
        src.setBounds (top);
        r.removeFromTop (10);
        auto mid = r.removeFromTop (juce::jlimit (220, 290, r.getHeight() - 10 - 180));
        fx.setBounds (mid);
        r.removeFromTop (10);
        res.setBounds (r);

        auto s = src.content();
        auto bar = s.removeFromTop (40);
        rowLayout (bar, { { openBtn.get(), 130 }, { playIn.get(), 130 }, { info.get(), 600 } });
        s.removeFromTop (6); inView.setBounds (s);

        auto f = fx.content();
        auto left = f.removeFromLeft (190);
        stretchOn.setBounds (left.removeFromTop (30)); rotOn.setBounds (left.removeFromTop (30)); reverse.setBounds (left.removeFromTop (30));
        left.removeFromTop (12);
        renderBtn->setBounds (left.removeFromTop (48));
        left.removeFromTop (8);
        pill.setBounds (left.removeFromTop (26));
        auto right = f.removeFromRight (200);
        presetLbl->setBounds (right.removeFromTop (20));
        const int ph = juce::jlimit (24, 40, right.getHeight() / juce::jmax (1, (int) presets.size()) - 2);   // all presets fit the panel
        for (auto& b : presets) { b->setBounds (right.removeFromTop (ph)); right.removeFromTop (2); }
        f.removeFromLeft (10); f.removeFromRight (10);
        // two rows of knobs: stretch pair first, then the rot set
        int kw = f.getWidth() / 5, kh = f.getHeight() / 2;
        for (size_t i = 0; i < knobs.size(); ++i)
        {
            auto cell = juce::Rectangle<int> (f.getX() + (int) (i % 5) * kw, f.getY() + (int) (i / 5) * kh, kw, kh);
            knobs[i].second->setBounds (cell.removeFromTop (18));
            knobs[i].first->setBounds (cell.reduced (8, 2));
        }

        auto o = res.content();
        auto ob = o.removeFromTop (40);
        rowLayout (ob, { { playOut.get(), 120 }, { stopBtn.get(), 80 }, { saveBtn.get(), 110 }, { again.get(), 220 }, { toChop.get(), 150 }, { toWaves.get(), 150 } });
        saved->setBounds (o.removeFromBottom (18));
        o.removeFromTop (6); outView.setBounds (o);
    }

    void demo() override
    {
        double rate = 48000;
        juce::AudioBuffer<float> b (2, (int) (rate * 2.5));
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            double t = i / rate, env = std::exp (-t * 1.4) * (1 - std::exp (-t * 60));
            double v = 0.6 * env * (std::sin (2 * juce::MathConstants<double>::pi * 73.4 * t) + 0.5 * std::sin (2 * juce::MathConstants<double>::pi * 146.8 * t + std::sin (t * 7)));
            b.setSample (0, i, (float) v); b.setSample (1, i, (float) v);
        }
        setInput (b, rate, "2600 drone D");
        preset (2);
        job.stretch = true; job.rot = true; job.rev = false;
        job.so = { 6.0, 0.4, 3 };
        job.ro.wow = 0.2; job.ro.drift = 0.4; job.ro.hiss = 0.15; job.ro.tone = 0.3; job.ro.seed = 3;
        run();   // synchronously, for the screenshot
        tick();
    }

    struct Job { bool stretch = true, rot = true, rev = false; mangle::StretchOptions so; mangle::RotOptions ro; } job;

    Slab src, fx, res;
    std::unique_ptr<juce::TextButton> openBtn, playIn, renderBtn, playOut, stopBtn, saveBtn, again, toChop, toWaves;
    std::vector<std::unique_ptr<juce::TextButton>> presets;
    std::unique_ptr<juce::Label> info, presetLbl, saved;
    juce::ToggleButton stretchOn, rotOn, reverse;
    juce::Slider factor, window, wow, flutter, drift, hiss, drop, tone, sat, crush;
    std::vector<std::pair<juce::Slider*, std::unique_ptr<juce::Label>>> knobs;
    ProgressPill pill;
    WaveView inView, outView;
    juce::AudioBuffer<float> input, output, result;
    double sr = 48000; juce::String name = "sound";
    std::unique_ptr<juce::FileChooser> chooser;
    juce::CriticalSection lock;
    std::atomic<double> progress { 0 };
    bool fresh = false;
};
}

std::unique_ptr<Page> makeManglePage (Engine& e) { return std::make_unique<ManglePage> (e); }
}
