#include "Pages.h"
#include "ChopCore.h"

namespace tk
{
namespace
{
/** One MPC-style pad: number, what it sounds like, and its key. */
class Pad : public juce::Component
{
public:
    juce::String label, tag, key;
    bool lit = false, has = false, selected = false, changed = false;
    std::function<void()> onHit;
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (3);
        drawKey (g, r, lit, down, hover && has, 8);
        if (! has) { g.setColour (juce::Colours::black.withAlpha (0.55f)); g.fillRoundedRectangle (r, 8); }
        if (selected && ! lit) { g.setColour (col::white.withAlpha (0.8f)); g.drawRoundedRectangle (r.reduced (1.5f), 7, 1.5f); }
        auto ink = lit ? col::black : (has ? col::text : col::text3);
        auto t = r.reduced (9, 6);
        g.setColour (ink.withMultipliedAlpha (0.75f)); g.setFont (monoFont (11));
        g.drawText (label, t, juce::Justification::topLeft);
        g.setColour (ink.withMultipliedAlpha (0.5f));
        g.drawText (key, t, juce::Justification::bottomRight);
        if (changed) g.drawText ("*", t, juce::Justification::topRight);
        g.setColour (ink); g.setFont (font (juce::jmin (26.0f, r.getHeight() * 0.36f), 2));
        g.drawText (tag, r.toNearestInt(), juce::Justification::centred);
    }
    void mouseDown (const juce::MouseEvent&) override { down = true; repaint(); if (has && onHit) onHit(); }
    void mouseUp (const juce::MouseEvent&) override { down = false; repaint(); }
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
private:
    bool down = false, hover = false;
};

class ChopPage : public Page, public juce::FileDragAndDropTarget
{
public:
    ChopPage (Engine& e) : Page (e, "chop", "Chop station",
                                 "Chop a sample by hits, beats, equal parts or by hand, play it on 64 pads, flip it, and send it to the MPC.")
    {
        for (auto* s : { &top, &pads, &slice, &out }) addAndMakeVisible (s);
        top.setTitle ("Sample"); pads.setTitle ("Pads"); slice.setTitle ("Pad"); out.setTitle ("Flip and export");

        // ---- transport + edit row
        loadBtn = makeButton ("Open file...", [this] { open(); });
        playBtn = makeButton ("Play", [this] { playWhole(); });
        stopBtn = makeButton ("Stop", [this] { stop(); });
        loopBtn = makeButton ("Loop", [] {}, true);
        undoBtn = makeButton ("Undo", [this] { undo(); });
        clearBtn = makeButton ("Clear", [this] { if (! audio) return; pushUndo(); setMarkers ({ 0 }, true); });
        fitBtn = makeButton ("Zoom out", [this] { wave.zoomToFit(); });
        snapBtn.setButtonText ("Snap to zero crossings");
        snapBtn.setToggleState (Settings::get().flag ("chopSnap", true), juce::dontSendNotification);
        snapBtn.onClick = [this] { Settings::get().set ("chopSnap", snapBtn.getToggleState()); };
        info = makeLabel ("", 12.5f, col::text2);
        info->setJustificationType (juce::Justification::centredRight);
        for (juce::Component* c : std::initializer_list<juce::Component*> { loadBtn.get(), playBtn.get(), stopBtn.get(), loopBtn.get(), undoBtn.get(),
                                                                             clearBtn.get(), fitBtn.get(), &snapBtn, info.get() })
            top.addAndMakeVisible (c);

        // ---- chop row
        modeLbl = makeLabel ("Chop by", 12, col::text3);
        mode.addItemList ({ "Hits (transients)", "Equal slices", "Beat grid", "By hand (tap)" }, 1);
        styleCombo (mode);
        mode.setSelectedItemIndex (juce::jlimit (0, 3, Settings::get().num ("chopMode", 0)), juce::dontSendNotification);
        mode.onChange = [this] { Settings::get().set ("chopMode", mode.getSelectedItemIndex()); showParam(); chop(); };
        sens.setRange (0, 1, 0.01); sens.setValue (Settings::get().dbl ("chopSens", 0.55), juce::dontSendNotification);
        sens.setSliderStyle (juce::Slider::LinearHorizontal);
        sens.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        sens.onDragEnd = [this] { Settings::get().set ("chopSens", sens.getValue()); chop(); };
        count.addItemList ({ "4", "8", "12", "16", "24", "32", "48", "64" }, 1);
        styleCombo (count); count.setSelectedItemIndex (3, juce::dontSendNotification);
        count.onChange = [this] { chop(); };
        division.addItemList ({ "Every bar", "Every 2 beats", "Every beat", "Every 1/8", "Every 1/16" }, 1);
        styleCombo (division); division.setSelectedItemIndex (2, juce::dontSendNotification);
        division.onChange = [this] { chop(); };
        handLbl = makeLabel ("Play, then press T on each hit. Double-click the wave to add one.", 12, col::text3);
        chopBtn = makeButton ("Chop", [this] { chop(); });
        tapBtn = makeButton ("Tap marker (T)", [this] { tap(); });
        for (juce::Component* c : std::initializer_list<juce::Component*> { modeLbl.get(), &mode, &sens, &count, &division, handLbl.get(), chopBtn.get(), tapBtn.get() })
            top.addAndMakeVisible (c);

        top.addAndMakeVisible (wave);
        wave.zoomable = true;
        wave.onSliceClicked = [this] (int i) { select (i); playSlice (i); };
        wave.onBeforeEdit = [this] { pushUndo(); };
        wave.onSlicesEdited = [this] { setMarkers (wave.getSlices(), false); };
        tipLbl = makeLabel ("Drag a marker to move it, double-click to add one, right-click one to remove it. Mouse wheel zooms, Shift+wheel scrolls.", 12, col::text3);
        top.addAndMakeVisible (*tipLbl);

        // ---- pads
        const char* bankNames[] = { "A", "B", "C", "D" };
        for (int b = 0; b < 4; ++b)
        {
            auto* btn = bankBtns.add (new juce::TextButton (bankNames[b]));
            btn->setClickingTogglesState (true);
            btn->setRadioGroupId (77);
            btn->onClick = [this, b] { bank = b; updatePads(); };
            pads.addAndMakeVisible (btn);
        }
        bankBtns[0]->setToggleState (true, juce::dontSendNotification);
        padMode.addItemList ({ "Mono (each hit cuts the last)", "Poly (hits ring over each other)" }, 1);
        styleCombo (padMode);
        padMode.setSelectedItemIndex (Settings::get().num ("chopPoly", 0), juce::dontSendNotification);
        padMode.onChange = [this] { Settings::get().set ("chopPoly", padMode.getSelectedItemIndex()); };
        pads.addAndMakeVisible (padMode);
        for (int i = 0; i < 16; ++i)
        {
            auto* p = padCtl.add (new Pad());
            p->onHit = [this, i] { int s = bank * 16 + i; select (s); playSlice (s); };
            pads.addAndMakeVisible (p);
        }

        // ---- selected pad
        padTitle = makeLabel ("No pad", 15, col::text, 2);
        tuneLbl = makeLabel ("Tune", 12, col::text3); levelLbl = makeLabel ("Level", 12, col::text3);
        tune.setRange (-24, 24, 1); tune.setTextValueSuffix (" st");
        level.setRange (-24, 12, 0.5); level.setTextValueSuffix (" dB");
        for (auto* s : { &tune, &level })
        {
            s->setSliderStyle (juce::Slider::LinearHorizontal);
            s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 62, 22);
            s->setDoubleClickReturnValue (true, 0);
            s->onDragStart = [this] { pushUndo(); };
            s->onValueChange = [this] { editFx(); };
            s->onDragEnd = [this] { makeFlip (false); if (selected >= 0) playSlice (selected); };
        }
        reverseBtn.setButtonText ("Reverse");
        reverseBtn.onClick = [this] { pushUndo(); editFx(); makeFlip (false); if (selected >= 0) playSlice (selected); };
        padPlay = makeButton ("Play pad", [this] { if (selected >= 0) playSlice (selected); });
        padDelete = makeButton ("Remove marker", [this] { removeSelected(); });
        for (juce::Component* c : std::initializer_list<juce::Component*> { padTitle.get(), tuneLbl.get(), &tune, levelLbl.get(), &level, &reverseBtn, padPlay.get(), padDelete.get() })
            slice.addAndMakeVisible (c);

        // ---- flip + export
        flipStyle.addItemList (chop::flipStyles(), 1);
        styleCombo (flipStyle); flipStyle.setSelectedItemIndex (1, juce::dontSendNotification);
        flipStyle.onChange = [this] { makeFlip (false); };
        bars.addItemList ({ "1 bar", "2 bars", "4 bars" }, 1);
        styleCombo (bars); bars.setSelectedItemIndex (1, juce::dontSendNotification);
        bars.onChange = [this] { makeFlip (false); };
        newFlip = makeButton ("New flip", [this] { makeFlip (true); });
        playFlip = makeButton ("Play flip", [this] { playTheFlip(); });
        flipInfo = makeLabel ("", 12, col::text3);
        flipInfo->setJustificationType (juce::Justification::topLeft);
        bpm.setRange (50, 200, 0.5); bpm.setValue (90); bpm.setSliderStyle (juce::Slider::LinearHorizontal);
        bpm.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 22);
        bpm.setTextValueSuffix (" BPM");
        bpm.onDragEnd = [this] { if (mode.getSelectedItemIndex() == 2) chop(); else makeFlip (false); };
        bpm.onValueChange = [this] { updateInfo(); };
        name.setText ("Chop", juce::dontSendNotification);
        normalize.setButtonText ("Normalize pads"); normalize.setToggleState (true, juce::dontSendNotification);
        fades.setButtonText ("Tiny fades"); fades.setToggleState (true, juce::dontSendNotification);
        exportBtn = makeButton ("Export to MPC", [this] { exportAll(); });
        where = makeLabel ("", 12, col::text2);
        where->setJustificationType (juce::Justification::topLeft);
        for (juce::Component* c : std::initializer_list<juce::Component*> { &flipStyle, &bars, newFlip.get(), playFlip.get(), flipInfo.get(), &bpm, &name,
                                                                             &normalize, &fades, exportBtn.get(), where.get() })
            out.addAndMakeVisible (c);

        setWantsKeyboardFocus (true);
        showParam();
        updatePads();
        updateSlicePanel();
        showWhere();
    }

    ~ChopPage() override { pool.removeAllJobs (true, 20000); }

    // ------------------------------------------------------------ layout
    void resized() override
    {
        auto r = getLocalBounds();
        auto bottom = r.removeFromBottom (juce::jlimit (260, 330, getHeight() * 40 / 100));
        top.setBounds (r);
        int w = bottom.getWidth();
        pads.setBounds (bottom.removeFromLeft (w * 40 / 100));
        slice.setBounds (bottom.removeFromLeft (w * 24 / 100));
        out.setBounds (bottom);

        auto c = top.content();
        auto row1 = c.removeFromTop (36);
        rowLayout (row1, { { loadBtn.get(), 118 }, { playBtn.get(), 72 }, { stopBtn.get(), 72 }, { loopBtn.get(), 72 },
                           { undoBtn.get(), 72 }, { clearBtn.get(), 72 }, { fitBtn.get(), 92 }, { &snapBtn, 196 } }, 6);
        info->setBounds (row1.withTrimmedLeft (118 + 72 * 5 + 92 + 196 + 8 * 6));
        c.removeFromTop (6);
        auto row2 = c.removeFromTop (32);
        modeLbl->setBounds (row2.removeFromLeft (56));
        mode.setBounds (row2.removeFromLeft (180)); row2.removeFromLeft (8);
        auto param = row2.removeFromLeft (juce::jmax (160, row2.getWidth() - 270));
        sens.setBounds (param.withWidth (juce::jmin (260, param.getWidth())));
        count.setBounds (param.withWidth (110)); division.setBounds (param.withWidth (150));
        handLbl->setBounds (param);
        row2.removeFromLeft (8);
        chopBtn->setBounds (row2.removeFromLeft (100)); row2.removeFromLeft (6);
        tapBtn->setBounds (row2.removeFromLeft (150));
        tipLbl->setBounds (c.removeFromBottom (20));
        c.removeFromTop (6);
        wave.setBounds (c);

        auto p = pads.content();
        auto ph = p.removeFromTop (30);
        for (auto* b : bankBtns) { b->setBounds (ph.removeFromLeft (40)); ph.removeFromLeft (4); }
        ph.removeFromLeft (6);
        padMode.setBounds (ph);
        p.removeFromTop (6);
        int pw = p.getWidth() / 4, phh = p.getHeight() / 4;
        for (int i = 0; i < 16; ++i)   // MPC layout: pad 1 bottom left, pad 16 top right
            padCtl[i]->setBounds (p.getX() + (i % 4) * pw, p.getY() + (3 - i / 4) * phh, pw, phh);

        auto s = slice.content();
        padTitle->setBounds (s.removeFromTop (26)); s.removeFromTop (4);
        tuneLbl->setBounds (s.removeFromTop (16)); tune.setBounds (s.removeFromTop (28)); s.removeFromTop (4);
        levelLbl->setBounds (s.removeFromTop (16)); level.setBounds (s.removeFromTop (28)); s.removeFromTop (4);
        reverseBtn.setBounds (s.removeFromTop (28)); s.removeFromTop (6);
        auto sb = s.removeFromTop (36);
        padPlay->setBounds (sb.removeFromLeft (sb.getWidth() / 2 - 3)); sb.removeFromLeft (6); padDelete->setBounds (sb);

        auto o = out.content();
        auto f1 = o.removeFromTop (30);
        flipStyle.setBounds (f1.removeFromLeft (f1.getWidth() - 96)); f1.removeFromLeft (6); bars.setBounds (f1);
        o.removeFromTop (6);
        auto f2 = o.removeFromTop (34);
        newFlip->setBounds (f2.removeFromLeft (f2.getWidth() / 2 - 3)); f2.removeFromLeft (6); playFlip->setBounds (f2);
        o.removeFromTop (2);
        flipInfo->setBounds (o.removeFromTop (34));
        bpm.setBounds (o.removeFromTop (28)); o.removeFromTop (4);
        name.setBounds (o.removeFromTop (30)); o.removeFromTop (4);
        auto tg = o.removeFromTop (26);
        normalize.setBounds (tg.removeFromLeft (tg.getWidth() / 2)); fades.setBounds (tg);
        o.removeFromTop (4);
        exportBtn->setBounds (o.removeFromTop (40)); o.removeFromTop (4);
        where->setBounds (o);
    }

    // ------------------------------------------------------------ loading
    bool isInterestedInFileDrag (const juce::StringArray& f) override { return f.size() == 1; }
    void filesDropped (const juce::StringArray& f, int, int) override { load (juce::File (f[0])); }

    void open()
    {
        chooser = std::make_unique<juce::FileChooser> ("Open a sample", juce::File (Settings::get().str ("chopFolder", musicFolder().getFullPathName())),
                                                       formats().getWildcardForAllFormats());
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc) {
            if (fc.getResult().existsAsFile()) load (fc.getResult());
        });
    }

    void load (const juce::File& f)
    {
        juce::AudioBuffer<float> b; double s;
        if (! readAudio (f, b, s, 600)) { where->setText ("Couldn't read " + f.getFileName() + ".", juce::dontSendNotification); return; }
        Settings::get().set ("chopFolder", f.getParentDirectory().getFullPathName());
        receiveAudio (b, s, f.getFileNameWithoutExtension());
    }

    void receiveAudio (const juce::AudioBuffer<float>& b, double s, const juce::String& n) override
    {
        if (b.getNumSamples() < 64) return;
        stop();
        auto a = std::make_shared<juce::AudioBuffer<float>> (juce::jmin (2, b.getNumChannels()), b.getNumSamples());
        for (int c = 0; c < a->getNumChannels(); ++c) a->copyFrom (c, 0, b, c, 0, b.getNumSamples());
        audio = a; sr = s;
        name.setText (safeName (n), juce::dontSendNotification);
        wave.setAudio (audio.get());
        undoStack.clear();
        fx.clear(); flipSteps.clear(); flipAudio.reset();
        env = {}; analysed = false;
        setMarkers ({ 0 }, false);
        const int gen = ++loadGen;
        info->setText ("Finding hits and tempo...", juce::dontSendNotification);
        auto job = [safe = juce::Component::SafePointer<ChopPage> (this), a, s, gen]
        {
            auto e = onsetEnvelope (*a, s);
            double t = estimateBpm (*a, s);
            juce::MessageManager::callAsync ([safe, e = std::move (e), t, gen] { if (safe != nullptr && safe->loadGen == gen) safe->analysisDone (e, t); });
        };
        if (syncAnalysis) { env = onsetEnvelope (*a, s); analysisDone (env, estimateBpm (*a, s)); }
        else pool.addJob (job);
    }

    void analysisDone (const OnsetEnvelope& e, double t)
    {
        env = e; analysed = true;
        if (t > 0) bpm.setValue (t, juce::dontSendNotification);
        chop (false);
    }

    // ------------------------------------------------------------ chopping
    void showParam()
    {
        int m = mode.getSelectedItemIndex();
        sens.setVisible (m == 0); count.setVisible (m == 1); division.setVisible (m == 2); handLbl->setVisible (m == 3);
        tapBtn->setEnabled (true);
    }

    void chop (bool undoable = true)
    {
        if (! audio) return;
        const int n = audio->getNumSamples();
        std::vector<int> m;
        switch (mode.getSelectedItemIndex())
        {
            case 0:
            {
                if (! analysed) return;   // applied when analysis finishes
                OnsetOptions o; o.sensitivity = sens.getValue(); o.maxSlices = chop::kMaxSlices;
                m = pickOnsets (env, sr, o);
                break;
            }
            case 1: m = chop::equal (n, count.getText().getIntValue()); break;
            case 2:
            {
                const double beats[] = { 4, 2, 1, 0.5, 0.25 };
                int start = analysed && env.firstSound > 0 ? juce::jmax (0, env.firstSound - 64) : 0;
                m = chop::grid (n, sr, bpm.getValue(), beats[juce::jlimit (0, 4, division.getSelectedItemIndex())], start);
                break;
            }
            default: m = { 0 }; break;
        }
        if (m.empty()) m = { 0 };
        if (undoable) pushUndo();
        fx.clear();
        setMarkers (m, true);
    }

    /** Every marker change goes through here: tidy, snap, re-read the hit types, refresh pads. */
    void setMarkers (std::vector<int> m, bool snap)
    {
        if (! audio) return;
        const int n = audio->getNumSamples();
        if (snap && snapBtn.getToggleState()) m = chop::snapToZero (*audio, m, (int) (0.002 * sr));
        m = chop::tidy (m, n, 32);
        if (m.empty()) m = { 0 };
        wave.setSlices (m);
        fx.resize (m.size());
        types.clear();
        std::vector<juce::String> tags;
        for (int i = 0; i < (int) m.size(); ++i)
        {
            auto [a, b] = chop::range (m, i, n);
            types.push_back (chop::classify (*audio, sr, a, b));
            tags.push_back (chop::letter (types.back()));
        }
        wave.setTags (tags);
        if (analysed) makeFlip (false);
        if (selected >= (int) m.size()) selected = (int) m.size() - 1;
        wave.selected = selected;
        updatePads();
        updateSlicePanel();
        updateInfo();
    }

    void tap()
    {
        if (! audio) return;
        int at = -1;
        if (engine.isPlaying() && engine.playingToken() == wholeToken) at = engine.playPosition();
        if (at < 0) { info->setText ("Press Play first, then T on each hit.", juce::dontSendNotification); return; }
        pushUndo();
        auto m = wave.getSlices();
        m.push_back (at);
        setMarkers (m, true);
    }

    void removeSelected()
    {
        auto m = wave.getSlices();
        if (selected <= 0 || selected >= (int) m.size()) return;   // the first marker stays
        pushUndo();
        m.erase (m.begin() + selected);
        fx.erase (fx.begin() + selected);
        selected = selected - 1;
        setMarkers (m, false);
    }

    // ------------------------------------------------------------ undo
    struct Snapshot { std::vector<int> markers; std::vector<chop::SliceFx> fx; };
    void pushUndo()
    {
        undoStack.push_back ({ wave.getSlices(), fx });
        if (undoStack.size() > 100) undoStack.erase (undoStack.begin());
    }
    void undo()
    {
        if (undoStack.empty()) return;
        auto s = undoStack.back(); undoStack.pop_back();
        wave.setSlices (s.markers);
        setMarkers (s.markers, false);
        fx = s.fx; fx.resize (s.markers.size());
        updatePads(); updateSlicePanel();
    }

    // ------------------------------------------------------------ pads
    void select (int i)
    {
        if (i < 0 || i >= (int) wave.getSlices().size()) return;
        selected = i;
        wave.selected = i; wave.repaint();
        if (i / 16 != bank) { bank = i / 16; bankBtns[bank]->setToggleState (true, juce::dontSendNotification); }
        updatePads();
        updateSlicePanel();
    }

    static juce::String padName (int i) { return juce::String::charToString ((juce::juce_wchar) ('A' + i / 16)) + juce::String (i % 16 + 1).paddedLeft ('0', 2); }

    void updatePads()
    {
        static const char* keys[16] = { "Z", "X", "C", "V", "A", "S", "D", "F", "Q", "W", "E", "R", "1", "2", "3", "4" };
        const int n = (int) wave.getSlices().size();
        for (int b = 0; b < 4; ++b) bankBtns[b]->setEnabled (b * 16 < n || b == 0);
        for (int i = 0; i < 16; ++i)
        {
            int s = bank * 16 + i;
            auto* p = padCtl[i];
            p->has = s < n;
            p->label = padName (s);
            p->tag = s < (int) types.size() ? chop::letter (types[(size_t) s]) : "";
            p->key = keys[i];
            p->selected = s == selected;
            p->changed = s < (int) fx.size() && ! (fx[(size_t) s] == chop::SliceFx {});
            p->repaint();
        }
    }

    void updateSlicePanel()
    {
        bool has = selected >= 0 && selected < (int) fx.size();
        for (juce::Component* c : std::initializer_list<juce::Component*> { &tune, &level, &reverseBtn, padPlay.get(), padDelete.get() }) c->setEnabled (has);
        padDelete->setEnabled (has && selected > 0);
        if (! has) { padTitle->setText ("Pick a pad", juce::dontSendNotification); return; }
        auto& f = fx[(size_t) selected];
        tune.setValue (f.semitones, juce::dontSendNotification);
        level.setValue (f.gainDb, juce::dontSendNotification);
        reverseBtn.setToggleState (f.reverse, juce::dontSendNotification);
        auto [a, b] = chop::range (wave.getSlices(), selected, audio ? audio->getNumSamples() : 0);
        padTitle->setText ("Pad " + padName (selected) + "   " + chop::hitName (types[(size_t) selected]) + ", "
                           + juce::String (juce::roundToInt ((b - a) / sr * 1000.0)) + " ms", juce::dontSendNotification);
    }

    void editFx()
    {
        if (selected < 0 || selected >= (int) fx.size()) return;
        auto& f = fx[(size_t) selected];
        f.semitones = tune.getValue(); f.gainDb = (float) level.getValue(); f.reverse = reverseBtn.getToggleState();
        updatePads();
    }

    void updateInfo()
    {
        if (! audio) { info->setText ("", juce::dontSendNotification); return; }
        info->setText (juce::String ((int) wave.getSlices().size()) + " slices    " + fmtTime (audio->getNumSamples() / sr)
                       + "    " + juce::String (bpm.getValue(), 1) + " BPM", juce::dontSendNotification);
    }

    // ------------------------------------------------------------ playback
    void playWhole()
    {
        if (! audio) return;
        PlayOptions o; o.loop = loopBtn->getToggleState(); o.fadeInMs = 0;
        wholeToken = engine.play (audio, sr, o);
        playingFlip = false;
    }

    void playSlice (int i)
    {
        auto& m = wave.getSlices();
        if (! audio || i < 0 || i >= (int) m.size()) return;
        auto [a, b] = chop::range (m, i, audio->getNumSamples());
        if (b - a < 2) return;
        PlayOptions o;
        o.start = a; o.length = b - a;
        if (i < (int) fx.size()) { o.semitones = fx[(size_t) i].semitones; o.gainDb = fx[(size_t) i].gainDb; o.reverse = fx[(size_t) i].reverse; }
        o.loop = loopBtn->getToggleState();
        o.choke = padMode.getSelectedItemIndex() == 0;
        o.fadeInMs = fades.getToggleState() ? 0.7f : 0.0f;
        engine.play (audio, sr, o);
        playingFlip = false;
        if (i / 16 == bank) { padCtl[i % 16]->lit = true; padCtl[i % 16]->repaint(); flash[(size_t) (i % 16)] = 6; }
    }

    void stop() { engine.stopPlayback(); }

    void playTheFlip()
    {
        if (flipAudio == nullptr) makeFlip (false);
        if (flipAudio == nullptr) return;
        PlayOptions o; o.loop = true; o.fadeInMs = 0;
        engine.play (flipAudio, sr, o);
        playingFlip = true;
    }

    // ------------------------------------------------------------ flips
    void makeFlip (bool newSeed)
    {
        if (! audio) return;
        if (newSeed) ++flipSeed;
        int style = flipStyle.getSelectedItemIndex();
        int nBars = 1 << juce::jlimit (0, 2, bars.getSelectedItemIndex());
        flipSteps = chop::makeFlip (style, wave.getSlices(), audio->getNumSamples(), sr, bpm.getValue(), types, nBars, flipSeed);
        auto buf = chop::renderFlip (flipSteps, *audio, sr, wave.getSlices(), fx, bpm.getValue(), nBars, fades.getToggleState());
        flipAudio = std::make_shared<const juce::AudioBuffer<float>> (std::move (buf));
        flipInfo->setText (chop::flipBlurbs()[style] + "  (" + juce::String ((int) flipSteps.size()) + " hits)", juce::dontSendNotification);
        if (playingFlip && engine.isPlaying()) playTheFlip();
    }

    // ------------------------------------------------------------ keys
    bool keyPressed (const juce::KeyPress& k) override
    {
        const juce::String keys = "zxcvasdfqwer1234";
        auto ch = juce::CharacterFunctions::toLowerCase (k.getTextCharacter());
        if (k.getModifiers().isCommandDown() && k.getKeyCode() == 'Z') { undo(); return true; }
        if (! k.getModifiers().isCommandDown())
        {
            int i = keys.indexOfChar (ch);
            if (i >= 0) { int s = bank * 16 + i; select (s); playSlice (s); return true; }
            if (ch == 't' || k == juce::KeyPress::returnKey) { tap(); return true; }
        }
        if (k == juce::KeyPress::spaceKey) { if (engine.isPlaying()) stop(); else playWhole(); return true; }
        if (k == juce::KeyPress::escapeKey) { stop(); return true; }
        if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) { removeSelected(); return true; }
        if (k == juce::KeyPress::leftKey || k == juce::KeyPress::rightKey)
        {
            int s = juce::jlimit (0, (int) wave.getSlices().size() - 1, selected + (k == juce::KeyPress::leftKey ? -1 : 1));
            select (s); playSlice (s); return true;
        }
        return false;
    }

    // ------------------------------------------------------------ export
    void showWhere()
    {
        juce::String s = "Goes to ";
        auto dirs = mpcExportFolders ("Toolkit Chops");
        for (int i = 0; i < dirs.size(); ++i) s << (i ? " and " : "") << dirs[i].getFullPathName();
        s << ": a WAV per pad, an MPC drum program, the groove as MIDI, and the flip as WAV + MIDI.";
        where->setText (s, juce::dontSendNotification);
    }

    void exportAll()
    {
        auto& m = wave.getSlices();
        if (! audio || m.empty()) return;
        auto nm = safeName (name.getText());
        const int n = audio->getNumSamples();
        const double tempo = bpm.getValue();
        juce::Array<juce::File> written;
        bool ok = true;
        for (auto& dest : mpcExportFolders ("Toolkit Chops"))
        {
            auto dir = dest.getChildFile (nm);
            dir.createDirectory();
            juce::StringArray files;
            std::vector<NoteEvent> groove;
            for (int i = 0; i < (int) m.size(); ++i)
            {
                auto [a, b] = chop::range (m, i, n);
                auto sl = chop::render (*audio, sr, a, b, i < (int) fx.size() ? fx[(size_t) i] : chop::SliceFx {}, normalize.getToggleState(), fades.getToggleState());
                auto file = dir.getChildFile (nm + " " + padName (i) + ".wav");
                if (sl.getNumSamples() < 2 || ! writeWav (file, sl, sr, 24)) { ok = false; continue; }
                files.add (file.getFullPathName());
                double beat = (a - m[0]) / sr * tempo / 60.0, len = (b - a) / sr * tempo / 60.0;
                groove.push_back ({ beat, juce::jmax (0.05, len * 0.95), 37 + i, 110, 10 });
            }
            ok = writeXpmDrum (dir.getChildFile (nm + ".xpm"), nm, files) && ok;
            ok = writeMidiFile (dir.getChildFile (nm + " groove.mid"), { { nm, groove } }, tempo) && ok;
            if (flipAudio != nullptr && ! flipSteps.empty())
            {
                std::vector<NoteEvent> notes;
                for (auto& s : flipSteps) notes.push_back ({ s.beat, juce::jmax (0.05, s.length * 0.95), 37 + s.slice, s.velocity, 10 });
                ok = writeMidiFile (dir.getChildFile (nm + " flip.mid"), { { nm + " flip", notes } }, tempo) && ok;
                ok = writeWav (dir.getChildFile (nm + " flip.wav"), *flipAudio, sr, 24) && ok;
            }
            written.add (dir);
        }
        if (written.isEmpty()) return;
        juce::String s = ok ? "Saved " : "Saved, with problems: ";
        s << (int) m.size() << " pads" << (flipAudio != nullptr ? " + flip" : "") << " to ";
        for (int i = 0; i < written.size(); ++i) s << (i ? " and " : "") << written[i].getFullPathName();
        where->setText (s, juce::dontSendNotification);
        written.getLast().revealToUser();
    }

    // ------------------------------------------------------------ tick
    void tick() override
    {
        const int tok = engine.playingToken();
        if (engine.isPlaying() && ! playingFlip && audio && tok != 0) wave.setPlayhead (engine.playPosition());
        else wave.setPlayhead (-1);
        playBtn->setToggleState (engine.isPlaying() && tok == wholeToken, juce::dontSendNotification);
        playFlip->setToggleState (engine.isPlaying() && playingFlip, juce::dontSendNotification);
        for (int i = 0; i < 16; ++i)
            if (flash[(size_t) i] > 0 && --flash[(size_t) i] == 0) { padCtl[i]->lit = false; padCtl[i]->repaint(); }
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
        mode.setSelectedItemIndex (0, juce::dontSendNotification); showParam();
        sens.setValue (0.55, juce::dontSendNotification);
        syncAnalysis = true;
        receiveAudio (buf, rate, "Fifth Ward break");
        syncAnalysis = false;
        if (fx.size() > 6)
        {
            select (6);
            fx[6].semitones = -5; fx[6].reverse = true;
            updatePads(); updateSlicePanel();
        }
        padCtl[4]->lit = true;
    }

    // test hooks
    const std::vector<int>& markers() const { return wave.getSlices(); }
    const std::vector<chop::Hit>& hitTypes() const { return types; }

    Slab top, pads, slice, out;
    std::unique_ptr<juce::TextButton> loadBtn, playBtn, stopBtn, loopBtn, undoBtn, clearBtn, fitBtn, chopBtn, tapBtn,
                                      padPlay, padDelete, newFlip, playFlip, exportBtn;
    juce::OwnedArray<juce::TextButton> bankBtns;
    juce::OwnedArray<Pad> padCtl;
    std::unique_ptr<juce::Label> info, modeLbl, handLbl, tipLbl, padTitle, tuneLbl, levelLbl, flipInfo, where;
    juce::ComboBox mode, count, division, padMode, flipStyle, bars;
    juce::Slider sens, bpm, tune, level;
    juce::TextEditor name;
    juce::ToggleButton snapBtn, reverseBtn, normalize, fades;
    WaveView wave;

    std::shared_ptr<juce::AudioBuffer<float>> audio;
    double sr = 48000;
    OnsetEnvelope env;
    bool analysed = false, syncAnalysis = false, playingFlip = false;
    int loadGen = 0, selected = -1, bank = 0, flipSeed = 1, wholeToken = -1;
    std::vector<chop::SliceFx> fx;
    std::vector<chop::Hit> types;
    std::vector<Snapshot> undoStack;
    std::vector<chop::Step> flipSteps;
    SharedAudio flipAudio;
    std::array<int, 16> flash {};
    std::unique_ptr<juce::FileChooser> chooser;
    juce::ThreadPool pool { 1 };
};
}

std::unique_ptr<Page> makeChopPage (Engine& e) { return std::make_unique<ChopPage> (e); }
}
