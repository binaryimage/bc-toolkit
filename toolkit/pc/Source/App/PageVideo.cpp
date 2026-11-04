// The BC Visualizer inside the Toolkit: turn a track into a YouTube-ready MP4 with one of the audio-reactive styles.
// The styles, renderer and MP4 encoder are the standalone app's own code (visualizer/Source); playback goes through the Engine.
#include "Pages.h"
#include "Session.h"

namespace tk
{
namespace
{
juce::File videoFolder() { return Settings::get().folder ("videoFolder", musicFolder().getChildFile ("BC Visualizer")); }

class VideoPreview : public Screen
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
        if (frame.isValid()) g.drawImageAt (frame, videoArea().getX(), videoArea().getY());
    }
};

class StyleRows : public juce::ListBoxModel
{
public:
    std::vector<std::unique_ptr<bcv::Viz>>* all = nullptr;
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

class VideoPage : public Page, public juce::FileDragAndDropTarget
{
public:
    VideoPage (Engine& e) : Page (e, "video", "Video",
                                  "Turns a track into a music video for YouTube: audio-reactive styles, your title and artist name, exported as an MP4.")
    {
        all = bcv::makeAll();
        loadPrefs();
        track = std::make_shared<bcv::Track>();

        for (auto* s : { &stylesSlab, &previewSlab, &settingsSlab }) addAndMakeVisible (s);
        stylesSlab.setTitle ("Styles"); previewSlab.setTitle ("Preview"); settingsSlab.setTitle ("Video");
        rows.all = &all;
        rows.onPick = [this] (int i) { look.style = i; all[(size_t) i]->reset(); blurb->setText (all[(size_t) i]->blurb(), juce::dontSendNotification); savePrefs(); frameDirty = true; };
        styles.setModel (&rows);
        styles.setRowHeight (30);
        styles.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        stylesSlab.addAndMakeVisible (styles);

        previewSlab.addAndMakeVisible (preview);
        openBtn = makeButton ("Open track...", [this] { chooseTrack(); });
        playBtn = makeButton ("Play", [this] { togglePlay(); });
        stopBtn = makeButton ("Stop", [this] { stop(); });
        trackLabel = makeLabel ("No track loaded. Open one, drop one here, or send one from Record or the sample library.", 14, col::text2, 1);
        timeLabel = makeLabel ("0:00 / 0:00", 13, col::text3, 3);
        timeLabel->setJustificationType (juce::Justification::centredRight);
        position.setSliderStyle (juce::Slider::LinearHorizontal);
        position.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        position.setRange (0, 1, 0);
        position.onDragStart = [this] { scrubbing = true; };
        position.onDragEnd = [this] { scrubbing = false; seek (position.getValue() * track->seconds()); };
        for (juce::Component* c : std::initializer_list<juce::Component*> { openBtn.get(), playBtn.get(), stopBtn.get(), trackLabel.get(), timeLabel.get(), &position })
            previewSlab.addAndMakeVisible (c);

        auto label = [this] (const juce::String& s) { auto l = makeLabel (s, 12, col::text3, 1); settingsSlab.addAndMakeVisible (*l); labels.push_back (std::move (l)); };
        label ("Title"); label ("Artist"); label ("Colour"); label ("Background"); label ("Text"); label ("Intensity"); label ("Size"); label ("Frame rate");
        for (auto* ed : { &titleEd, &artistEd })
        {
            ed->setFont (font (15));
            ed->onTextChange = [this] { look.title = titleEd.getText(); look.artist = artistEd.getText(); savePrefs(); frameDirty = true; };
            settingsSlab.addAndMakeVisible (*ed);
        }
        artistEd.setText (look.artist, false);
        auto combo = [this] (juce::ComboBox& c, const juce::StringArray& items, int sel, std::function<void (int)> on)
        {
            c.addItemList (items, 1); styleCombo (c); c.setSelectedItemIndex (sel, juce::dontSendNotification);
            c.onChange = [this, &c, on] { on (c.getSelectedItemIndex()); savePrefs(); frameDirty = true; };
            settingsSlab.addAndMakeVisible (c);
        };
        combo (tintBox, bcv::tintNames(), look.tint, [this] (int i) { look.tint = i; });
        combo (bgBox, bcv::backgroundNames(), look.background, [this] (int i) { look.background = i; });
        combo (textBox, bcv::textNames(), look.text, [this] (int i) { look.text = i; });
        juce::StringArray sizes; for (auto& s : bcv::videoSizes()) sizes.add (s.label);
        combo (sizeBox, sizes, sizeIndex, [this] (int i) { sizeIndex = i; });
        combo (fpsBox, { "30 fps", "60 fps" }, fps60 ? 1 : 0, [this] (int i) { fps60 = i == 1; });
        combo (reactBox, bcv::reactionNames(), look.reactions, [this] (int i) { look.reactions = i; renderer.reset(); });
        reactLbl = makeLabel ("Reactions", 12, col::text3, 1); settingsSlab.addAndMakeVisible (*reactLbl);
        intensity.setSliderStyle (juce::Slider::LinearHorizontal);
        intensity.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        intensity.setRange (0.25, 2.0, 0.01);
        intensity.setValue (look.intensity, juce::dontSendNotification);
        intensity.onValueChange = [this] { look.intensity = (float) intensity.getValue(); savePrefs(); frameDirty = true; };
        settingsSlab.addAndMakeVisible (intensity);
        auto toggle = [this] (juce::ToggleButton& t, const juce::String& s, bool on, std::function<void (bool)> fn)
        {
            t.setButtonText (s); t.setToggleState (on, juce::dontSendNotification);
            t.onClick = [this, &t, fn] { fn (t.getToggleState()); savePrefs(); frameDirty = true; };
            settingsSlab.addAndMakeVisible (t);
        };
        toggle (fadesBtn, "Fade in and out", look.fades, [this] (bool b) { look.fades = b; });
        toggle (vignetteBtn, "Vignette and grain", look.vignette, [this] (bool b) { look.vignette = b; });
        toggle (progressBtn, "Progress line", look.progress, [this] (bool b) { look.progress = b; });
        toggle (badgeBtn, "Cover next to title", look.coverBadge, [this] (bool b) { look.coverBadge = b; });
        coverBtn = makeButton ("Cover art...", [this] { chooseCover(); });
        clearCoverBtn = makeButton ("Clear", [this] { look.cover = {}; Settings::get().set ("videoCover", juce::String()); frameDirty = true; });
        exportBtn = makeButton ("Export MP4", [this] { startExport(); });
        cancelBtn = makeButton ("Cancel", [this] { if (job) job->signalThreadShouldExit(); });
        folderBtn = makeButton ("Show folder", [this] { videoFolder().createDirectory(); videoFolder().startAsProcess(); });
        blurb = makeLabel ({}, 13, col::text2);
        blurb->setJustificationType (juce::Justification::topLeft);
        status = makeLabel ("Videos go to " + videoFolder().getFullPathName() + ".", 12.5f, col::text3);
        status->setJustificationType (juce::Justification::topLeft);
        for (juce::Component* c : std::initializer_list<juce::Component*> { coverBtn.get(), clearCoverBtn.get(), exportBtn.get(), cancelBtn.get(), folderBtn.get(), blurb.get(), status.get(), &progress })
            settingsSlab.addAndMakeVisible (c);
        cancelBtn->setEnabled (false);

        styles.selectRow (juce::jlimit (0, (int) all.size() - 1, look.style));
        bcv::setDemoTrack (*track, 32);
        attachTrack();
        trackLabel->setText ("Demo loop. Open a track, drop one here, or send one from Record or the sample library.", juce::dontSendNotification);
        titleEd.setText ("Demo loop", true);
        setWantsKeyboardFocus (true);
    }

    ~VideoPage() override
    {
        if (job) job->stopThread (15000);
        styles.setModel (nullptr);
    }

    // ---------------------------------------------------------------- layout
    void resized() override
    {
        auto r = getLocalBounds();
        stylesSlab.setBounds (r.removeFromLeft (250));
        r.removeFromLeft (8);
        settingsSlab.setBounds (r.removeFromRight (320));
        r.removeFromRight (8);
        previewSlab.setBounds (r);
        styles.setBounds (stylesSlab.content());

        auto p = previewSlab.content();
        auto top = p.removeFromTop (34);
        openBtn->setBounds (top.removeFromLeft (130));
        top.removeFromLeft (12);
        trackLabel->setBounds (top);
        auto bottom = p.removeFromBottom (36);
        playBtn->setBounds (bottom.removeFromLeft (90)); bottom.removeFromLeft (8);
        stopBtn->setBounds (bottom.removeFromLeft (90)); bottom.removeFromLeft (12);
        timeLabel->setBounds (bottom.removeFromRight (120));
        position.setBounds (bottom.reduced (0, 8));
        p.removeFromBottom (8); p.removeFromTop (8);
        preview.setBounds (p);

        auto s = settingsSlab.content();
        size_t li = 0;
        auto row = [&] (juce::Component& c, int h = 30) { labels[li++]->setBounds (s.removeFromTop (18)); c.setBounds (s.removeFromTop (h)); s.removeFromTop (6); };
        blurb->setBounds (s.removeFromTop (50)); s.removeFromTop (4);
        row (titleEd); row (artistEd); row (tintBox); row (bgBox); row (textBox);
        {
            auto lab = s.removeFromTop (18); labels[li++]->setBounds (lab.removeFromLeft (lab.getWidth() * 55 / 100)); reactLbl->setBounds (lab.withTrimmedLeft (8));
            auto rr = s.removeFromTop (28); intensity.setBounds (rr.removeFromLeft (rr.getWidth() * 55 / 100)); reactBox.setBounds (rr.withTrimmedLeft (8));
            s.removeFromTop (6);
        }
        auto cov = s.removeFromTop (30); coverBtn->setBounds (cov.removeFromLeft (cov.getWidth() * 2 / 3 - 4)); cov.removeFromLeft (8); clearCoverBtn->setBounds (cov);
        s.removeFromTop (4);
        fadesBtn.setBounds (s.removeFromTop (24)); vignetteBtn.setBounds (s.removeFromTop (24)); progressBtn.setBounds (s.removeFromTop (24)); badgeBtn.setBounds (s.removeFromTop (24));
        s.removeFromTop (6);
        auto two = s.removeFromTop (18);
        labels[li]->setBounds (two.removeFromLeft (two.getWidth() / 2)); labels[li + 1]->setBounds (two.withTrimmedLeft (8));
        auto twoC = s.removeFromTop (30);
        sizeBox.setBounds (twoC.removeFromLeft (twoC.getWidth() / 2)); fpsBox.setBounds (twoC.withTrimmedLeft (8));
        s.removeFromTop (10);
        auto ex = s.removeFromTop (40); exportBtn->setBounds (ex.removeFromLeft (ex.getWidth() * 2 / 3 - 4)); ex.removeFromLeft (8); cancelBtn->setBounds (ex);
        s.removeFromTop (6);
        progress.setBounds (s.removeFromTop (22));
        s.removeFromTop (6);
        folderBtn->setBounds (s.removeFromBottom (30));
        status->setBounds (s);
        frameDirty = true;
    }

    // ---------------------------------------------------------------- tracks
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

    bool busy()
    {
        if (job == nullptr) return false;
        status->setText ("Wait for the export to finish (or cancel it) before changing the track.", juce::dontSendNotification);
        return true;
    }

    void openTrack (const juce::File& f)
    {
        if (busy()) return;
        juce::String err;
        auto t = std::make_shared<bcv::Track>();
        if (! bcv::loadTrack (f, *t, err)) { status->setText (err, juce::dontSendNotification); return; }
        stop();
        track = t;
        attachTrack();
        trackLabel->setText (f.getFileName() + "   " + fmtTime (track->seconds()), juce::dontSendNotification);
        titleEd.setText (f.getFileNameWithoutExtension(), true);
        Settings::get().set ("videoLastFolder", f.getParentDirectory().getFullPathName());
    }

    /** From Record, the sample library or the chop station: converted to the 48 kHz stereo the MP4 is written at. */
    void receiveAudio (const juce::AudioBuffer<float>& b, double rate, const juce::String& nm) override
    {
        if (b.getNumSamples() == 0 || busy()) return;
        stop();
        auto t = std::make_shared<bcv::Track>();
        const int n = b.getNumSamples();
        const double ratio = rate / bcv::Track::rate;
        const int outN = (int) std::ceil (n / ratio);
        t->audio.setSize (2, outN);
        for (int ch = 0; ch < 2; ++ch)
        {
            const float* in = b.getReadPointer (juce::jmin (ch, b.getNumChannels() - 1));
            if (std::abs (ratio - 1.0) < 1e-9) { t->audio.copyFrom (ch, 0, in, juce::jmin (n, outN)); continue; }
            std::vector<float> padded ((size_t) n + 8, 0.0f);
            std::copy (in, in + n, padded.begin());
            juce::LagrangeInterpolator li;
            li.process (ratio, padded.data(), t->audio.getWritePointer (ch), outN);
        }
        track = t;
        attachTrack();
        trackLabel->setText (nm + "   " + fmtTime (track->seconds()), juce::dontSendNotification);
        titleEd.setText (nm, true);
    }

    void setCover (const juce::File& f)
    {
        auto img = juce::ImageFileFormat::loadFrom (f);
        if (! img.isValid()) { status->setText ("Couldn't read that image.", juce::dontSendNotification); return; }
        look.cover = img;
        if (look.background == 0) { look.background = 1; bgBox.setSelectedItemIndex (1, juce::dontSendNotification); }
        Settings::get().set ("videoCover", f.getFullPathName());
        savePrefs(); frameDirty = true;
    }

    void chooseTrack()
    {
        chooser = std::make_unique<juce::FileChooser> ("Open a track", juce::File (Settings::get().str ("videoLastFolder", musicFolder().getFullPathName())),
                                                       "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) openTrack (fc.getResult()); });
    }
    void chooseCover()
    {
        chooser = std::make_unique<juce::FileChooser> ("Cover art", juce::File (Settings::get().str ("videoLastFolder", musicFolder().getFullPathName())), "*.png;*.jpg;*.jpeg");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) setCover (fc.getResult()); });
    }

    // ---------------------------------------------------------------- transport (through the Engine, no copy of the track)
    void attachTrack()
    {
        clockT = 0; analyzer.reset(); all[(size_t) look.style]->reset();
        frameDirty = true;
    }

    SharedAudio sharedTrack() const { return SharedAudio (track, &track->audio); }

    void startVoice()
    {
        PlayOptions o; o.start = (int) (clockT * bcv::Track::rate); o.fadeInMs = clockT > 0 ? 2.0f : 0.0f;
        token = engine.play (sharedTrack(), bcv::Track::rate, o);
    }

    void togglePlay()
    {
        if (playing) { pause(); return; }
        if (clockT >= track->seconds() - 0.05) seek (0);
        playing = true;
        startVoice();
        lastTick = juce::Time::getMillisecondCounterHiRes();
        playBtn->setButtonText ("Pause");
        playBtn->setToggleState (true, juce::dontSendNotification);
    }
    void pause()
    {
        if (playing && engine.playingToken() == token) engine.stopPlayback();
        playing = false;
        playBtn->setButtonText ("Play");
        playBtn->setToggleState (false, juce::dontSendNotification);
    }
    void stop() { pause(); seek (0); }
    void seek (double t)
    {
        clockT = juce::jlimit (0.0, juce::jmax (0.0, track->seconds()), t);
        analyzer.reset(); all[(size_t) look.style]->reset(); renderer.reset();
        if (playing) startVoice();
        frameDirty = true;
    }

    // ---------------------------------------------------------------- frames
    void tick() override
    {
        if (job && job->finished) finishExport();
        if (job) { auto& s = bcv::videoSizes()[(size_t) sizeIndex]; progress.setValue (job->progress, juce::String ((int) (job->progress * 100)) + "%   " + juce::String (s.h) + "p"); }
        if (playing)
        {
            if (engine.hasOutput())
            {
                if (engine.playingToken() != token) pause();   // something else took over the output
                else
                {
                    clockT = engine.playPosition() / bcv::Track::rate;
                    if (! engine.isPlaying())                     // reached the end, or was stopped (e.g. by leaving the page)
                    {
                        pause();
                        if (clockT >= track->seconds() - 0.1) clockT = track->seconds();
                    }
                }
            }
            else { auto now = juce::Time::getMillisecondCounterHiRes(); clockT += (now - lastTick) / 1000.0; lastTick = now; }
            if (clockT >= track->seconds()) { pause(); clockT = track->seconds(); }
            renderFrame();
        }
        else if (frameDirty) renderFrame();
    }

    void renderFrame()
    {
        frameDirty = false;
        auto area = preview.videoArea();
        if (area.isEmpty()) return;
        if (! preview.frame.isValid() || preview.frame.getBounds() != area.withZeroOrigin())
            preview.frame = juce::Image (juce::Image::ARGB, area.getWidth(), area.getHeight(), true, juce::SoftwareImageType());
        if (playing || analyzerT != clockT) analyzer.analyse (track->audio, bcv::Track::rate, clockT, 30, frame);
        analyzerT = clockT;
        frame.duration = track->seconds();
        { juce::Graphics g (preview.frame); renderer.render (g, preview.frame.getBounds().toFloat(), frame, *all[(size_t) look.style], look); }
        preview.repaint (area);
        if (! scrubbing && track->seconds() > 0) position.setValue (clockT / track->seconds(), juce::dontSendNotification);
        timeLabel->setText (fmtTime (clockT) + " / " + fmtTime (track->seconds()), juce::dontSendNotification);
    }

    // ---------------------------------------------------------------- export
    void startExport()
    {
        if (job || ! track->loaded()) return;
        pause();
        auto nm = juce::File::createLegalFileName (look.title.isNotEmpty() ? look.title : "BC Video");
        auto out = videoFolder().getNonexistentChildFile (nm, ".mp4", false);
        auto& s = bcv::videoSizes()[(size_t) sizeIndex];
        exportTrack = track;   // the job reads this while it runs
        job = std::make_unique<bcv::ExportJob> (*exportTrack, look, out, s.w, s.h, fps60 ? 60 : 30);
        job->startThread (juce::Thread::Priority::normal);
        exportBtn->setEnabled (false); cancelBtn->setEnabled (true); openBtn->setEnabled (false);
        status->setText ("Rendering " + out.getFileName() + "... you can keep previewing, or use other pages.", juce::dontSendNotification);
    }

    void finishExport()
    {
        job->stopThread (10000);
        if (job->ok) status->setText ("Saved " + job->output.getFileName() + " in " + juce::String (juce::roundToInt (job->secondsTaken)) + " s. Ready for YouTube.", juce::dontSendNotification);
        else if (job->cancelled) status->setText ("Export cancelled.", juce::dontSendNotification);
        else status->setText ("Export failed: " + job->error, juce::dontSendNotification);
        progress.setValue (job->ok ? 1.0 : 0.0, job->ok ? "Done" : "");
        job.reset();
        exportTrack.reset();
        exportBtn->setEnabled (true); cancelBtn->setEnabled (false); openBtn->setEnabled (true);
    }

    // ---------------------------------------------------------------- settings
    void loadPrefs()
    {
        auto& p = Settings::get();
        look.style = p.num ("videoStyle", 0);
        look.artist = p.str ("videoArtist", "binaryimage");
        look.tint = p.num ("videoTint", 0);
        look.background = p.num ("videoBackground", 0);
        look.text = p.num ("videoText", 0);
        look.fades = p.flag ("videoFades", true);
        look.vignette = p.flag ("videoVignette", true);
        look.progress = p.flag ("videoProgress", false);
        look.coverBadge = p.flag ("videoCoverBadge", true);
        look.reactions = juce::jlimit (0, 2, p.num ("videoReactions", 2));
        look.tint = juce::jlimit (0, bcv::tintNames().size() - 1, look.tint);
        look.intensity = (float) p.dbl ("videoIntensity", 1.0);
        sizeIndex = juce::jlimit (0, (int) bcv::videoSizes().size() - 1, p.num ("videoSize", 0));
        fps60 = p.flag ("videoFps60", false);
        juce::File cov (p.str ("videoCover"));
        if (cov.existsAsFile()) look.cover = juce::ImageFileFormat::loadFrom (cov);
    }
    void savePrefs()
    {
        auto& p = Settings::get().props();
        p.setValue ("videoStyle", look.style); p.setValue ("videoArtist", look.artist); p.setValue ("videoTint", look.tint);
        p.setValue ("videoBackground", look.background); p.setValue ("videoText", look.text); p.setValue ("videoFades", look.fades);
        p.setValue ("videoVignette", look.vignette); p.setValue ("videoProgress", look.progress); p.setValue ("videoCoverBadge", look.coverBadge); p.setValue ("videoReactions", look.reactions); p.setValue ("videoIntensity", (double) look.intensity);
        p.setValue ("videoSize", sizeIndex); p.setValue ("videoFps60", fps60);
        p.saveIfNeeded();
    }

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

    void demo() override
    {
        bcv::setDemoTrack (*track, 8);
        attachTrack();
        styles.selectRow (juce::jmin (5, (int) all.size() - 1));
        look.title = "Fifth Ward Haunt"; titleEd.setText (look.title, false);
        for (int f = 0; f <= 90; ++f) { clockT = f / 30.0; renderFrame(); }   // 3 s in, so trails and particles have built up
    }

    std::vector<std::unique_ptr<bcv::Viz>> all;
    bcv::Look look;
    std::shared_ptr<bcv::Track> track, exportTrack;
    bcv::Renderer renderer;
    bcv::Analyzer analyzer;
    bcv::Frame frame;
    int sizeIndex = 0, token = -1; bool fps60 = false;

    Slab stylesSlab, previewSlab, settingsSlab;
    StyleRows rows; juce::ListBox styles;
    VideoPreview preview;
    std::unique_ptr<juce::TextButton> openBtn, playBtn, stopBtn, coverBtn, clearCoverBtn, exportBtn, cancelBtn, folderBtn;
    std::unique_ptr<juce::Label> trackLabel, timeLabel, blurb, status;
    std::vector<std::unique_ptr<juce::Label>> labels;
    juce::Slider position, intensity;
    juce::TextEditor titleEd, artistEd;
    juce::ComboBox tintBox, bgBox, textBox, sizeBox, fpsBox, reactBox;
    std::unique_ptr<juce::Label> reactLbl;
    juce::ToggleButton fadesBtn, vignetteBtn, progressBtn, badgeBtn;
    ProgressPill progress;
    std::unique_ptr<juce::FileChooser> chooser;
    bool playing = false, scrubbing = false, frameDirty = true;
    double clockT = 0, lastTick = 0, analyzerT = -1;
    std::unique_ptr<bcv::ExportJob> job;
};
}

std::unique_ptr<Page> makeVideoPage (Engine& e) { return std::make_unique<VideoPage> (e); }
}
