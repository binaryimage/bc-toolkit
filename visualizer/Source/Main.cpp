// BC Visualizer: turn a track into a YouTube-ready MP4 with one of its audio-reactive styles.
#include "Session.h"
#include "Common/Look.h"
#include "Common/Widgets.h"
#include "Common/LayoutCheck.h"

namespace bcv
{
using namespace tk;

static juce::PropertiesFile& prefs()
{
    static std::unique_ptr<juce::PropertiesFile> p;
    if (! p)
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "BC Visualizer"; o.folderName = "BC Visualizer"; o.filenameSuffix = ".settings";
        o.osxLibrarySubFolder = "Application Support";
        p = std::make_unique<juce::PropertiesFile> (o);
    }
    return *p;
}

static juce::File outputFolder() { return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("BC Visualizer"); }

// ------------------------------------------------------------ preview

class Preview : public Screen
{
public:
    juce::Image frame;
    juce::String overlay;                 // "Rendering 42%" while an export runs and the preview is paused
    std::function<void()> onDoubleClick;
    juce::Rectangle<int> videoArea() const
    {
        auto a = screenArea().reduced (6);
        float w = juce::jmin (a.getWidth(), a.getHeight() * 16.0f / 9.0f);
        return a.withSizeKeepingCentre (w, w * 9.0f / 16.0f).toNearestInt();
    }
    void paintScreen (juce::Graphics& g, juce::Rectangle<float>) override
    {
        // the preview renders at a lighter resolution and is scaled up here (the export renders at full size)
        if (frame.isValid()) { g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality); g.drawImage (frame, videoArea().toFloat()); }
        if (overlay.isNotEmpty())
        {
            auto r = videoArea().toFloat();
            auto pill = juce::Rectangle<float> (r.getX() + 12, r.getY() + 12, 190, 30);
            g.setColour (juce::Colours::black.withAlpha (0.65f)); g.fillRoundedRectangle (pill, 6);
            g.setColour (col::text); g.setFont (font (13.5f, 2));
            g.drawText (overlay, pill, juce::Justification::centred);
        }
    }
    void mouseDoubleClick (const juce::MouseEvent&) override { if (onDoubleClick) onDoubleClick(); }
};

/** The video filling the whole screen (F11 or double-click; Esc, F11 or double-click again to leave). */
class FullView : public juce::Component
{
public:
    juce::Image frame;
    std::function<void()> onExit;
    juce::Rectangle<int> videoArea() const
    {
        auto a = getLocalBounds();
        int w = juce::jmin (a.getWidth(), a.getHeight() * 16 / 9);
        return a.withSizeKeepingCentre (w, w * 9 / 16);
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black);
        if (frame.isValid()) { g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality); g.drawImage (frame, videoArea().toFloat()); }
    }
    void mouseDoubleClick (const juce::MouseEvent&) override { if (onExit) onExit(); }
};

/** Which style plays when: one block per style, fades shaded, hard cuts as bright lines. Click to jump there. */
class Timeline : public juce::Component
{
public:
    const Plan* plan = nullptr;
    double duration = 0, playhead = 0;
    juce::String note;
    std::function<void (double)> onSeek;

    void setPlayhead (double t) { if (std::abs (t - playhead) > 0.02) { playhead = t; repaint(); } }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        drawScreen (g, r, 5);
        auto in = r.reduced (3);
        if (note.isNotEmpty() || plan == nullptr || duration <= 0 || plan->steps.empty())
        {
            g.setColour (col::text3); g.setFont (font (12));
            g.drawText (note, in.reduced (6, 0), juce::Justification::centredLeft);
            return;
        }
        auto xAt = [&] (double t) { return in.getX() + in.getWidth() * (float) juce::jlimit (0.0, 1.0, t / duration); };
        const auto& st = plan->steps;
        for (size_t i = 0; i < st.size(); ++i)
        {
            const double a = st[i].fadeStart + st[i].fade, b = i + 1 < st.size() ? st[i + 1].fadeStart : duration;
            auto seg = juce::Rectangle<float> (xAt (a), in.getY(), xAt (b) - xAt (a), in.getHeight());
            g.setColour (col::white.withAlpha (i % 2 ? 0.10f : 0.20f));
            g.fillRect (seg);
            if (st[i].fade > 0)
            {
                auto fr = juce::Rectangle<float> (xAt (st[i].fadeStart), in.getY(), xAt (a) - xAt (st[i].fadeStart), in.getHeight());
                g.setGradientFill (juce::ColourGradient (col::white.withAlpha (i % 2 ? 0.20f : 0.10f), fr.getX(), 0,
                                                         col::white.withAlpha (i % 2 ? 0.10f : 0.20f), fr.getRight(), 0, false));
                g.fillRect (fr);
            }
            else if (i > 0) { g.setColour (col::white.withAlpha (0.85f)); g.fillRect (xAt (a) - 1, in.getY(), 2.0f, in.getHeight()); }
            if (seg.getWidth() > 22)
            {
                g.setColour (col::text2); g.setFont (monoFont (11));
                g.drawText (juce::String (st[i].style + 1), seg.reduced (3, 0), juce::Justification::centredLeft, false);
            }
        }
        g.setColour (col::white);
        g.fillRect (xAt (playhead) - 1, r.getY(), 2.0f, r.getHeight());
    }
    void mouseDrag (const juce::MouseEvent& e) override { if (duration > 0) setPlayhead (duration * juce::jlimit (0.0, 1.0, (double) e.x / juce::jmax (1, getWidth()))); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (duration > 0 && onSeek) onSeek (duration * juce::jlimit (0.0, 1.0, (double) e.x / juce::jmax (1, getWidth())));
    }
};

// ------------------------------------------------------------ style list

class StyleList : public juce::ListBoxModel
{
public:
    std::vector<std::unique_ptr<Viz>>* all = nullptr;
    std::function<void (int)> onPick;
    int getNumRows() override { return all ? (int) all->size() : 0; }
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
    {
        auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (2, 1.5f);
        if (selected) drawKey (g, r, true, false, false, 6);
        g.setColour (selected ? col::black.withAlpha (0.5f) : col::text3);
        g.setFont (monoFont (12));
        g.drawText (juce::String (row + 1).paddedLeft ('0', 2), r.withWidth (34).translated (8, 0), juce::Justification::centredLeft);
        g.setColour (selected ? col::black : col::text2);
        g.setFont (font (14.5f, selected ? 2 : 0));
        g.drawText ((*all)[(size_t) row]->name(), r.withTrimmedLeft (42), juce::Justification::centredLeft);
    }
    void selectedRowsChanged (int row) override { if (row >= 0 && onPick) onPick (row); }
};

/** Click styles to add them to the pattern (numbered in the order picked); click again to take one out. */
class PickList : public juce::ListBoxModel
{
public:
    std::vector<std::unique_ptr<Viz>>* all = nullptr;
    std::vector<int>* picks = nullptr;
    std::function<void()> onChange;
    int getNumRows() override { return all ? (int) all->size() : 0; }
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool) override
    {
        auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (2, 1.5f);
        const auto it = std::find (picks->begin(), picks->end(), row);
        const bool on = it != picks->end();
        auto box = r.removeFromLeft (34).withSizeKeepingCentre (24, 24);
        if (on) drawKey (g, box, true, false, false, 5); else drawScreen (g, box, 5);
        if (on)
        {
            g.setColour (col::black); g.setFont (monoFont (12));
            g.drawText (juce::String ((int) (it - picks->begin()) + 1), box, juce::Justification::centred);
        }
        g.setColour (col::text3); g.setFont (monoFont (12));
        g.drawText (juce::String (row + 1).paddedLeft ('0', 2), r.removeFromLeft (30).translated (4, 0), juce::Justification::centredLeft);
        g.setColour (on ? col::text : col::text2); g.setFont (font (14.5f, on ? 2 : 0));
        g.drawText ((*all)[(size_t) row]->name(), r, juce::Justification::centredLeft);
    }
    void listBoxItemClicked (int row, const juce::MouseEvent&) override
    {
        auto it = std::find (picks->begin(), picks->end(), row);
        if (it != picks->end()) picks->erase (it); else picks->push_back (row);
        if (onChange) onChange();
    }
};

/** Sits over the style list while you pick styles for a pattern. */
class PickerPanel : public juce::Component
{
public:
    PickerPanel()
    {
        title = makeLabel ("PICK STYLES", 12, col::text3, 2);
        hint = makeLabel ("Click the styles you want, in the order you want them. Click again to take one out.", 12.5f, col::text2);
        hint->setJustificationType (juce::Justification::topLeft);
        count = makeLabel ({}, 12, col::text3, 3);
        count->setJustificationType (juce::Justification::centredRight);
        list.setRowHeight (30);
        list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        allBtn = makeButton ("All", [this] { picks->clear(); for (int i = 0; i < model.getNumRows(); ++i) picks->push_back (i); changed(); });
        noneBtn = makeButton ("None", [this] { picks->clear(); changed(); });
        doneBtn = makeButton ("Done", [this] { if (onDone) onDone(); });
        for (juce::Component* c : std::initializer_list<juce::Component*> { title.get(), hint.get(), count.get(), &list, allBtn.get(), noneBtn.get(), doneBtn.get() }) addAndMakeVisible (c);
    }
    ~PickerPanel() override { list.setModel (nullptr); }
    void attach (std::vector<std::unique_ptr<Viz>>& all, std::vector<int>& p)
    {
        model.all = &all; model.picks = &p; picks = &p;
        model.onChange = [this] { changed(); };
        list.setModel (&model);
    }
    void refresh() { list.updateContent(); list.repaint(); count->setText (juce::String ((int) picks->size()) + " picked", juce::dontSendNotification); }
    void paint (juce::Graphics& g) override { g.fillAll (col::bg); drawScreen (g, getLocalBounds().toFloat().reduced (1), 8); }
    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 8);
        auto top = r.removeFromTop (22); count->setBounds (top.removeFromRight (90)); title->setBounds (top);
        hint->setBounds (r.removeFromTop (36)); r.removeFromTop (4);
        auto b = r.removeFromBottom (32); r.removeFromBottom (8);
        const int bw = (b.getWidth() - 12) / 3;
        allBtn->setBounds (b.removeFromLeft (bw)); b.removeFromLeft (6);
        noneBtn->setBounds (b.removeFromLeft (bw)); b.removeFromLeft (6);
        doneBtn->setBounds (b);
        list.setBounds (r);
    }
    std::function<void()> onDone, onChange;
private:
    PickList model;
    std::vector<int>* picks = nullptr;
    juce::ListBox list;
    std::unique_ptr<juce::Label> title, hint, count;
    std::unique_ptr<juce::TextButton> allBtn, noneBtn, doneBtn;
    void changed() { refresh(); if (onChange) onChange(); }
};

// ------------------------------------------------------------ main component

class MainComponent : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    MainComponent()
    {
        all = makeAll();
        loadPrefs();

        addAndMakeVisible (stylesSlab); addAndMakeVisible (previewSlab); addAndMakeVisible (settingsSlab);
        stylesModel.all = &all;
        stylesModel.onPick = [this] (int i)
        {
            look.style = i; all[(size_t) i]->reset(); blurb->setText (all[(size_t) i]->blurb(), juce::dontSendNotification); savePrefs();
            if (patterns.mode == Patterns::single) patternsChanged();
            redrawIfPaused();
        };
        styles.setModel (&stylesModel);
        styles.setRowHeight (30);
        styles.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        addAndMakeVisible (styles);

        addAndMakeVisible (preview);
        preview.onDoubleClick = [this] { setFullscreen (true); };
        addChildComponent (full);
        full.onExit = [this] { setFullscreen (false); };
        addAndMakeVisible (timeline);
        timeline.onSeek = [this] (double t) { seek (t); };
        fullBtn = makeButton ("Full screen", [this] { setFullscreen (true); });
        fullBtn->setTooltip ("F11 or double-click the preview. Esc to come back.");
        openBtn = makeButton ("Open track...", [this] { chooseTrack(); });
        playBtn = makeButton ("Play", [this] { togglePlay(); });
        stopBtn = makeButton ("Stop", [this] { stop(); });
        trackLabel = makeLabel ("No track loaded. Open one or drop a file here.", 14, col::text2, 1);
        timeLabel = makeLabel ("0:00 / 0:00", 13, col::text3, 3);
        timeLabel->setJustificationType (juce::Justification::centredRight);
        position.setSliderStyle (juce::Slider::LinearHorizontal);
        position.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        position.setRange (0, 1, 0);
        position.onDragStart = [this] { scrubbing = true; };
        position.onDragEnd = [this] { scrubbing = false; seek (position.getValue() * track.seconds()); };
        for (juce::Component* c : std::initializer_list<juce::Component*> { openBtn.get(), playBtn.get(), stopBtn.get(), fullBtn.get(), trackLabel.get(), timeLabel.get(), &position }) addAndMakeVisible (c);

        // settings: Look / Effects / Patterns tabs (scrolling if the window is short), export pinned underneath
        for (auto* t : { &lookTab, &fxTab, &patTab })
        {
            t->setRadioGroupId (41); t->setClickingTogglesState (true);
            t->onClick = [this] { prefs().setValue ("tab", currentTab()); showTab(); };
            addAndMakeVisible (t);
        }
        lookTab.setButtonText ("Look"); fxTab.setButtonText ("Effects"); patTab.setButtonText ("Patterns");
        {
            const int t = prefs().getIntValue ("tab", 0);
            (t == 1 ? fxTab : t == 2 ? patTab : lookTab).setToggleState (true, juce::dontSendNotification);
        }
        addAndMakeVisible (paneView);
        paneView.setScrollBarsShown (true, false);
        paneView.setScrollBarThickness (8);

        auto label = [this] (const juce::String& s, juce::Component& parent) { auto l = makeLabel (s, 12, col::text3, 1); parent.addAndMakeVisible (*l); labels.push_back (std::move (l)); };
        label ("Title", lookPane); label ("Artist", lookPane); label ("Colour", lookPane); label ("Background", lookPane); label ("Text", lookPane); label ("Intensity", lookPane);
        label ("Size", *this); label ("Frame rate", *this); label ("Codec", *this);
        for (auto* e : { &titleEd, &artistEd })
        {
            e->setFont (font (15));
            e->onTextChange = [this] { look.title = titleEd.getText(); look.artist = artistEd.getText(); savePrefs(); redrawIfPaused(); };
            lookPane.addAndMakeVisible (*e);
        }
        artistEd.setText (look.artist, false);
        auto combo = [this] (juce::ComboBox& c, const juce::StringArray& items, int sel, std::function<void (int)> on, juce::Component& parent)
        {
            c.addItemList (items, 1); styleCombo (c); c.setSelectedItemIndex (sel, juce::dontSendNotification);
            c.onChange = [this, &c, on] { on (c.getSelectedItemIndex()); savePrefs(); redrawIfPaused(); };
            parent.addAndMakeVisible (c);
        };
        combo (tintBox, tintNames(), look.tint, [this] (int i) { look.tint = i; }, lookPane);
        combo (bgBox, backgroundNames(), look.background, [this] (int i) { look.background = i; }, lookPane);
        combo (textBox, textNames(), look.text, [this] (int i) { look.text = i; }, lookPane);
        juce::StringArray sizes; for (auto& s : videoSizes()) sizes.add (juce::String (s.label).upToFirstOccurrenceOf (" (", false, false));
        combo (sizeBox, sizes, sizeIndex, [this] (int i) { sizeIndex = i; }, *this);
        sizeBox.setTooltip ("1080p = 1920 x 1080, 1440p = 2560 x 1440, 4K = 3840 x 2160. 1440p gets YouTube's better encoder.");
        combo (fpsBox, { "30 fps", "60 fps" }, fps60 ? 1 : 0, [this] (int i) { fps60 = i == 1; }, *this);
        if (! VideoEncoder::hevcAvailable()) hevc = false;
        combo (codecBox, VideoEncoder::hevcAvailable() ? juce::StringArray { "H.264", "HEVC" } : juce::StringArray { "H.264" }, hevc ? 1 : 0, [this] (int i) { hevc = i == 1; }, *this);
        codecBox.setTooltip (VideoEncoder::hevcAvailable() ? "H.264 plays anywhere. HEVC makes files about 40% smaller; YouTube takes both."
                                                            : "This PC has no HEVC encoder (Microsoft's HEVC Video Extensions add one), so videos are H.264.");
        codecBox.setEnabled (VideoEncoder::hevcAvailable());
        responsiveBtn.setButtonText ("Keep the PC responsive");
        responsiveBtn.setTooltip ("Renders on fewer cores with short breaks, so Ableton and the rest stay smooth. Slower export.");
        responsiveBtn.setToggleState (responsive, juce::dontSendNotification);
        responsiveBtn.onClick = [this] { responsive = responsiveBtn.getToggleState(); if (job) job->keepCool = responsive; savePrefs(); };
        addAndMakeVisible (responsiveBtn);
        combo (reactBox, reactionNames(), look.reactions, [this] (int i) { look.reactions = i; fxReactBox.setSelectedItemIndex (i, juce::dontSendNotification); renderer.reset(); }, lookPane);
        reactLbl = makeLabel ("Reactions", 12, col::text3, 1); lookPane.addAndMakeVisible (*reactLbl);
        intensity.setSliderStyle (juce::Slider::LinearHorizontal);
        intensity.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        intensity.setRange (0.25, 2.0, 0.01);
        intensity.setValue (look.intensity, juce::dontSendNotification);
        intensity.onValueChange = [this] { look.intensity = (float) intensity.getValue(); savePrefs(); redrawIfPaused(); };
        lookPane.addAndMakeVisible (intensity);
        auto toggle = [this] (juce::ToggleButton& t, const juce::String& s, bool on, std::function<void (bool)> fn)
        {
            t.setButtonText (s); t.setToggleState (on, juce::dontSendNotification);
            t.onClick = [this, &t, fn] { fn (t.getToggleState()); savePrefs(); redrawIfPaused(); };
            lookPane.addAndMakeVisible (t);
        };
        toggle (fadesBtn, "Fade in and out", look.fades, [this] (bool b) { look.fades = b; });
        toggle (vignetteBtn, "Vignette and grain", look.vignette, [this] (bool b) { look.vignette = b; });
        toggle (progressBtn, "Progress line", look.progress, [this] (bool b) { look.progress = b; });
        toggle (badgeBtn, "Cover next to title", look.coverBadge, [this] (bool b) { look.coverBadge = b; });
        coverBtn = makeButton ("Cover art...", [this] { chooseCover(); });
        clearCoverBtn = makeButton ("Clear", [this] { look.cover = {}; prefs().setValue ("cover", juce::String()); redrawIfPaused(); });
        exportBtn = makeButton ("Export MP4", [this] { startExport(); });
        cancelBtn = makeButton ("Cancel", [this] { if (job) job->signalThreadShouldExit(); });
        folderBtn = makeButton ("Show folder", [this] { outputFolder().createDirectory(); outputFolder().startAsProcess(); });
        blurb = makeLabel ({}, 13, col::text2);
        blurb->setJustificationType (juce::Justification::topLeft);
        status = makeLabel ("Videos go to Music\\BC Visualizer.", 12.5f, col::text3);
        status->setJustificationType (juce::Justification::topLeft);
        for (juce::Component* c : std::initializer_list<juce::Component*> { coverBtn.get(), clearCoverBtn.get(), blurb.get(), &colourLock })
            lookPane.addAndMakeVisible (c);
        for (juce::Component* c : std::initializer_list<juce::Component*> { exportBtn.get(), cancelBtn.get(), folderBtn.get(), status.get(), &progress })
            addAndMakeVisible (c);
        cancelBtn->setEnabled (false);
        colourLock.setToggleState (prefs().getBoolValue ("lockColour", false), juce::dontSendNotification);
        colourLock.onClick = [this] { prefs().setValue ("lockColour", colourLock.getToggleState()); };

        // Effects tab: every reaction effect on its own, and the mirror
        fxIntro = makeLabel ("Each effect follows the music while Reactions is on. Switch off the ones you don't want; "
                             "a fixed mirror stays on even with Reactions off.", 12, col::text3);
        fxIntro->setJustificationType (juce::Justification::topLeft);
        fxPane.addAndMakeVisible (*fxIntro);
        fxReactLbl = makeLabel ("Reactions", 12, col::text3, 1); fxPane.addAndMakeVisible (*fxReactLbl);
        combo (fxReactBox, reactionNames(), look.reactions, [this] (int i) { look.reactions = i; reactBox.setSelectedItemIndex (i, juce::dontSendNotification); renderer.reset(); }, fxPane);
        combo (mirrorBox, mirrorNames(), look.mirror, [this] (int i) { look.mirror = i; renderer.reset(); }, fxPane);
        mirrorBox.setTooltip ("Follows the song: flips to a new mirror each section and flickers a kaleidoscope on mid hits. Or pick one to keep.");
        auto names = effectNames(), hints = effectHints();
        for (int i = 0; i < kEffectCount; ++i)
        {
            auto* t = fxToggles.add (new juce::ToggleButton (names[i]));
            t->setTooltip (hints[i]);
            t->setToggleState (look.effectOn ((Effect) i), juce::dontSendNotification);
            t->onClick = [this, t, i] { look.setEffect ((Effect) i, t->getToggleState()); savePrefs(); redrawIfPaused(); };
            fxPane.addAndMakeVisible (t);
        }
        fxAllOn = makeButton ("All on", [this] { setAllEffects (true); });
        fxAllOff = makeButton ("All off", [this] { setAllEffects (false); });
        fxPane.addAndMakeVisible (*fxAllOn); fxPane.addAndMakeVisible (*fxAllOff);

        // Random (style + colour, unless locked), under the style list
        randomBtn = makeButton ("Random", [this] { randomLook(); });
        randomBtn->setTooltip ("A new style and colour. Lock either to keep it.");
        addAndMakeVisible (*randomBtn);
        addAndMakeVisible (styleLock);
        styleLock.setToggleState (prefs().getBoolValue ("lockStyle", false), juce::dontSendNotification);
        styleLock.onClick = [this] { prefs().setValue ("lockStyle", styleLock.getToggleState()); };
        styleLockLbl = makeLabel ("Keep style", 12, col::text3, 1);
        addAndMakeVisible (*styleLockLbl);
        buildPatternsPane();
        addChildComponent (picker);
        picker.attach (all, patterns.picks);
        picker.onDone = [this] { showPicker (false); };
        picker.onChange = [this] { patternsEdited(); };
        showTab();

        // audio out
        juce::String err = devices.initialiseWithDefaultDevices (0, 2);
        devices.addAudioCallback (&player);
        player.setSource (&transport);

        styles.selectRow (juce::jlimit (0, (int) all.size() - 1, look.style));
        syncPatternControls();
        if (track.loaded() == false) { setDemoTrack (track, 32); attachTrack(); trackLabel->setText ("Demo loop. Open a track or drop one here.", juce::dontSendNotification); titleEd.setText ("Demo loop", true); }
        startTimerHz (30);   // the videos are 30 or 60 fps; 30 keeps the preview steady
        setSize (1480, 900);
    }

    ~MainComponent() override
    {
        stopTimer();
        ++scanGeneration;
        loader.removeAllJobs (true, 10000);
        if (job) job->stopThread (15000);
        transport.setSource (nullptr);
        player.setSource (nullptr);
        devices.removeAudioCallback (&player);
        styles.setModel (nullptr);
    }

    // ---------------------------------------------------------------- layout
    void paint (juce::Graphics& g) override { g.fillAll (col::bg); }
    void resized() override
    {
        auto r = getLocalBounds().reduced (10);
        stylesSlab.setBounds (r.removeFromLeft (270));
        settingsSlab.setBounds (r.removeFromRight (330));
        previewSlab.setBounds (r);
        {
            auto sc = stylesSlab.content() + stylesSlab.getPosition();
            auto rnd = sc.removeFromBottom (40);
            auto lk = rnd.removeFromRight (96);
            styleLock.setBounds (lk.removeFromLeft (22).withSizeKeepingCentre (22, 22));
            styleLockLbl->setBounds (lk.withTrimmedLeft (2));
            randomBtn->setBounds (rnd.withTrimmedRight (6));
            sc.removeFromBottom (8);
            styles.setBounds (sc);
        }

        picker.setBounds (stylesSlab.getBounds());
        auto p = previewSlab.content() + previewSlab.getPosition();
        auto top = p.removeFromTop (34);
        openBtn->setBounds (top.removeFromLeft (130));
        top.removeFromLeft (12);
        trackLabel->setBounds (top);
        auto bottom = p.removeFromBottom (36);
        const bool ownRow = bottom.getWidth() < 90 + 8 + 90 + 12 + 110 + 12 + 120 + 160;   // narrow window: slider above the buttons
        if (ownRow) { position.setBounds (p.removeFromBottom (28)); p.removeFromBottom (4); }
        playBtn->setBounds (bottom.removeFromLeft (90)); bottom.removeFromLeft (8);
        stopBtn->setBounds (bottom.removeFromLeft (90)); bottom.removeFromLeft (12);
        fullBtn->setBounds (bottom.removeFromLeft (110)); bottom.removeFromLeft (12);
        timeLabel->setBounds (bottom.removeFromRight (juce::jmin (120, bottom.getWidth())));
        if (! ownRow) position.setBounds (bottom.reduced (0, 8));
        p.removeFromBottom (6);
        timeline.setBounds (p.removeFromBottom (22));
        p.removeFromBottom (6); p.removeFromTop (8);
        preview.setBounds (p);
        if (fullscreen) full.setBounds (getLocalBounds());

        auto s = settingsSlab.content() + settingsSlab.getPosition();
        auto tabs = s.removeFromTop (32);
        {
            const int tw = (tabs.getWidth() - 12) / 3;
            lookTab.setBounds (tabs.removeFromLeft (tw)); tabs.removeFromLeft (6);
            fxTab.setBounds (tabs.removeFromLeft (tw)); tabs.removeFromLeft (6);
            patTab.setBounds (tabs);
        }
        s.removeFromTop (8);
        folderBtn->setBounds (s.removeFromBottom (30)); s.removeFromBottom (4);
        status->setBounds (s.removeFromBottom (34)); s.removeFromBottom (4);
        progress.setBounds (s.removeFromBottom (22)); s.removeFromBottom (6);
        auto ex = s.removeFromBottom (40); exportBtn->setBounds (ex.removeFromLeft (ex.getWidth() * 2 / 3 - 4)); ex.removeFromLeft (8); cancelBtn->setBounds (ex);
        s.removeFromBottom (6);
        responsiveBtn.setBounds (s.removeFromBottom (26));
        s.removeFromBottom (4);
        {
            auto threeC = s.removeFromBottom (30);
            auto three = s.removeFromBottom (18);
            const int cw = (threeC.getWidth() - 16) * 27 / 100, sw = threeC.getWidth() - 16 - 2 * cw;
            sizeBox.setBounds (threeC.removeFromLeft (sw)); threeC.removeFromLeft (8);
            fpsBox.setBounds (threeC.removeFromLeft (cw)); threeC.removeFromLeft (8);
            codecBox.setBounds (threeC);
            labels[6]->setBounds (three.removeFromLeft (sw)); three.removeFromLeft (8);
            labels[7]->setBounds (three.removeFromLeft (cw)); three.removeFromLeft (8);
            labels[8]->setBounds (three);
        }
        s.removeFromBottom (8);
        paneView.setBounds (s);
        layoutPanes();
        frameDirty = true;
    }

    void layoutPanes()
    {
        const int w = juce::jmax (100, paneView.getWidth() - 10);   // room for the scrollbar when it shows
        {
            auto s = juce::Rectangle<int> (0, 0, w, 2000);
            size_t li = 0;
            auto row = [&] (juce::Component& c, int h = 30) { labels[li++]->setBounds (s.removeFromTop (18)); c.setBounds (s.removeFromTop (h)); s.removeFromTop (8); };
            blurb->setBounds (s.removeFromTop (54)); s.removeFromTop (6);
            row (titleEd); row (artistEd);
            {
                auto lab = s.removeFromTop (18);
                colourLock.setBounds (lab.removeFromRight (22));
                labels[li++]->setBounds (lab);
                tintBox.setBounds (s.removeFromTop (30)); s.removeFromTop (8);
            }
            row (bgBox); row (textBox);
            {
                auto lab = s.removeFromTop (18); labels[li++]->setBounds (lab.removeFromLeft (lab.getWidth() * 55 / 100)); reactLbl->setBounds (lab.withTrimmedLeft (8));
                auto rr = s.removeFromTop (28); intensity.setBounds (rr.removeFromLeft (rr.getWidth() * 55 / 100)); reactBox.setBounds (rr.withTrimmedLeft (8));
                s.removeFromTop (8);
            }
            auto cov = s.removeFromTop (30); coverBtn->setBounds (cov.removeFromLeft (cov.getWidth() * 2 / 3 - 4)); cov.removeFromLeft (8); clearCoverBtn->setBounds (cov);
            s.removeFromTop (6);
            fadesBtn.setBounds (s.removeFromTop (26)); vignetteBtn.setBounds (s.removeFromTop (26)); progressBtn.setBounds (s.removeFromTop (26)); badgeBtn.setBounds (s.removeFromTop (26));
            lookPane.setSize (w, s.getY() + 4);
        }
        {
            auto s = juce::Rectangle<int> (0, 0, w, 2000);
            fxIntro->setBounds (s.removeFromTop (48)); s.removeFromTop (4);
            fxReactLbl->setBounds (s.removeFromTop (18)); fxReactBox.setBounds (s.removeFromTop (30)); s.removeFromTop (8);
            mirrorBox.setBounds (s.removeFromTop (30)); s.removeFromTop (8);
            const int colW = (w - 8) / 2;
            for (int i = 0; i < fxToggles.size(); ++i)
                fxToggles[i]->setBounds ((i % 2) * (colW + 8), s.getY() + (i / 2) * 26, colW, 24);
            s.removeFromTop (((fxToggles.size() + 1) / 2) * 26 + 8);
            auto b = s.removeFromTop (32);
            fxAllOn->setBounds (b.removeFromLeft (b.getWidth() / 2 - 4)); b.removeFromLeft (8); fxAllOff->setBounds (b);
            fxPane.setSize (w, s.getY() + 4);
        }
        {
            auto s = juce::Rectangle<int> (0, 0, w, 2000);
            auto rowOf = [&] (juce::OwnedArray<juce::TextButton>& btns, int h)
            {
                auto r = s.removeFromTop (h);
                const int bw = (r.getWidth() - (btns.size() - 1) * 6) / juce::jmax (1, btns.size());
                for (int i = 0; i < btns.size(); ++i) { btns[i]->setBounds (i == btns.size() - 1 ? r : r.removeFromLeft (bw)); if (i < btns.size() - 1) r.removeFromLeft (6); }
            };
            modeLbl->setBounds (s.removeFromTop (18)); rowOf (modeBtns, 30); s.removeFromTop (8);
            patternsInfo->setBounds (s.removeFromTop (54)); s.removeFromTop (4);
            auto pr = s.removeFromTop (30); pickBtn->setBounds (pr.removeFromLeft (pr.getWidth() * 3 / 5 - 4)); pr.removeFromLeft (8); rerollBtn->setBounds (pr);
            s.removeFromTop (4);
            shuffleBtn.setBounds (s.removeFromTop (26)); s.removeFromTop (8);
            whenLbl->setBounds (s.removeFromTop (18)); rowOf (whenBtns, 30); s.removeFromTop (8);
            if (patterns.when == Patterns::timer)
            {
                everyLbl->setBounds (s.removeFromTop (18)); everyBox.setBounds (s.removeFromTop (30)); s.removeFromTop (8);
            }
            else
            {
                minLbl->setBounds (s.removeFromTop (18)); minSlider.setBounds (s.removeFromTop (26)); s.removeFromTop (6);
                maxLbl->setBounds (s.removeFromTop (18)); maxSlider.setBounds (s.removeFromTop (26)); s.removeFromTop (6);
            }
            fadeLbl->setBounds (s.removeFromTop (18)); fadeSlider.setBounds (s.removeFromTop (26)); s.removeFromTop (6);
            cutBtn.setBounds (s.removeFromTop (26));
            patPane.setSize (w, s.getY() + 4);
        }
    }

    int currentTab() const { return fxTab.getToggleState() ? 1 : patTab.getToggleState() ? 2 : 0; }

    void showTab()
    {
        const int t = currentTab();
        paneView.setViewedComponent (t == 1 ? &fxPane : t == 2 ? &patPane : &lookPane, false);
        layoutPanes();
    }

    // ---------------------------------------------------------------- patterns
    void buildPatternsPane()
    {
        auto paneLabel = [this] (const juce::String& s) { auto l = makeLabel (s, 12, col::text3, 1); patPane.addAndMakeVisible (*l); return l; };
        modeLbl = paneLabel ("Styles in the video");
        whenLbl = paneLabel ("When it switches");
        const auto modes = Patterns::modeNames(), whens = juce::StringArray { "Follow the song", "Every few secs" };
        for (int i = 0; i < modes.size(); ++i)
        {
            auto* b = modeBtns.add (new juce::TextButton (modes[i]));
            b->setRadioGroupId (61); b->setClickingTogglesState (true);
            b->onClick = [this, i] { if (modeBtns[i]->getToggleState() && patterns.mode != i) { patterns.mode = i; patternsEdited(); } };
            patPane.addAndMakeVisible (b);
        }
        modeBtns[0]->setTooltip ("One style all the way through (the one picked on the left).");
        modeBtns[1]->setTooltip ("Switches between the styles you pick, in order or shuffled.");
        modeBtns[2]->setTooltip ("Switches between random styles. Re-roll for a different set.");
        for (int i = 0; i < whens.size(); ++i)
        {
            auto* b = whenBtns.add (new juce::TextButton (whens[i]));
            b->setRadioGroupId (62); b->setClickingTogglesState (true);
            b->onClick = [this, i] { if (whenBtns[i]->getToggleState() && patterns.when != i) { patterns.when = i; patternsEdited(); } };
            patPane.addAndMakeVisible (b);
        }
        whenBtns[0]->setTooltip ("Switches when the music changes section; hard cuts when it slams back in.");
        whenBtns[1]->setTooltip ("Switches on a steady timer.");
        patternsInfo = makeLabel ({}, 12.5f, col::text2);
        patternsInfo->setJustificationType (juce::Justification::topLeft);
        patPane.addAndMakeVisible (*patternsInfo);
        pickBtn = makeButton ("Pick styles", [this] { showPicker (true); });
        rerollBtn = makeButton ("Re-roll", [this] { patterns.seed = (juce::uint32) juce::Random::getSystemRandom().nextInt() | 1u; patternsEdited(); });
        rerollBtn->setTooltip ("A new random order (same settings).");
        patPane.addAndMakeVisible (*pickBtn); patPane.addAndMakeVisible (*rerollBtn);
        auto slider = [this] (juce::Slider& sl, double lo, double hi, double step, double v)
        {
            sl.setSliderStyle (juce::Slider::LinearHorizontal);
            sl.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            sl.setRange (lo, hi, step);
            sl.setValue (v, juce::dontSendNotification);
            sl.onValueChange = [this] { readPatternSliders(); };
            sl.onDragEnd = [this] { patternsEdited(); };
            patPane.addAndMakeVisible (sl);
        };
        minLbl = paneLabel ({}); maxLbl = paneLabel ({}); fadeLbl = paneLabel ({});
        everyLbl = paneLabel ("Switch every");
        slider (minSlider, 2, 60, 1, patterns.minSeconds);
        slider (maxSlider, 4, 120, 1, patterns.maxSeconds);
        slider (fadeSlider, 0.5, 8, 0.5, patterns.fadeSeconds);
        for (auto v : Patterns::everyChoices()) everyBox.addItem (juce::String ((int) v) + " seconds", (int) v);
        styleCombo (everyBox);
        everyBox.onChange = [this] { patterns.everySeconds = everyBox.getSelectedId(); patternsEdited(); };
        patPane.addAndMakeVisible (everyBox);
        cutBtn.setButtonText ("Hard cut when it slams back in");
        cutBtn.onClick = [this] { patterns.cutOnSlams = cutBtn.getToggleState(); patternsEdited(); };
        shuffleBtn.setButtonText ("Shuffle the picked styles");
        shuffleBtn.onClick = [this] { patterns.shuffle = shuffleBtn.getToggleState(); patternsEdited(); };
        patPane.addAndMakeVisible (cutBtn); patPane.addAndMakeVisible (shuffleBtn);
    }

    void readPatternSliders()
    {
        patterns.minSeconds = minSlider.getValue();
        patterns.maxSeconds = juce::jmax (maxSlider.getValue(), patterns.minSeconds + 1);
        patterns.fadeSeconds = fadeSlider.getValue();
        minLbl->setText ("Shortest: " + juce::String ((int) patterns.minSeconds) + " s on a style", juce::dontSendNotification);
        maxLbl->setText ("Longest: " + juce::String ((int) patterns.maxSeconds) + " s on a style", juce::dontSendNotification);
        fadeLbl->setText ("Fade between styles: " + juce::String (patterns.fadeSeconds, 1) + " s", juce::dontSendNotification);
    }

    void patternsEdited() { savePrefs(); syncPatternControls(); patternsChanged(); }

    void syncPatternControls()
    {
        for (int i = 0; i < modeBtns.size(); ++i) modeBtns[i]->setToggleState (patterns.mode == i, juce::dontSendNotification);
        for (int i = 0; i < whenBtns.size(); ++i) whenBtns[i]->setToggleState (patterns.when == i, juce::dontSendNotification);
        const bool sw = patterns.mode != Patterns::single;
        for (auto* b : whenBtns) b->setEnabled (sw);
        for (juce::Component* c : std::initializer_list<juce::Component*> { &minSlider, &maxSlider, &fadeSlider, &everyBox, &cutBtn }) c->setEnabled (sw);
        const bool timerMode = patterns.when == Patterns::timer;
        minSlider.setVisible (! timerMode); maxSlider.setVisible (! timerMode); minLbl->setVisible (! timerMode); maxLbl->setVisible (! timerMode);
        everyBox.setVisible (timerMode); everyLbl->setVisible (timerMode);
        everyBox.setSelectedId ((int) patterns.everySeconds, juce::dontSendNotification);
        if (everyBox.getSelectedId() == 0) everyBox.setSelectedId (16, juce::dontSendNotification);
        minSlider.setValue (patterns.minSeconds, juce::dontSendNotification);
        maxSlider.setValue (patterns.maxSeconds, juce::dontSendNotification);
        fadeSlider.setValue (patterns.fadeSeconds, juce::dontSendNotification);
        cutBtn.setToggleState (patterns.cutOnSlams, juce::dontSendNotification);
        cutBtn.setEnabled (sw && ! timerMode);
        shuffleBtn.setToggleState (patterns.shuffle, juce::dontSendNotification);
        shuffleBtn.setEnabled (patterns.mode == Patterns::picked);
        pickBtn->setButtonText ("Pick styles (" + juce::String ((int) patterns.picks.size()) + ")");
        rerollBtn->setEnabled (patterns.mode == Patterns::random || (patterns.mode == Patterns::picked && patterns.shuffle));
        readPatternSliders();
        layoutPanes();
    }

    void patternsChanged()
    {
        plan = makePlan (patterns, events, look.style, (int) all.size());
        stylePlayer.reset();
        renderer.reset();
        timeline.plan = &plan;
        timeline.duration = track.seconds();
        const int switches = (int) plan.steps.size() - 1;
        juce::String info;
        if (patterns.mode == Patterns::single) info = "Single: the style picked on the left plays all the way through. Pick Picked or Random to switch styles during the video.";
        else if (scanning) info = "Listening to the track for section changes...";
        else if (patterns.mode == Patterns::picked && patterns.picks.size() < 2)
            info = "Pick at least two styles to switch between (" + juce::String ((int) patterns.picks.size()) + " picked).";
        else if (switches == 0 && patterns.when == Patterns::followSong)
            info = juce::String ("No section changes heard in this track, so it would stay on one style. Use Every few secs to switch on a timer, or lower Shortest.");
        else
            info = (patterns.mode == Patterns::random ? juce::String ("Random from all ") + juce::String ((int) all.size())
                                                      : juce::String ((int) patterns.picks.size()) + " picked" + (patterns.shuffle ? ", shuffled" : ", in order"))
                 + ": " + juce::String (switches) + (switches == 1 ? " switch" : " switches") + " over " + fmtTime (timeline.duration) + ". The strip under the preview shows when.";
        patternsInfo->setText (info, juce::dontSendNotification);
        pickBtn->setButtonText ("Pick styles (" + juce::String ((int) patterns.picks.size()) + ")");
        timeline.note = patterns.mode == Patterns::single ? "One style all the way through (Patterns tab to switch styles)"
                      : scanning ? "Listening for section changes..." : juce::String();
        timeline.repaint();
        frameDirty = true;
    }

    void showPicker (bool on)
    {
        picker.refresh();
        picker.setVisible (on);
        if (on) picker.toFront (false);
        for (juce::Component* c : std::initializer_list<juce::Component*> { &styles, randomBtn.get(), &styleLock, styleLockLbl.get() }) c->setVisible (! on);
    }

    void startScan()
    {
        scanning = true;
        events = {};
        const int gen = ++scanGeneration;
        auto audio = std::make_shared<juce::AudioBuffer<float>> (track.audio);
        juce::Component::SafePointer<MainComponent> safe (this);
        std::atomic<int>* genPtr = &scanGeneration;
        loader.addJob ([audio, gen, safe, genPtr]
        {
            auto ev = std::make_shared<SongEvents> (scanSong (*audio, Track::rate, [genPtr, gen] { return genPtr->load() != gen; }));
            juce::MessageManager::callAsync ([safe, gen, ev]
            {
                if (safe == nullptr || safe->scanGeneration != gen) return;
                safe->events = *ev; safe->scanning = false; safe->patternsChanged();
            });
        });
        patternsChanged();
    }

    // ---------------------------------------------------------------- full screen
    void setFullscreen (bool on)
    {
        if (fullscreen == on) return;
        fullscreen = on;
        auto* window = dynamic_cast<juce::DocumentWindow*> (getTopLevelComponent());
        if (window != nullptr && window != (juce::Component*) this)
            juce::Desktop::getInstance().setKioskModeComponent (on ? window : nullptr, false);
        full.setVisible (on);
        if (on) { full.setBounds (getLocalBounds()); full.toFront (false); }
        grabKeyboardFocus();
        frameDirty = true;
    }

    void setAllEffects (bool on)
    {
        for (int i = 0; i < kEffectCount; ++i) { look.setEffect ((Effect) i, on); fxToggles[i]->setToggleState (on, juce::dontSendNotification); }
        savePrefs(); redrawIfPaused();
    }

    /** a new style and colour (whichever isn't locked) */
    void randomLook()
    {
        auto& rnd = juce::Random::getSystemRandom();
        if (! styleLock.getToggleState() && all.size() > 1)
        {
            int n = look.style;
            while (n == look.style) n = rnd.nextInt ((int) all.size());
            styles.selectRow (n);
            styles.scrollToEnsureRowIsOnscreen (n);
        }
        if (! colourLock.getToggleState() && tintBox.getNumItems() > 1)
        {
            int c = look.tint;
            while (c == look.tint) c = rnd.nextInt (tintBox.getNumItems());
            tintBox.setSelectedItemIndex (c, juce::sendNotificationSync);
        }
        redrawIfPaused();
    }

    // ---------------------------------------------------------------- files
    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray& files, int, int) override
    {
        for (auto& f : files)
        {
            juce::File file (f);
            if (file.hasFileExtension ("png;jpg;jpeg;gif")) setCover (file);
            else openTrack (file);
        }
    }

    void openTrack (const juce::File& f)
    {
        juce::String err;
        stop();
        transport.setSource (nullptr);
        Track t;
        if (! loadTrack (f, t, err)) { status->setText (err, juce::dontSendNotification); return; }
        track = std::move (t);
        attachTrack();
        trackLabel->setText (f.getFileName() + "   " + fmtTime (track.seconds()), juce::dontSendNotification);
        titleEd.setText (f.getFileNameWithoutExtension(), true);
        prefs().setValue ("lastFolder", f.getParentDirectory().getFullPathName());
    }

    void setCover (const juce::File& f)
    {
        auto img = juce::ImageFileFormat::loadFrom (f);
        if (! img.isValid()) { status->setText ("Couldn't read that image.", juce::dontSendNotification); return; }
        look.cover = img;
        if (look.background == 0) { look.background = 1; bgBox.setSelectedItemIndex (1, juce::dontSendNotification); }
        prefs().setValue ("cover", f.getFullPathName());
        savePrefs(); redrawIfPaused();
    }

    // ---------------------------------------------------------------- for tests
    void renderTo (juce::Image& img, const Frame& f, Viz& v) { juce::Graphics g (img); renderer.render (g, img.getBounds().toFloat(), f, v, look); }
    Look& getLook() { return look; }
    void setTab (int t) { (t == 1 ? fxTab : t == 2 ? patTab : lookTab).setToggleState (true, juce::dontSendNotification); showTab(); }
    void testPatterns (int mode) { waitForScan(); patterns.mode = mode; patterns.picks = { 0, 12, 40, 53 }; syncPatternControls(); patternsChanged(); }
    void testPicker (bool on) { showPicker (on); }
    void testFullscreen (bool on) { fullscreen = on; full.setVisible (on); if (on) { full.setBounds (getLocalBounds()); full.toFront (false); } frameDirty = true; tick(); }
    void waitForScan()
    {
        if (! scanning) return;
        ++scanGeneration; loader.removeAllJobs (true, 10000);
        events = scanSong (track.audio, Track::rate); scanning = false; patternsChanged();
    }
    Track& getTrack() { return track; }
    void showTime (double t) { for (double x = 0; x <= t; x += 1.0 / 30) { clockT = x; tick(); } }

private:
    std::vector<std::unique_ptr<Viz>> all;
    Look look;
    Track track;
    Renderer renderer;
    Analyzer analyzer;
    Frame frame;
    int sizeIndex = 0; bool fps60 = false;

    Slab stylesSlab { "Styles" }, previewSlab { "Preview" }, settingsSlab { "Video" };
    StyleList stylesModel; juce::ListBox styles;
    Preview preview;
    std::unique_ptr<juce::TextButton> openBtn, playBtn, stopBtn, fullBtn, coverBtn, clearCoverBtn, exportBtn, cancelBtn, folderBtn, randomBtn, fxAllOn, fxAllOff;
    juce::TextButton lookTab, fxTab, patTab;
    juce::Viewport paneView;
    juce::Component lookPane, fxPane, patPane;
    // patterns
    Patterns patterns;
    SongEvents events;
    Plan plan;
    StylePlayer stylePlayer;
    Timeline timeline;
    PickerPanel picker;
    FullView full;
    bool fullscreen = false, scanning = false, hevc = false, responsive = false;
    std::atomic<int> scanGeneration { 0 };
    juce::ThreadPool loader { 1 };
    juce::OwnedArray<juce::TextButton> modeBtns, whenBtns;
    std::unique_ptr<juce::Label> modeLbl, whenLbl, patternsInfo, minLbl, maxLbl, fadeLbl, everyLbl;
    std::unique_ptr<juce::TextButton> pickBtn, rerollBtn;
    juce::Slider minSlider, maxSlider, fadeSlider;
    juce::ComboBox everyBox, codecBox;
    juce::ToggleButton cutBtn, shuffleBtn, responsiveBtn;
    int lastThumb = -1;
    LockToggle styleLock { "style" }, colourLock { "colour" };
    std::unique_ptr<juce::Label> styleLockLbl, fxIntro, fxReactLbl;
    juce::ComboBox fxReactBox, mirrorBox;
    juce::OwnedArray<juce::ToggleButton> fxToggles;
    std::unique_ptr<juce::Label> trackLabel, timeLabel, blurb, status;
    std::vector<std::unique_ptr<juce::Label>> labels;
    juce::Slider position, intensity;
    juce::TextEditor titleEd, artistEd;
    juce::ComboBox tintBox, bgBox, textBox, sizeBox, fpsBox, reactBox;
    std::unique_ptr<juce::Label> reactLbl;
    juce::ToggleButton fadesBtn, vignetteBtn, progressBtn, badgeBtn;
    ProgressPill progress;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::AudioDeviceManager devices;
    juce::AudioSourcePlayer player;
    juce::AudioTransportSource transport;
    std::unique_ptr<juce::MemoryAudioSource> source;
    bool playing = false, scrubbing = false, frameDirty = true;
    double clockT = 0, lastTick = 0;
    std::unique_ptr<ExportJob> job;

    bool haveDevice() { return devices.getCurrentAudioDevice() != nullptr; }

    void attachTrack()
    {
        transport.setSource (nullptr);
        source = std::make_unique<juce::MemoryAudioSource> (track.audio, false, false);
        transport.setSource (source.get(), 0, nullptr, Track::rate, 2);
        clockT = 0; analyzer.reset(); all[(size_t) look.style]->reset();
        stylePlayer.reset(); renderer.reset();
        frameDirty = true;
        startScan();
    }

    void togglePlay()
    {
        if (playing) { pause(); return; }
        if (clockT >= track.seconds() - 0.05) seek (0);
        playing = true;
        if (haveDevice()) { transport.setPosition (clockT); transport.start(); }
        lastTick = juce::Time::getMillisecondCounterHiRes();
        playBtn->setButtonText ("Pause");
    }
    void pause() { playing = false; transport.stop(); playBtn->setButtonText ("Play"); }
    void stop() { pause(); seek (0); }
    void seek (double t)
    {
        clockT = juce::jlimit (0.0, juce::jmax (0.0, track.seconds()), t);
        transport.setPosition (clockT);
        analyzer.reset(); all[(size_t) look.style]->reset(); renderer.reset(); stylePlayer.reset();
        frameDirty = true;
    }
    void redrawIfPaused() { frameDirty = true; }

    void chooseTrack()
    {
        chooser = std::make_unique<juce::FileChooser> ("Open a track", juce::File (prefs().getValue ("lastFolder")), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) openTrack (fc.getResult()); });
    }
    void chooseCover()
    {
        chooser = std::make_unique<juce::FileChooser> ("Cover art", juce::File (prefs().getValue ("lastFolder")), "*.png;*.jpg;*.jpeg");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) setCover (fc.getResult()); });
    }

    // ---------------------------------------------------------------- frames
    void timerCallback() override
    {
        if (job && job->finished) finishExport();
        if (job)
        {
            auto& s = videoSizes()[(size_t) sizeIndex];
            const int pc = (int) (job->progress * 100);
            progress.setValue (job->progress, juce::String (pc) + "%   " + juce::String (s.h) + "p");
            const double fpsNow = job->framesPerSecond, left = job->secondsLeft;
            if (fpsNow > 0)
            {
                const int vfps = fps60 ? 60 : 30;
                juce::String line = juce::String (fpsNow, 0) + " fps (" + juce::String (fpsNow / vfps, 1) + "x real time)";
                if (left >= 0) line << "   " << fmtTime (left) << " left";
                if (job->encoderInfo.isNotEmpty()) line << "\n" << job->encoderInfo;
                if (job->easing) line << "   (easing off)";
                status->setText (line, juce::dontSendNotification);
            }
            if (! playing)
            {
                const int v = job->thumbVersion;
                if (v != lastThumb)
                {
                    lastThumb = v;
                    auto th = job->getThumb();
                    if (th.isValid()) { preview.frame = th; preview.overlay = "Rendering " + juce::String (pc) + "%"; preview.repaint(); }
                }
                else if (preview.overlay.isNotEmpty()) { preview.overlay = "Rendering " + juce::String (pc) + "%"; preview.repaint(); }
            }
            else if (preview.overlay.isNotEmpty()) preview.overlay = {};
        }
        if (playing)
        {
            if (haveDevice()) clockT = transport.getCurrentPosition();
            else { auto now = juce::Time::getMillisecondCounterHiRes(); clockT += (now - lastTick) / 1000.0; lastTick = now; }
            if (clockT >= track.seconds()) { pause(); clockT = track.seconds(); }
            tick();
        }
        else if (frameDirty && ! (job && ! playing && lastThumb >= 0)) tick();
        timeline.setPlayhead (clockT);
    }

    void tick()
    {
        frameDirty = false;
        auto area = preview.videoArea();
        if (area.isEmpty()) return;
        if (fullscreen) area = full.videoArea();
        const float cap = fullscreen ? 1600.0f : 800.0f;
        const float shrink = juce::jmin (1.0f, cap / (float) juce::jmax (1, area.getWidth()));
        const auto size = juce::Rectangle<int> (juce::jmax (16, juce::roundToInt (area.getWidth() * shrink)), juce::jmax (9, juce::roundToInt (area.getHeight() * shrink)));
        juce::Image& target = fullscreen ? full.frame : preview.frame;
        if (! target.isValid() || target.getBounds() != size)
            target = juce::Image (juce::Image::ARGB, size.getWidth(), size.getHeight(), true, juce::SoftwareImageType());
        const double fps = 30;
        if (playing || analyzerT != clockT) analyzer.analyse (track.audio, Track::rate, clockT, fps, frame);
        analyzerT = clockT;
        frame.duration = track.seconds();
        {
            juce::Graphics g (target);
            if (patterns.mode != Patterns::single && ! plan.steps.empty())
            {
                auto now = stylePlayer.at (clockT, plan, all);
                renderer.render (g, target.getBounds().toFloat(), frame, *now.a, look, now.b, now.mix);
            }
            else renderer.render (g, target.getBounds().toFloat(), frame, *all[(size_t) look.style], look);
        }
        preview.overlay = {};
        if (fullscreen) full.repaint(); else preview.repaint (area);
        if (! scrubbing && track.seconds() > 0) position.setValue (clockT / track.seconds(), juce::dontSendNotification);
        timeLabel->setText (fmtTime (clockT) + " / " + fmtTime (track.seconds()), juce::dontSendNotification);
    }
    double analyzerT = -1;

    // ---------------------------------------------------------------- export
    void startExport()
    {
        if (job || ! track.loaded()) return;
        if (scanning && patterns.mode != Patterns::single)
        {
            status->setText ("Still listening to the track for section changes - try again in a second.", juce::dontSendNotification);
            return;
        }
        if (patterns.mode == Patterns::picked && patterns.picks.size() < 2)
        {
            status->setText ("Pick at least two styles on the Patterns tab (or set it to Single).", juce::dontSendNotification);
            return;
        }
        pause();
        auto name = juce::File::createLegalFileName (look.title.isNotEmpty() ? look.title : "BC Visualizer");
        auto out = outputFolder().getNonexistentChildFile (name, ".mp4", false);
        auto& s = videoSizes()[(size_t) sizeIndex];
        job = std::make_unique<ExportJob> (track, look, out, s.w, s.h, fps60 ? 60 : 30);
        if (patterns.mode != Patterns::single) job->plan = plan;
        job->threads = Workers::recommended();
        job->hevc = hevc;
        job->keepCool = responsive;
        job->heat = [] { return 2; };    // no heat sensor on a PC: "Keep the PC responsive" always eases off when it's on
        lastThumb = -1;
        job->startThread (responsive ? juce::Thread::Priority::low : juce::Thread::Priority::normal);
        exportBtn->setEnabled (false); cancelBtn->setEnabled (true); openBtn->setEnabled (false);
        status->setText ("Rendering " + out.getFileName() + "... you can keep previewing.", juce::dontSendNotification);
    }

    void finishExport()
    {
        job->stopThread (10000);
        if (job->ok) status->setText ("Saved " + job->output.getFileName() + " in " + fmtTime (job->secondsTaken)
                                      + (job->secondsTaken > 0 ? " (" + juce::String (track.seconds() / job->secondsTaken, 1) + "x real time)" : juce::String()) + ". Ready for YouTube.", juce::dontSendNotification);
        else if (job->cancelled) status->setText ("Export cancelled.", juce::dontSendNotification);
        else status->setText ("Export failed: " + job->error, juce::dontSendNotification);
        progress.setValue (job->ok ? 1.0 : 0.0, job->ok ? "Done" : "");
        job.reset();
        lastThumb = -1; preview.overlay = {};
        stylePlayer.reset(); renderer.reset(); analyzer.reset(); analyzerT = -1;
        frameDirty = true;
        exportBtn->setEnabled (true); cancelBtn->setEnabled (false); openBtn->setEnabled (true);
    }

    // ---------------------------------------------------------------- prefs
    void loadPrefs()
    {
        auto& p = prefs();
        look.style = p.getIntValue ("style", 0);
        look.artist = p.getValue ("artist", "binaryimage");
        look.tint = p.getIntValue ("tint", 0);
        look.background = p.getIntValue ("background", 0);
        look.text = p.getIntValue ("text", 0);
        look.fades = p.getBoolValue ("fades", true);
        look.vignette = p.getBoolValue ("vignette", true);
        look.progress = p.getBoolValue ("progress", false);
        look.coverBadge = p.getBoolValue ("coverBadge", true);
        look.reactions = juce::jlimit (0, 2, p.getIntValue ("reactions", 2));
        look.tint = juce::jlimit (0, tintNames().size() - 1, look.tint);
        look.intensity = (float) p.getDoubleValue ("intensity", 1.0);
        look.mirror = juce::jlimit (0, (int) kMirrorCount - 1, p.getIntValue ("mirror", mirrorAuto));
        look.effectsOff = p.getIntValue ("effectsOff", 0);
        sizeIndex = juce::jlimit (0, (int) videoSizes().size() - 1, p.getIntValue ("size", 0));
        fps60 = p.getBoolValue ("fps60", false);
        patterns.mode = juce::jlimit (0, 2, p.getIntValue ("pmode", 0));
        patterns.picks.clear();
        for (auto& x : juce::StringArray::fromTokens (p.getValue ("picks", "0,12,40,53"), ",", {}))
            if (x.trim().isNotEmpty()) { const int i = x.getIntValue(); if (i >= 0 && i < (int) all.size()) patterns.picks.push_back (i); }
        patterns.shuffle = p.getBoolValue ("shuffle", false);
        patterns.when = juce::jlimit (0, 1, p.getIntValue ("when", 0));
        patterns.minSeconds = juce::jlimit (2.0, 60.0, p.getDoubleValue ("minS", 8));
        patterns.maxSeconds = juce::jlimit (4.0, 120.0, p.getDoubleValue ("maxS", 30));
        patterns.everySeconds = p.getDoubleValue ("every", 16);
        patterns.fadeSeconds = juce::jlimit (0.5, 8.0, p.getDoubleValue ("fade", 2.5));
        patterns.cutOnSlams = p.getBoolValue ("cut", true);
        patterns.seed = (juce::uint32) p.getIntValue ("seed", 1) | 1u;
        hevc = p.getBoolValue ("hevc", false);
        responsive = p.getBoolValue ("responsive", false);
        juce::File cov (p.getValue ("cover"));
        if (cov.existsAsFile()) look.cover = juce::ImageFileFormat::loadFrom (cov);
    }
    void savePrefs()
    {
        auto& p = prefs();
        p.setValue ("style", look.style); p.setValue ("artist", look.artist); p.setValue ("tint", look.tint);
        p.setValue ("background", look.background); p.setValue ("text", look.text); p.setValue ("fades", look.fades);
        p.setValue ("vignette", look.vignette); p.setValue ("progress", look.progress); p.setValue ("coverBadge", look.coverBadge); p.setValue ("reactions", look.reactions); p.setValue ("intensity", (double) look.intensity);
        p.setValue ("mirror", look.mirror); p.setValue ("effectsOff", look.effectsOff);
        p.setValue ("size", sizeIndex); p.setValue ("fps60", fps60);
        juce::StringArray pk; for (int i : patterns.picks) pk.add (juce::String (i));
        p.setValue ("pmode", patterns.mode); p.setValue ("picks", pk.joinIntoString (","));
        p.setValue ("shuffle", patterns.shuffle); p.setValue ("when", patterns.when);
        p.setValue ("minS", patterns.minSeconds); p.setValue ("maxS", patterns.maxSeconds);
        p.setValue ("every", patterns.everySeconds); p.setValue ("fade", patterns.fadeSeconds);
        p.setValue ("cut", patterns.cutOnSlams); p.setValue ("seed", (int) patterns.seed);
        p.setValue ("hevc", hevc); p.setValue ("responsive", responsive);
    }

public:
    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::spaceKey) { togglePlay(); return true; }
        if (k == juce::KeyPress::F11Key) { setFullscreen (! fullscreen); return true; }
        if (k == juce::KeyPress::escapeKey && fullscreen) { setFullscreen (false); return true; }
        if (k == juce::KeyPress::escapeKey && picker.isVisible()) { showPicker (false); return true; }
        if (k == juce::KeyPress::upKey || k == juce::KeyPress::downKey)
        {
            int n = juce::jlimit (0, (int) all.size() - 1, look.style + (k == juce::KeyPress::upKey ? -1 : 1));
            styles.selectRow (n); return true;
        }
        return false;
    }
};

// ------------------------------------------------------------ command-line modes (CI and screenshots)

static void writePng (const juce::Image& img, const juce::File& f)
{
    f.deleteFile();
    juce::FileOutputStream os (f);
    juce::PNGImageFormat().writeImageToStream (img, os);
}

/** One PNG per style after 4 seconds of the demo loop, a contact sheet, and the app window. */
/** The app window, plus the button check (also on its own: --layout-check <folder>). */
static int runLayoutCheck (const juce::File& dir)
{
    dir.createDirectory();
    // the app itself, plus the button check: both tabs, at the normal size, the smallest it can be dragged to, and tall
    int layoutProblems = 0;
    {
        MainComponent mc;
        mc.setSize (1480, 900);
        mc.showTime (3.0);
        auto shot = mc.createComponentSnapshot (mc.getLocalBounds(), true, 1.0f);
        writePng (shot, dir.getChildFile ("app.png"));

        struct Size { const char* name; int w, h; };
        const Size sizes[] = { { "normal", 1480, 900 }, { "smallest", 1100, 690 }, { "tall", 1100, 1500 } };   // window limit 1100 x 720
        juce::StringArray report;
        for (auto& sz : sizes)
            for (int tab = 0; tab < 3; ++tab)
            {
                mc.setSize (sz.w, sz.h);
                mc.setTab (tab);
                auto found = findLayoutProblems (mc);
                layoutProblems += found.size();
                const char* tabNames[] = { "Look", "Effects", "Patterns" };
                const char* tabFiles[] = { "look", "effects", "patterns" };
                if (tab == 2) mc.testPatterns (Patterns::random);
                const juce::String where = juce::String (sz.name) + " " + juce::String (sz.w) + "x" + juce::String (sz.h) + "  " + tabNames[tab];
                report.add ((found.isEmpty() ? "PASS  " : "FAIL  ") + where + "   (" + juce::String (listControls (mc).size()) + " controls)");
                for (auto& f : found) report.add ("        " + f);
                writePng (mc.createComponentSnapshot (mc.getLocalBounds(), true, 1.0f), dir.getChildFile ("app-" + juce::String (sz.name) + "-" + tabFiles[tab] + ".png"));
            }
        // the style picker open, and full screen, at the normal size
        mc.setSize (1480, 900);
        mc.setTab (2); mc.testPatterns (Patterns::picked);
        mc.testPicker (true);
        {
            auto found = findLayoutProblems (mc);
            layoutProblems += found.size();
            report.add ((found.isEmpty() ? "PASS  " : "FAIL  ") + juce::String ("normal 1480x900  style picker open   (") + juce::String (listControls (mc).size()) + " controls)");
            for (auto& f : found) report.add ("        " + f);
            writePng (mc.createComponentSnapshot (mc.getLocalBounds(), true, 1.0f), dir.getChildFile ("app-normal-picker.png"));
        }
        mc.testPicker (false);
        mc.waitForScan();
        mc.showTime (6.0);
        mc.testFullscreen (true);
        writePng (mc.createComponentSnapshot (mc.getLocalBounds(), true, 1.0f), dir.getChildFile ("app-fullscreen.png"));
        mc.testFullscreen (false);
        report.insert (0, juce::String (layoutProblems) + " problems");
        dir.getChildFile ("layout-check.txt").replaceWithText (report.joinIntoString ("\n") + "\n");
    }
    return layoutProblems == 0 ? 0 : 3;
}

static int runGallery (const juce::File& dir)
{
    dir.createDirectory();
    Track t; setDemoTrack (t, 8);
    auto all = makeAll();
    const int W = 1280, H = 720, cols = 6, tw = 320, th = 180;
    const int rows = ((int) all.size() + cols - 1) / cols;
    juce::Image sheet (juce::Image::RGB, cols * tw, rows * th, true, juce::SoftwareImageType());
    juce::Graphics gs (sheet);
    gs.fillAll (juce::Colours::black);
    juce::String index;
    const juce::String only = juce::SystemStats::getEnvironmentVariable ("BCV_ONLY", {});
    const int onlyA = only.upToFirstOccurrenceOf ("-", false, false).getIntValue(), onlyB = only.fromFirstOccurrenceOf ("-", false, false).getIntValue();
    for (size_t i = 0; i < all.size(); ++i)
    {
        if (only.isNotEmpty() && ((int) i + 1 < onlyA || (int) i + 1 > onlyB)) continue;
        Analyzer an; Renderer rr; Frame fr; Look look;
        look.title = all[i]->name(); look.fades = false;
        look.tint = juce::SystemStats::getEnvironmentVariable ("BCV_TINT", "0").getIntValue();
        all[i]->reset();
        juce::Image img (juce::Image::ARGB, W, H, true, juce::SoftwareImageType());
        for (int f = 0; f <= 120; ++f)          // 4 s at 30 fps so trails and particles build up
        {
            an.analyse (t.audio, Track::rate, f / 30.0, 30, fr);
            fr.duration = t.seconds(); fr.index = f;
            juce::Graphics g (img);
            rr.render (g, img.getBounds().toFloat(), fr, *all[i], look);
        }
        writePng (img, dir.getChildFile (juce::String ((int) i + 1).paddedLeft ('0', 2) + "-" + all[i]->name().removeCharacters (" &/").toLowerCase() + ".png"));
        gs.setOpacity (1.0f);   // (drawImage uses the current colour's alpha)
        gs.drawImage (img, juce::Rectangle<float> ((float) ((int) i % cols * tw), (float) ((int) i / cols * th), (float) tw, (float) th));
        gs.setColour (juce::Colours::white.withAlpha (0.25f));
        gs.drawRect (juce::Rectangle<int> ((int) i % cols * tw, (int) i / cols * th, tw, th), 1);
        index << juce::String ((int) i + 1) << ". " << all[i]->name() << " - " << all[i]->blurb() << "\n";
    }
    if (only.isNotEmpty()) return 0;
    writePng (sheet, dir.getChildFile ("00-all-styles.png"));
    dir.getChildFile ("styles.txt").replaceWithText (index);

    // the same contact sheet in Multi-colour (smaller renders, it's only a sheet)
    auto sheetIn = [&] (int tint, const juce::String& file)
    {
        juce::Image multi (juce::Image::RGB, cols * tw, rows * th, true, juce::SoftwareImageType());
        juce::Graphics gm (multi);
        gm.fillAll (juce::Colours::black);
        auto fresh = makeAll();
        for (size_t i = 0; i < fresh.size(); ++i)
        {
            Analyzer an; Renderer rr; Frame fr; Look look;
            look.title = fresh[i]->name(); look.fades = false; look.tint = tint;
            juce::Image img (juce::Image::ARGB, 640, 360, true, juce::SoftwareImageType());
            for (int f = 0; f <= 90; ++f)
            {
                an.analyse (t.audio, Track::rate, f / 30.0, 30, fr);
                fr.duration = t.seconds(); fr.index = f;
                juce::Graphics g (img);
                rr.render (g, img.getBounds().toFloat(), fr, *fresh[i], look);
            }
            gm.drawImage (img, juce::Rectangle<float> ((float) ((int) i % cols * tw), (float) ((int) i / cols * th), (float) tw, (float) th));
        }
        writePng (multi, dir.getChildFile (file));
    };
    sheetIn (kMultiColour, "00-all-styles-multicolour.png");

    // every colour on one style, so it's easy to pick
    {
        const auto names = tintNames();
        const int n = names.size(), ccols = 6, crows = (n + ccols - 1) / ccols;
        juce::Image sw (juce::Image::RGB, ccols * tw, crows * th, true, juce::SoftwareImageType());
        juce::Graphics gc (sw);
        gc.fillAll (juce::Colours::black);
        int kali = 0;
        auto fresh = makeAll();
        for (size_t i = 0; i < fresh.size(); ++i) if (fresh[i]->name() == "Kaliset") kali = (int) i;
        for (int c = 0; c < n; ++c)
        {
            auto styles = makeAll();
            Analyzer an; Renderer rr; Frame fr; Look look;
            look.title = names[c]; look.artist = {}; look.fades = false; look.tint = c; look.vignette = false;
            juce::Image img (juce::Image::ARGB, 640, 360, true, juce::SoftwareImageType());
            for (int f = 0; f <= 45; ++f)
            {
                an.analyse (t.audio, Track::rate, f / 30.0, 30, fr);
                fr.duration = t.seconds(); fr.index = f;
                juce::Graphics g (img);
                rr.render (g, img.getBounds().toFloat(), fr, *styles[(size_t) kali], look);
            }
            gc.drawImage (img, juce::Rectangle<float> ((float) (c % ccols * tw), (float) (c / ccols * th), (float) tw, (float) th));
        }
        writePng (sw, dir.getChildFile ("00-colours.png"));
    }

    return runLayoutCheck (dir);
}

static int runExport (const juce::StringArray& args, int at)
{
    juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile (args[at + 1].unquoted());
    auto opt = [&] (const char* name, int def) { int i = args.indexOf (name); return i >= 0 && i + 1 < args.size() ? args[i + 1].getIntValue() : def; };
    int si = args.indexOf ("--seconds");
    double secs = juce::jmax (1.0, si >= 0 && si + 1 < args.size() ? args[si + 1].getDoubleValue() : 12.0);
    int style = opt ("--style", 0), w = opt ("--width", 1920), h = opt ("--height", 1080), fps = opt ("--fps", 30), tint = opt ("--tint", 0);
    int ci = args.indexOf ("--cycle");
    double cycle = juce::jmax (0.25, ci >= 0 && ci + 1 < args.size() ? args[ci + 1].getDoubleValue() : 4.0);
    Track t;
    int ai = args.indexOf ("--audio");
    juce::String err;
    if (ai >= 0 && ai + 1 < args.size()) { if (! loadTrack (juce::File (args[ai + 1].unquoted()), t, err)) { juce::Logger::writeToLog (err); return 2; } }
    else setDemoTrack (t, secs);
    Look look; look.style = style; look.tint = juce::jlimit (0, tintNames().size() - 1, tint);
    look.title = style < 0 ? juce::String() : makeAll()[(size_t) juce::jlimit (0, (int) makeAll().size() - 1, style)]->name();
    ExportJob job (t, look, out, w, h, fps, cycle);
    const int pmode = opt ("--pattern", 0), threads = opt ("--threads", 1);
    job.threads = threads <= 0 ? Workers::recommended() : threads;
    job.hevc = args.contains ("--hevc");
    juce::String extra;
    if (pmode > 0)
    {
        Patterns pat; pat.mode = juce::jlimit (1, 2, pmode); pat.picks = { 0, 12, 40, 53 };
        const int every = opt ("--every", 0);
        if (every > 0) { pat.when = Patterns::timer; pat.everySeconds = every; }
        pat.fadeSeconds = 1.5;
        auto ev = scanSong (t.audio, Track::rate);
        job.plan = makePlan (pat, ev, juce::jmax (0, style), (int) makeAll().size());
        extra << ", " << (int) job.plan.steps.size() << " pattern steps";
    }
    bool ok = job.runNow();
    juce::String report = (ok ? "OK " : "FAILED ") + out.getFullPathName() + " " + juce::String (out.getSize()) + " bytes, "
                        + juce::String (job.secondsTaken, 1) + " s, " + juce::String (job.secondsTaken > 0 ? t.seconds() * fps / job.secondsTaken : 0.0, 1) + " fps, "
                        + juce::String (job.threads) + " threads, " + job.encoderInfo + extra + (ok ? juce::String() : " : " + job.error);
    juce::Logger::writeToLog (report);
    out.getSiblingFile (out.getFileNameWithoutExtension() + ".log").replaceWithText (report + "\n");
    return ok ? 0 : 1;
}

/** Prints what the analyser hears, a few times a second (for checking the reactions against a track). */
static int runAnalyse (const juce::String& path)
{
    Track t; juce::String err;
    if (! loadTrack (juce::File (path), t, err)) { std::printf ("%s\n", err.toRawUTF8()); return 2; }
    Analyzer an; Frame f;
    std::printf ("t     sec chg  drop swell pitch pan   width noise crunch tail sil  dens drone att  bright hits(sub..air)\n");
    for (int i = 0; i < (int) (t.seconds() * 30); ++i)
    {
        an.analyse (t.audio, Track::rate, i / 30.0, 30, f);
        if (i % 8 != 0) continue;
        juce::String hits;
        for (int z = 0; z < kZones; ++z) hits << (f.zoneHit[(size_t) z] > 0.5f ? "#" : f.zones[(size_t) z] > 0.5f ? "." : " ");
        std::printf ("%5.1f %3d %4.2f %4.2f %5.2f %5.2f %5.2f %5.2f %5.2f %5.2f  %4.2f %4.2f %4.2f %5.2f %4.2f %5.2f |%s|\n", f.t, f.section, f.change, f.drop, f.swell,
                     f.pitchMove, f.pan, f.width, f.noisiness, f.crunch, f.tail, f.silence, f.density, f.drone, f.attack, f.brightness, hits.toRawUTF8());
    }
    return 0;
}

/** Times each style at preview size (ms per frame), with and without Reactions. */
static int runBench (int w, int h)
{
    Track t; setDemoTrack (t, 8);
    auto all = makeAll();
    double totalOff = 0, totalWild = 0;
    for (size_t i = 0; i < all.size(); ++i)
    {
        double ms[2] {};
        for (int react = 0; react < 2; ++react)
        {
            all[i]->reset();
            Analyzer an; Renderer rr; Frame fr; Look look; look.title = all[i]->name(); look.reactions = react ? 2 : 0;
            juce::Image img (juce::Image::ARGB, w, h, true, juce::SoftwareImageType());
            for (int f = 0; f < 75; ++f)
            {
                an.analyse (t.audio, Track::rate, 2.0 + f / 30.0, 30, fr);
                fr.duration = t.seconds(); fr.index = f;
                auto t0 = juce::Time::getMillisecondCounterHiRes();
                { juce::Graphics g (img); rr.render (g, img.getBounds().toFloat(), fr, *all[i], look); }
                if (f >= 15) ms[react] += juce::Time::getMillisecondCounterHiRes() - t0;
            }
            ms[react] /= 60.0;
        }
        totalOff += ms[0]; totalWild += ms[1];
        std::printf ("%2d %-20s off %6.1f ms   wild %6.1f ms\n", (int) i + 1, all[i]->name().toRawUTF8(), ms[0], ms[1]);
    }
    std::printf ("average: off %.1f ms, wild %.1f ms\n", totalOff / all.size(), totalWild / all.size());
    return 0;
}

class MainWindow : public juce::DocumentWindow
{
public:
    MainWindow() : juce::DocumentWindow ("BC Visualizer", col::bg, juce::DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new MainComponent(), true);
        setResizable (true, true);
        setResizeLimits (1100, 720, 4000, 3000);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
        getContentComponent()->grabKeyboardFocus();
    }
    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
};

class App : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "BC Visualizer"; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise (const juce::String& cmd) override
    {
        juce::LookAndFeel::setDefaultLookAndFeel (&look);
        auto args = juce::StringArray::fromTokens (cmd, true);
        int g = args.indexOf ("--gallery"), e = args.indexOf ("--test-export"), an = args.indexOf ("--analyse");
        if (an >= 0 && an + 1 < args.size()) { setApplicationReturnValue (runAnalyse (args[an + 1].unquoted())); quit(); return; }
        int bn = args.indexOf ("--bench");
        if (bn >= 0) { setApplicationReturnValue (runBench (args[bn + 1].getIntValue() > 0 ? args[bn + 1].getIntValue() : 1100, args[bn + 2].getIntValue() > 0 ? args[bn + 2].getIntValue() : 620)); quit(); return; }
        if (g >= 0 && g + 1 < args.size()) { setApplicationReturnValue (runGallery (juce::File::getCurrentWorkingDirectory().getChildFile (args[g + 1].unquoted()))); quit(); return; }
        int lc = args.indexOf ("--layout-check");
        if (lc >= 0 && lc + 1 < args.size()) { setApplicationReturnValue (runLayoutCheck (juce::File::getCurrentWorkingDirectory().getChildFile (args[lc + 1].unquoted()))); quit(); return; }
        if (e >= 0 && e + 1 < args.size()) { setApplicationReturnValue (runExport (args, e)); quit(); return; }
        window = std::make_unique<MainWindow>();
    }
    void shutdown() override { window.reset(); juce::LookAndFeel::setDefaultLookAndFeel (nullptr); }
    void systemRequestedQuit() override { quit(); }

private:
    tk::Look look;
    std::unique_ptr<MainWindow> window;
};
}

START_JUCE_APPLICATION (bcv::App)
