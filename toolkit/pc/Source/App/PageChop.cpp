#include "Pages.h"

namespace tk
{
namespace
{
class ChopPage : public Page, public juce::FileDragAndDropTarget, private juce::KeyListener
{
public:
    ChopPage (Engine& e) : Page (e, "chop", "Chop station",
                                 "Finds the hits in a sample, lets you play them on pads, and exports an MPC drum program plus a MIDI file of the groove.")
    {
        for (auto* s : { &top, &pads, &out }) addAndMakeVisible (s);
        top.setTitle ("Sample"); pads.setTitle ("Pads (keys 1-8, Q-I)"); out.setTitle ("Export");
        loadBtn = makeButton ("Open file...", [this] { open(); });
        detectBtn = makeButton ("Find hits", [this] { detect(); });
        even8 = makeButton ("8 equal", [this] { equal (8); });
        even16 = makeButton ("16 equal", [this] { equal (16); });
        playAll = makeButton ("Play all", [this] { if (audio.getNumSamples()) engine.play (audio, sr); });
        for (auto* b : { loadBtn.get(), detectBtn.get(), even8.get(), even16.get(), playAll.get() }) top.addAndMakeVisible (b);
        sens.setRange (0, 1, 0.01); sens.setValue (0.55); sens.setSliderStyle (juce::Slider::LinearHorizontal);
        sens.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        sens.onDragEnd = [this] { detect(); };
        top.addAndMakeVisible (sens);
        sensLbl = makeLabel ("Sensitivity", 12, col::text3);
        top.addAndMakeVisible (*sensLbl);
        top.addAndMakeVisible (wave);
        wave.onSliceClicked = [this] (int i) { playSlice (i); };
        wave.onSlicesEdited = [this] { updatePads(); };
        tipLbl = makeLabel ("Drag markers to move them. Alt-click adds one, right-click a marker removes it.", 12, col::text3);
        top.addAndMakeVisible (*tipLbl);
        for (int i = 0; i < 16; ++i)
        {
            auto* b = padBtns.add (new juce::TextButton (juce::String (i + 1)));
            b->onClick = [this, i] { playSlice (i); };
            pads.addAndMakeVisible (b);
        }
        name.setText ("Chop", juce::dontSendNotification);
        out.addAndMakeVisible (name);
        normalize.setButtonText ("Normalize each slice"); normalize.setToggleState (true, juce::dontSendNotification);
        fades.setButtonText ("Tiny fades (no clicks)"); fades.setToggleState (true, juce::dontSendNotification);
        bpm.setRange (60, 200, 0.5); bpm.setValue (90); bpm.setSliderStyle (juce::Slider::LinearHorizontal); bpm.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 22);
        bpm.setTextValueSuffix (" BPM");
        for (auto* c : std::initializer_list<juce::Component*> { &normalize, &fades, &bpm }) out.addAndMakeVisible (c);
        exportBtn = makeButton ("Export to MPC", [this] { exportAll(); });
        out.addAndMakeVisible (*exportBtn);
        where = makeLabel ("", 12.5f, col::text2);
        where->setJustificationType (juce::Justification::topLeft);
        out.addAndMakeVisible (*where);
        addKeyListener (this);
        setWantsKeyboardFocus (true);
        updateWhere();
    }

    void updateWhere()
    {
        juce::String s = "Goes to:\n";
        for (auto& f : mpcExportFolders ("Toolkit Chops")) s << f.getFullPathName() << "\n";
        s << "\nSlices as WAVs + " << "<name>.xpm drum program (pads A01-A16) + <name>.mid with the original groove.";
        where->setText (s, juce::dontSendNotification);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto bottom = r.removeFromBottom (300);
        top.setBounds (r);
        pads.setBounds (bottom.removeFromLeft (bottom.getWidth() * 55 / 100));
        out.setBounds (bottom);
        auto c = top.content();
        auto bar = c.removeFromTop (42);
        for (auto* b : { loadBtn.get(), detectBtn.get(), even8.get(), even16.get(), playAll.get() }) { b->setBounds (bar.removeFromLeft (130)); bar.removeFromLeft (6); }
        sensLbl->setBounds (bar.removeFromLeft (80)); sens.setBounds (bar.removeFromLeft (220));
        tipLbl->setBounds (c.removeFromBottom (20));
        c.removeFromTop (6);
        wave.setBounds (c);
        auto p = pads.content();
        int w = p.getWidth() / 8, h = p.getHeight() / 2;
        for (int i = 0; i < 16; ++i) padBtns[i]->setBounds (p.getX() + (i % 8) * w, p.getY() + (i / 8) * h, w, h);
        auto o = out.content();
        name.setBounds (o.removeFromTop (34)); o.removeFromTop (6);
        normalize.setBounds (o.removeFromTop (28)); fades.setBounds (o.removeFromTop (28));
        bpm.setBounds (o.removeFromTop (30)); o.removeFromTop (6);
        exportBtn->setBounds (o.removeFromTop (44)); o.removeFromTop (6);
        where->setBounds (o);
    }

    bool isInterestedInFileDrag (const juce::StringArray& f) override { return f.size() == 1; }
    void filesDropped (const juce::StringArray& f, int, int) override { load (juce::File (f[0])); }

    void open()
    {
        chooser = std::make_unique<juce::FileChooser> ("Open a sample", musicFolder(), formats().getWildcardForAllFormats());
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc) {
            if (fc.getResult().existsAsFile()) load (fc.getResult());
        });
    }

    void load (const juce::File& f)
    {
        juce::AudioBuffer<float> b; double s;
        if (! readAudio (f, b, s, 600)) return;
        receiveAudio (b, s, f.getFileNameWithoutExtension());
    }

    void receiveAudio (const juce::AudioBuffer<float>& b, double s, const juce::String& n) override
    {
        audio.makeCopyOf (b); sr = s;
        name.setText (safeName (n), juce::dontSendNotification);
        wave.setAudio (&audio);
        double t = estimateBpm (audio, sr);
        if (t > 0) bpm.setValue (t, juce::dontSendNotification);
        detect();
    }

    void detect()
    {
        if (audio.getNumSamples() == 0) return;
        OnsetOptions o; o.sensitivity = sens.getValue(); o.maxSlices = 16;
        wave.setSlices (detectOnsets (audio, sr, o));
        updatePads();
    }

    void equal (int n)
    {
        if (audio.getNumSamples() == 0) return;
        std::vector<int> s;
        for (int i = 0; i < n; ++i) s.push_back ((int) ((juce::int64) audio.getNumSamples() * i / n));
        wave.setSlices (s);
        updatePads();
    }

    void updatePads()
    {
        int n = (int) wave.getSlices().size();
        for (int i = 0; i < 16; ++i) padBtns[i]->setEnabled (i < n);
    }

    std::pair<int, int> sliceRange (int i) const
    {
        auto& s = wave.getSlices();
        int a = s[(size_t) i], b = i + 1 < (int) s.size() ? s[(size_t) i + 1] : audio.getNumSamples();
        return { a, b };
    }

    void playSlice (int i)
    {
        if (i < 0 || i >= (int) wave.getSlices().size()) return;
        auto [a, b] = sliceRange (i);
        engine.play (audio, sr, a, b - a);
        wave.selected = i; wave.repaint();
        for (int k = 0; k < 16; ++k) padBtns[k]->setToggleState (k == i, juce::dontSendNotification);
        flash = 8;
    }

    bool keyPressed (const juce::KeyPress& k, juce::Component*) override
    {
        const juce::String keys = "12345678qwertyui";
        int i = keys.indexOfChar (juce::CharacterFunctions::toLowerCase (k.getTextCharacter()));
        if (i >= 0) { playSlice (i); return true; }
        if (k == juce::KeyPress::spaceKey) { if (engine.isPlaying()) engine.stopPlayback(); else if (audio.getNumSamples()) engine.play (audio, sr); return true; }
        return false;
    }

    void exportAll()
    {
        auto& s = wave.getSlices();
        if (s.empty()) return;
        auto nm = safeName (name.getText());
        juce::StringArray files;
        std::vector<NoteEvent> groove;
        for (auto& dest : mpcExportFolders ("Toolkit Chops"))
        {
            auto dir = dest.getChildFile (nm);
            dir.createDirectory();
            files.clear(); groove.clear();
            for (int i = 0; i < (int) s.size() && i < 16; ++i)
            {
                auto [a, b] = sliceRange (i);
                juce::AudioBuffer<float> sl (audio.getNumChannels(), b - a);
                for (int c = 0; c < sl.getNumChannels(); ++c) sl.copyFrom (c, 0, audio, c, a, b - a);
                if (normalize.getToggleState()) { float pk = peakOf (sl); if (pk > 1e-4f) sl.applyGain (0.95f / pk); }
                if (fades.getToggleState())
                {
                    int f = juce::jmin (sl.getNumSamples() / 4, (int) (0.003 * sr));
                    sl.applyGainRamp (0, f, 0, 1); sl.applyGainRamp (sl.getNumSamples() - f, f, 1, 0);
                }
                auto file = dir.getChildFile (nm + " " + juce::String (i + 1).paddedLeft ('0', 2) + ".wav");
                writeWav (file, sl, sr, 24);
                files.add (file.getFullPathName());
                double beat = a / sr * bpm.getValue() / 60.0, len = (b - a) / sr * bpm.getValue() / 60.0;
                groove.push_back ({ beat, juce::jmax (0.05, len * 0.95), 37 + i, 110, 10 });
            }
            writeXpmDrum (dir.getChildFile (nm + ".xpm"), nm, files);
            writeMidiFile (dir.getChildFile (nm + ".mid"), { { nm, groove } }, bpm.getValue());
        }
        mpcExportFolders ("Toolkit Chops").getLast().getChildFile (nm).revealToUser();
    }

    void tick() override
    {
        if (engine.isPlaying()) wave.setPlayhead (engine.playPosition());
        else if (flash == 0) wave.setPlayhead (-1);
        if (flash > 0 && --flash == 0) for (auto* b : padBtns) b->setToggleState (false, juce::dontSendNotification);
    }

    void demo() override
    {
        const double rate = 48000;
        juce::AudioBuffer<float> buf (2, (int) (rate * 4));
        buf.clear();
        juce::Random rnd (9);
        const double beat = 60.0 / 92;
        for (int k = 0; k < 16; ++k)
        {
            int at = (int) (k * beat / 2 * rate);
            bool kick = k % 4 == 0, snare = k % 4 == 2;
            for (int i = 0; i < (int) (0.3 * rate) && at + i < buf.getNumSamples(); ++i)
            {
                double t = i / rate, v;
                if (kick) v = 0.9 * std::sin (2 * juce::MathConstants<double>::pi * (45 + 180 * std::exp (-t * 35)) * t) * std::exp (-t * 9);
                else if (snare) v = 0.6 * (rnd.nextFloat() * 2 - 1) * std::exp (-t * 18) + 0.3 * std::sin (2 * juce::MathConstants<double>::pi * 190 * t) * std::exp (-t * 25);
                else v = 0.25 * (rnd.nextFloat() * 2 - 1) * std::exp (-t * 60);
                buf.addSample (0, at + i, (float) v); buf.addSample (1, at + i, (float) v);
            }
        }
        receiveAudio (buf, rate, "Fifth Ward break");
        playSlice (4);
        flash = 100000;
    }

    Slab top, pads, out;
    std::unique_ptr<juce::TextButton> loadBtn, detectBtn, even8, even16, playAll, exportBtn;
    juce::OwnedArray<juce::TextButton> padBtns;
    std::unique_ptr<juce::Label> sensLbl, tipLbl, where;
    juce::Slider sens, bpm;
    juce::TextEditor name;
    juce::ToggleButton normalize, fades;
    WaveView wave;
    juce::AudioBuffer<float> audio; double sr = 48000;
    std::unique_ptr<juce::FileChooser> chooser;
    int flash = 0;
};
}

std::unique_ptr<Page> makeChopPage (Engine& e) { return std::make_unique<ChopPage> (e); }
}
