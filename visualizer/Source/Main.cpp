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
        stylesModel.onPick = [this] (int i) { look.style = i; all[(size_t) i]->reset(); blurb->setText (all[(size_t) i]->blurb(), juce::dontSendNotification); savePrefs(); redrawIfPaused(); };
        styles.setModel (&stylesModel);
        styles.setRowHeight (30);
        styles.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        addAndMakeVisible (styles);

        addAndMakeVisible (preview);
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
        for (juce::Component* c : std::initializer_list<juce::Component*> { openBtn.get(), playBtn.get(), stopBtn.get(), trackLabel.get(), timeLabel.get(), &position }) addAndMakeVisible (c);

        // settings: Look / Effects tabs (scrolling if the window is short), export pinned underneath
        for (auto* t : { &lookTab, &fxTab })
        {
            t->setRadioGroupId (41); t->setClickingTogglesState (true);
            t->onClick = [this] { prefs().setValue ("tab", fxTab.getToggleState() ? 1 : 0); showTab(); };
            addAndMakeVisible (t);
        }
        lookTab.setButtonText ("Look"); fxTab.setButtonText ("Effects");
        (prefs().getIntValue ("tab", 0) == 1 ? fxTab : lookTab).setToggleState (true, juce::dontSendNotification);
        addAndMakeVisible (paneView);
        paneView.setScrollBarsShown (true, false);
        paneView.setScrollBarThickness (8);

        auto label = [this] (const juce::String& s, juce::Component& parent) { auto l = makeLabel (s, 12, col::text3, 1); parent.addAndMakeVisible (*l); labels.push_back (std::move (l)); };
        label ("Title", lookPane); label ("Artist", lookPane); label ("Colour", lookPane); label ("Background", lookPane); label ("Text", lookPane); label ("Intensity", lookPane);
        label ("Size", *this); label ("Frame rate", *this);
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
        juce::StringArray sizes; for (auto& s : videoSizes()) sizes.add (s.label);
        combo (sizeBox, sizes, sizeIndex, [this] (int i) { sizeIndex = i; }, *this);
        combo (fpsBox, { "30 fps", "60 fps" }, fps60 ? 1 : 0, [this] (int i) { fps60 = i == 1; }, *this);
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
        showTab();

        // audio out
        juce::String err = devices.initialiseWithDefaultDevices (0, 2);
        devices.addAudioCallback (&player);
        player.setSource (&transport);

        styles.selectRow (juce::jlimit (0, (int) all.size() - 1, look.style));
        if (track.loaded() == false) { setDemoTrack (track, 32); attachTrack(); trackLabel->setText ("Demo loop. Open a track or drop one here.", juce::dontSendNotification); titleEd.setText ("Demo loop", true); }
        startTimerHz (30);   // the videos are 30 or 60 fps; 30 keeps the preview steady
        setSize (1480, 900);
    }

    ~MainComponent() override
    {
        stopTimer();
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

        auto p = previewSlab.content() + previewSlab.getPosition();
        auto top = p.removeFromTop (34);
        openBtn->setBounds (top.removeFromLeft (130));
        top.removeFromLeft (12);
        trackLabel->setBounds (top);
        auto bottom = p.removeFromBottom (36);
        const bool ownRow = bottom.getWidth() < 90 + 8 + 90 + 12 + 120 + 160;   // narrow window: slider above the buttons
        if (ownRow) { position.setBounds (p.removeFromBottom (28)); p.removeFromBottom (4); }
        playBtn->setBounds (bottom.removeFromLeft (90)); bottom.removeFromLeft (8);
        stopBtn->setBounds (bottom.removeFromLeft (90)); bottom.removeFromLeft (12);
        timeLabel->setBounds (bottom.removeFromRight (juce::jmin (120, bottom.getWidth())));
        if (! ownRow) position.setBounds (bottom.reduced (0, 8));
        p.removeFromBottom (8); p.removeFromTop (8);
        preview.setBounds (p);

        auto s = settingsSlab.content() + settingsSlab.getPosition();
        auto tabs = s.removeFromTop (32);
        lookTab.setBounds (tabs.removeFromLeft (tabs.getWidth() / 2 - 3)); tabs.removeFromLeft (6); fxTab.setBounds (tabs);
        s.removeFromTop (8);
        folderBtn->setBounds (s.removeFromBottom (30)); s.removeFromBottom (4);
        status->setBounds (s.removeFromBottom (34)); s.removeFromBottom (4);
        progress.setBounds (s.removeFromBottom (22)); s.removeFromBottom (6);
        auto ex = s.removeFromBottom (40); exportBtn->setBounds (ex.removeFromLeft (ex.getWidth() * 2 / 3 - 4)); ex.removeFromLeft (8); cancelBtn->setBounds (ex);
        s.removeFromBottom (8);
        auto twoC = s.removeFromBottom (30);
        sizeBox.setBounds (twoC.removeFromLeft (twoC.getWidth() / 2)); fpsBox.setBounds (twoC.withTrimmedLeft (8));
        auto two = s.removeFromBottom (18);
        labels[6]->setBounds (two.removeFromLeft (two.getWidth() / 2)); labels[7]->setBounds (two.withTrimmedLeft (8));
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
    }

    void showTab()
    {
        paneView.setViewedComponent (fxTab.getToggleState() ? &fxPane : &lookPane, false);
        layoutPanes();
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
    void setTab (int t) { (t == 1 ? fxTab : lookTab).setToggleState (true, juce::dontSendNotification); showTab(); }
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
    std::unique_ptr<juce::TextButton> openBtn, playBtn, stopBtn, coverBtn, clearCoverBtn, exportBtn, cancelBtn, folderBtn, randomBtn, fxAllOn, fxAllOff;
    juce::TextButton lookTab, fxTab;
    juce::Viewport paneView;
    juce::Component lookPane, fxPane;
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
        frameDirty = true;
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
        analyzer.reset(); all[(size_t) look.style]->reset(); renderer.reset();
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
        if (job) { auto& s = videoSizes()[(size_t) sizeIndex]; progress.setValue (job->progress, juce::String ((int) (job->progress * 100)) + "%   " + juce::String (s.h) + "p"); }
        if (playing)
        {
            if (haveDevice()) clockT = transport.getCurrentPosition();
            else { auto now = juce::Time::getMillisecondCounterHiRes(); clockT += (now - lastTick) / 1000.0; lastTick = now; }
            if (clockT >= track.seconds()) { pause(); clockT = track.seconds(); }
            tick();
        }
        else if (frameDirty) tick();
    }

    void tick()
    {
        frameDirty = false;
        auto area = preview.videoArea();
        if (area.isEmpty()) return;
        const float shrink = juce::jmin (1.0f, 800.0f / (float) juce::jmax (1, area.getWidth()));
        const auto size = juce::Rectangle<int> (juce::jmax (16, juce::roundToInt (area.getWidth() * shrink)), juce::jmax (9, juce::roundToInt (area.getHeight() * shrink)));
        if (! preview.frame.isValid() || preview.frame.getBounds() != size)
            preview.frame = juce::Image (juce::Image::ARGB, size.getWidth(), size.getHeight(), true, juce::SoftwareImageType());
        const double fps = 30;
        if (playing || analyzerT != clockT) analyzer.analyse (track.audio, Track::rate, clockT, fps, frame);
        analyzerT = clockT;
        frame.duration = track.seconds();
        { juce::Graphics g (preview.frame); renderer.render (g, preview.frame.getBounds().toFloat(), frame, *all[(size_t) look.style], look); }
        preview.repaint (area);
        if (! scrubbing && track.seconds() > 0) position.setValue (clockT / track.seconds(), juce::dontSendNotification);
        timeLabel->setText (fmtTime (clockT) + " / " + fmtTime (track.seconds()), juce::dontSendNotification);
    }
    double analyzerT = -1;

    // ---------------------------------------------------------------- export
    void startExport()
    {
        if (job || ! track.loaded()) return;
        pause();
        auto name = juce::File::createLegalFileName (look.title.isNotEmpty() ? look.title : "BC Visualizer");
        auto out = outputFolder().getNonexistentChildFile (name, ".mp4", false);
        auto& s = videoSizes()[(size_t) sizeIndex];
        job = std::make_unique<ExportJob> (track, look, out, s.w, s.h, fps60 ? 60 : 30);
        job->startThread (juce::Thread::Priority::normal);
        exportBtn->setEnabled (false); cancelBtn->setEnabled (true); openBtn->setEnabled (false);
        status->setText ("Rendering " + out.getFileName() + "... you can keep previewing.", juce::dontSendNotification);
    }

    void finishExport()
    {
        job->stopThread (10000);
        if (job->ok) status->setText ("Saved " + job->output.getFileName() + " in " + juce::String (job->secondsTaken, 0) + " s. Ready for YouTube.", juce::dontSendNotification);
        else if (job->cancelled) status->setText ("Export cancelled.", juce::dontSendNotification);
        else status->setText ("Export failed: " + job->error, juce::dontSendNotification);
        progress.setValue (job->ok ? 1.0 : 0.0, job->ok ? "Done" : "");
        job.reset();
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
    }

public:
    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::spaceKey) { togglePlay(); return true; }
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
            for (int tab = 0; tab < 2; ++tab)
            {
                mc.setSize (sz.w, sz.h);
                mc.setTab (tab);
                auto found = findLayoutProblems (mc);
                layoutProblems += found.size();
                const juce::String where = juce::String (sz.name) + " " + juce::String (sz.w) + "x" + juce::String (sz.h) + "  " + (tab == 0 ? "Look" : "Effects");
                report.add ((found.isEmpty() ? "PASS  " : "FAIL  ") + where + "   (" + juce::String (listControls (mc).size()) + " controls)");
                for (auto& f : found) report.add ("        " + f);
                writePng (mc.createComponentSnapshot (mc.getLocalBounds(), true, 1.0f), dir.getChildFile ("app-" + juce::String (sz.name) + "-" + (tab == 0 ? "look" : "effects") + ".png"));
            }
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
    bool ok = job.runNow();
    juce::String report = (ok ? "OK " : "FAILED ") + out.getFullPathName() + " " + juce::String (out.getSize()) + " bytes, "
                        + juce::String (job.secondsTaken, 1) + " s" + (ok ? juce::String() : " : " + job.error);
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
