#include "Pages.h"
#include "MainWindow.h"

namespace tk
{
namespace
{
/** The take as it records: one bar per 256 frames, newest on the right, scrolling once it fills the width. */
class LiveTake : public Screen
{
public:
    std::vector<float> peaks;
    void paintScreen (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        const float mid = r.getCentreY(), half = r.getHeight() * 0.45f;
        g.setColour (col::grid); g.drawHorizontalLine ((int) mid, r.getX(), r.getRight());
        const int cols = juce::jmax (1, (int) r.getWidth());
        // fit everything recorded so far (several peaks per pixel for long takes), so the whole take stays in view
        const int n = (int) peaks.size();
        if (n == 0) { g.setColour (col::text3); g.setFont (font (13)); g.drawText ("listening...", r, juce::Justification::centred); return; }
        const double per = juce::jmax (1.0, (double) n / cols);
        g.setColour (juce::Colour (0xffd8d8d8));
        for (int x = 0; x < cols; ++x)
        {
            const int a = (int) (x * per), b = juce::jmin (n, (int) ((x + 1) * per));
            if (a >= n) break;
            float pk = 0; for (int i = a; i < juce::jmax (a + 1, b); ++i) pk = juce::jmax (pk, peaks[(size_t) i]);
            const float h = juce::jmin (1.0f, pk) * half;
            g.fillRect (r.getX() + x, mid - h, 1.0f, juce::jmax (1.0f, 2 * h));
        }
    }
};

class RecordPage : public Page
{
public:
    RecordPage (Engine& e) : Page (e, "record", "Record", "Record from anything: any input, an Ableton track via BC Link, or whatever the PC is playing.")
    {
        for (auto* s : { &srcSlab, &takeSlab, &jamSlab }) addAndMakeVisible (s);
        srcSlab.setTitle ("Source"); takeSlab.setTitle ("Take"); jamSlab.setTitle ("Jam recorder: never lose an idea");
        srcSlab.addAndMakeVisible (picker);
        pcBtn = makeButton ("Capture PC audio", [this] {
            if (engine.loopbackRunning()) engine.stopLoopback();
            else if (! engine.startLoopback())
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "PC audio",
                    engine.loopbackAvailable() ? "Couldn't open the Windows default output for capture." : "Capturing the PC's own output works on Windows.");
            picker.refresh();
            for (int i = 0; i < picker.getNumItems(); ++i) if (picker.getItemText (i) == "PC audio") picker.setSelectedItemIndex (i);
        }, true);
        srcSlab.addAndMakeVisible (*pcBtn);
        srcSlab.addAndMakeVisible (meter);
        recBtn = makeButton ("Record", [this] { toggleRecord (false); });
        armBtn = makeButton ("Arm: start on sound", [this] { toggleRecord (true); });
        for (auto* b : { recBtn.get(), armBtn.get() }) srcSlab.addAndMakeVisible (b);
        thresh.setRange (-60, -6, 1); thresh.setValue (Settings::get().dbl ("recThresh", -36));
        thresh.setSliderStyle (juce::Slider::LinearHorizontal); thresh.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 22);
        thresh.setTextValueSuffix (" dB");
        thresh.onValueChange = [this] { Settings::get().set ("recThresh", thresh.getValue()); };
        srcSlab.addAndMakeVisible (thresh);
        threshLbl = makeLabel ("Start when louder than", 12, col::text3);
        srcSlab.addAndMakeVisible (*threshLbl);
        maxLen.addItemList ({ "30 s", "1 min", "3 min", "10 min" }, 1);
        maxLen.setSelectedId (Settings::get().num ("recMax", 2), juce::dontSendNotification);
        maxLen.onChange = [this] { Settings::get().set ("recMax", maxLen.getSelectedId()); };
        srcSlab.addAndMakeVisible (maxLen);
        maxLbl = makeLabel ("Longest take", 12, col::text3);
        srcSlab.addAndMakeVisible (*maxLbl);
        state = makeLabel ("", 15, col::text, 1);
        srcSlab.addAndMakeVisible (*state);

        takeSlab.addAndMakeVisible (wave);
        takeSlab.addChildComponent (live);
        wave.editable = false;
        name.setText ("Take", juce::dontSendNotification);
        takeSlab.addAndMakeVisible (name);
        // Play turns into Stop while the take is sounding
        playBtn = makeButton ("Play", [this] {
            if (playingTake && engine.isPlaying()) { engine.stopPlayback(); playingTake = false; return; }
            if (take.getNumSamples()) { engine.play (take, takeSr); playingTake = true; }
        });
        saveBtn = makeButton ("Save WAV", [this] { saveTake(); });
        trimBtn = makeButton ("Trim silence", [this] { trim(); });
        chopBtn = makeButton ("To chop station", [this] { send ("chop"); });
        wavesBtn = makeButton ("To wavetables", [this] { send ("waves"); });
        mangleBtn = makeButton ("To mangle", [this] { send ("mangle"); });
        videoBtn = makeButton ("To video", [this] { send ("video"); });
        for (auto* b : { playBtn.get(), saveBtn.get(), trimBtn.get(), chopBtn.get(), wavesBtn.get(), mangleBtn.get(), videoBtn.get() }) takeSlab.addAndMakeVisible (b);
        info = makeLabel ("", 13, col::text2);
        takeSlab.addAndMakeVisible (*info);

        jamMins.addItemList ({ "2 minutes", "5 minutes", "10 minutes", "15 minutes", "30 minutes" }, 1);
        const int opts[] = { 2, 5, 10, 15, 30 };
        for (int i = 0; i < 5; ++i) if (opts[i] == engine.jamMinutes()) jamMins.setSelectedId (i + 1, juce::dontSendNotification);
        jamMins.onChange = [this] { const int o[] = { 2, 5, 10, 15, 30 }; engine.setJamMinutes (o[jamMins.getSelectedId() - 1]); };
        jamSlab.addAndMakeVisible (jamMins);
        jamLbl = makeLabel ("Keep the last", 13, col::text3);
        jamSlab.addAndMakeVisible (*jamLbl);
        jamName.setTextToShowWhenEmpty ("label (optional), e.g. Phrygian bass idea", col::text3);
        jamSlab.addAndMakeVisible (jamName);
        for (auto [txt, secs] : { std::pair<const char*, double> { "Save last 1 min", 60 }, { "Save last 5 min", 300 }, { "Save everything", 1e9 } })
        {
            double s = secs;
            auto* b = jamBtns.add (makeButton (txt, [this, s] { saveJam (s); }).release());
            jamSlab.addAndMakeVisible (b);
        }
        openJams = makeButton ("Open jams folder", [] { Settings::get().folder ("jamFolder", musicFolder().getChildFile ("Toolkit Jams")).startAsProcess(); });
        jamSlab.addAndMakeVisible (*openJams);
        jamInfo = makeLabel ("", 13, col::text2);
        jamSlab.addAndMakeVisible (*jamInfo);
    }

    void shown() override { picker.refresh(); pcBtn->setToggleState (engine.loopbackRunning(), juce::dontSendNotification); }

    void resized() override
    {
        auto r = getLocalBounds();
        auto bottom = r.removeFromBottom (170);
        auto left = r.removeFromLeft (380);
        srcSlab.setBounds (left);
        takeSlab.setBounds (r);
        jamSlab.setBounds (bottom);

        auto c = srcSlab.content();
        picker.setBounds (c.removeFromTop (34)); c.removeFromTop (8);
        pcBtn->setBounds (c.removeFromTop (40)); c.removeFromTop (12);
        auto mrow = c.removeFromTop (juce::jmax (80, c.getHeight() - 210));
        meter.setBounds (mrow.withSizeKeepingCentre (26, mrow.getHeight()));
        c.removeFromTop (8);
        state->setBounds (c.removeFromTop (24));
        auto br = c.removeFromTop (46);
        recBtn->setBounds (br.removeFromLeft (br.getWidth() / 2 - 4)); br.removeFromLeft (8); armBtn->setBounds (br);
        c.removeFromTop (6);
        threshLbl->setBounds (c.removeFromTop (20)); thresh.setBounds (c.removeFromTop (28));
        auto ml = c.removeFromTop (34); maxLbl->setBounds (ml.removeFromLeft (110)); maxLen.setBounds (ml);

        auto t = takeSlab.content();
        auto tr = t.removeFromTop (34);
        name.setBounds (tr.removeFromLeft (260)); tr.removeFromLeft (10); info->setBounds (tr);
        t.removeFromTop (8);
        auto btns = t.removeFromBottom (44);
        int bw = btns.getWidth() / 7;
        for (auto* b : { playBtn.get(), saveBtn.get(), trimBtn.get(), chopBtn.get(), wavesBtn.get(), mangleBtn.get(), videoBtn.get() }) b->setBounds (btns.removeFromLeft (bw).reduced (2, 0));
        t.removeFromBottom (8);
        wave.setBounds (t);
        live.setBounds (t);

        auto j = jamSlab.content();
        auto j1 = j.removeFromTop (36);
        jamLbl->setBounds (j1.removeFromLeft (100)); jamMins.setBounds (j1.removeFromLeft (160)); j1.removeFromLeft (16);
        jamInfo->setBounds (j1);
        j.removeFromTop (10);
        auto j2 = j.removeFromTop (44);
        // label box takes what the five buttons leave (never less than 160), so nothing runs off the edge
        const int bw2 = juce::jmin (170, (j2.getWidth() - 160 - 10 - 4 * 6) / 4);
        jamName.setBounds (j2.removeFromLeft (juce::jlimit (160, 320, j2.getWidth() - 4 * (bw2 + 6) - 10)).reduced (0, 4)); j2.removeFromLeft (10);
        for (auto* b : jamBtns) { b->setBounds (j2.removeFromLeft (bw2)); j2.removeFromLeft (6); }
        openJams->setBounds (j2.removeFromLeft (bw2));
    }

    void toggleRecord (bool arm)
    {
        auto* s = picker.selected();
        if (s == nullptr) return;
        if (recording != nullptr)
        {
            take = recording->stopCapture();
            live.setVisible (false); wave.setVisible (true);
            takeSr = recording->sr;
            takeName = engine.sourceLabel (recording);
            recording = nullptr;
            wave.setAudio (&take);
            name.setText (safeName (takeName) + " " + juce::Time::getCurrentTime().formatted ("%H-%M"), juce::dontSendNotification);
            updateInfo();
            return;
        }
        const double lens[] = { 30, 60, 180, 600 };
        double mx = lens[juce::jlimit (0, 3, maxLen.getSelectedId() - 1)];
        if (arm) s->armCapture (mx, (float) thresh.getValue()); else s->startCapture (mx);
        recording = s;
        live.peaks.clear();
    }

    void tick() override
    {
        if (auto* s = picker.selected()) meter.setLevels (s->peakDb.load(), s->rmsDb.load());
        if (recording != nullptr)
        {
            int st = recording->captureState();
            if (st == 0 && recording->capturedSeconds() > 0) { toggleRecord (false); return; }   // hit the length limit
            state->setText (st == 1 ? "Armed: waiting for sound..." : "Recording  " + fmtTime (recording->capturedSeconds()), juce::dontSendNotification);
            recBtn->setButtonText ("Stop"); recBtn->setToggleState (true, juce::dontSendNotification);
            armBtn->setEnabled (false);
            recording->capturePeaks (live.peaks);
            if (! live.isVisible()) { live.setVisible (true); wave.setVisible (false); }
            live.repaint();
        }
        else
        {
            state->setText (take.getNumSamples() > 0 ? "Ready" : "Pick a source and hit Record", juce::dontSendNotification);
            recBtn->setButtonText ("Record"); recBtn->setToggleState (false, juce::dontSendNotification);
            armBtn->setEnabled (true);
        }
        if (engine.isPlaying()) wave.setPlayhead (engine.playPosition()); else wave.setPlayhead (-1);
        if (playingTake && ! engine.isPlaying()) playingTake = false;
        playBtn->setButtonText (playingTake ? "Stop" : "Play");
        playBtn->setToggleState (playingTake, juce::dontSendNotification);
        auto all = engine.sources();
        double held = 0;
        for (auto* s : all) held = juce::jmax (held, s->jamFilledSeconds());
        jamInfo->setText ("Holding " + fmtTime (held) + " from " + juce::String (all.size()) + " sources, recording all the time. Silent channels are skipped when saving.", juce::dontSendNotification);
    }

    void updateInfo()
    {
        info->setText (juce::String (take.getNumSamples() / takeSr, 1) + " s   " + juce::String (takeSr / 1000, 1) + " kHz   "
                       + (take.getNumChannels() > 1 ? "stereo" : "mono") + "   peak " + fmtDb (juce::Decibels::gainToDecibels (peakOf (take))) + " dB", juce::dontSendNotification);
    }

    void trim()
    {
        if (take.getNumSamples() == 0) return;
        float th = peakOf (take) * 0.01f;
        int a = juce::jmax (0, firstAbove (take, th) - (int) (0.005 * takeSr)), b = lastAbove (take, th);
        if (b <= a) return;
        b = juce::jmin (take.getNumSamples() - 1, b + (int) (0.05 * takeSr));
        juce::AudioBuffer<float> t (take.getNumChannels(), b - a);
        for (int c = 0; c < t.getNumChannels(); ++c) t.copyFrom (c, 0, take, c, a, b - a);
        take = std::move (t);
        wave.setAudio (&take);
        updateInfo();
    }

    void saveTake()
    {
        if (take.getNumSamples() == 0) return;
        auto dir = Settings::get().folder ("takesFolder", musicFolder().getChildFile ("Toolkit Recordings"));
        auto f = dir.getNonexistentChildFile (safeName (name.getText()), ".wav", false);
        if (writeWav (f, take, takeSr, 24)) f.revealToUser();
    }

    void send (const juce::String& page)
    {
        if (take.getNumSamples() > 0 && main != nullptr) main->sendAudio (page, take, takeSr, name.getText());
    }

    void saveJam (double secs)
    {
        auto dir = engine.saveJam (secs, jamName.getText());
        jamName.clear();
        dir.revealToUser();
    }

    void demo() override
    {
        picker.refresh();
        takeSr = 48000;
        take.setSize (2, 48000 * 6);
        for (int i = 0; i < take.getNumSamples(); ++i)
        {
            double t = i / 48000.0, env = std::exp (-std::fmod (t, 0.75) * 5);
            float v = (float) (env * 0.6 * std::sin (2 * juce::MathConstants<double>::pi * 73.4 * t + 3 * std::sin (2 * juce::MathConstants<double>::pi * 146.8 * t)));
            take.setSample (0, i, v); take.setSample (1, i, v * 0.9f);
        }
        wave.setAudio (&take);
        name.setText ("Hydrasynth growl 21-14", juce::dontSendNotification);
        updateInfo();
    }

    SourcePicker picker { engine, "recSource" };
    Slab srcSlab, takeSlab, jamSlab;
    std::unique_ptr<juce::TextButton> pcBtn, recBtn, armBtn, playBtn, saveBtn, trimBtn, chopBtn, wavesBtn, mangleBtn, videoBtn, openJams;
    juce::OwnedArray<juce::TextButton> jamBtns;
    std::unique_ptr<juce::Label> state, threshLbl, maxLbl, info, jamLbl, jamInfo;
    juce::Slider thresh; juce::ComboBox maxLen, jamMins;
    juce::TextEditor name, jamName;
    LedMeter meter; WaveView wave; LiveTake live;
    bool playingTake = false;
    juce::AudioBuffer<float> take; double takeSr = 48000; juce::String takeName;
    Source* recording = nullptr;
};
}

std::unique_ptr<Page> makeRecordPage (Engine& e) { return std::make_unique<RecordPage> (e); }
}
