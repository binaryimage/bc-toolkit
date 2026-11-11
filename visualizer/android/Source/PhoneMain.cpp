// BC Visualizer for Android: the PC app's 80 styles, renderer and MP4 export, laid out for a phone held upright,
// plus Patterns (several styles fading into each other), multi-core rendering and the phone's hardware encoder.
// The same file builds on Linux for the CI screenshots, the button check and export tests (--selftest <folder>).
#include "Session.h"
#include "Common/Look.h"
#include "Common/Widgets.h"
#include "Common/LayoutCheck.h"
#include <optional>
#ifndef JUCE_APPLICATION_VERSION_STRING
 #define JUCE_APPLICATION_VERSION_STRING "1.0.0"
#endif
#if JUCE_ANDROID
 #include <android/thermal.h>
 #include <sys/system_properties.h>
#endif

namespace bcv
{
using namespace tk;

constexpr int kRow = 44;    // finger-sized rows (logical pixels)
constexpr int kGap = 8;

// ------------------------------------------------------------ places and phone bits

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

static juce::File dataFolder() { return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("BC Visualizer"); }

static juce::File outputFolder()
{
   #if JUCE_ANDROID
    juce::File movies ("/storage/emulated/0/Movies");
    if (movies.isDirectory()) return movies.getChildFile ("BC Visualizer");
   #endif
    auto m = juce::File::getSpecialLocation (juce::File::userMoviesDirectory);
    if (! m.isDirectory()) m = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Videos");
    return m.getChildFile ("BC Visualizer");
}

static juce::String outputFolderName()
{
   #if JUCE_ANDROID
    return "Movies/BC Visualizer";
   #else
    return outputFolder().getFullPathName();
   #endif
}

/** 0 none, 1 light, 2 moderate, 3 severe, 4 critical, 5 emergency, 6 shutdown (Android's thermal status) */
static int thermalStatus()
{
   #if JUCE_ANDROID
    static AThermalManager* m = AThermal_acquireManager();
    return m != nullptr ? (int) AThermal_getCurrentThermalStatus (m) : 0;
   #else
    return 0;
   #endif
}

static juce::String heatName (int h)
{
    static const char* n[] = { "cool", "warm", "hot", "very hot", "critical", "critical", "critical" };
    return n[juce::jlimit (0, 6, h)];
}

/** Android hands out content:// links, not files: copy the picked file into our own folder. */
static juce::File importDocument (const juce::URL& url, const juce::String& baseName, juce::String& displayName)
{
    std::unique_ptr<juce::InputStream> in;
   #if JUCE_ANDROID
    auto doc = juce::AndroidDocument::fromDocument (url);
    displayName = doc.getInfo().getName();
    in = doc.createInputStream();
   #else
    auto f = url.getLocalFile();
    displayName = f.getFileName();
    in = f.createInputStream();
   #endif
    if (in == nullptr) return {};
    auto ext = displayName.containsChar ('.') ? displayName.fromLastOccurrenceOf (".", false, false).toLowerCase() : juce::String();
    if (ext.isEmpty() || ext.length() > 5) ext = "bin";
    dataFolder().createDirectory();
    for (auto& old : dataFolder().findChildFiles (juce::File::findFiles, false, baseName + ".*")) old.deleteFile();
    auto dest = dataFolder().getChildFile (baseName + "." + ext);
    {
        juce::FileOutputStream os (dest);
        if (! os.openedOk()) return {};
        os.writeFromInputStream (*in, -1);
        os.flush();
        if (os.getStatus().failed()) return {};
    }
    return dest;
}

static juce::String bitrateFor (int sizeIndex, bool fps60, int& bps)
{
    // H.264 rates; HEVC gets 60% of these (the encoder scales it). Generous: grain and static eat bits.
    static const int rates[3][2] = { { 20, 30 }, { 35, 50 }, { 60, 90 } };
    bps = rates[juce::jlimit (0, 2, sizeIndex)][fps60 ? 1 : 0] * 1000000;
    return juce::String (bps / 1000000) + " Mbps";
}

// ------------------------------------------------------------ preview, full screen and the timeline

class PreviewView : public Screen
{
public:
    juce::Image frame;
    juce::String overlay;
    std::function<void()> onTap;
    juce::Rectangle<int> videoArea() const
    {
        auto a = screenArea().reduced (4);
        float w = juce::jmin (a.getWidth(), a.getHeight() * 16.0f / 9.0f);
        return a.withSizeKeepingCentre (w, w * 9.0f / 16.0f).toNearestInt();
    }
    void paintScreen (juce::Graphics& g, juce::Rectangle<float>) override
    {
        if (frame.isValid()) { g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality); g.drawImage (frame, videoArea().toFloat()); }
        if (overlay.isNotEmpty())
        {
            auto band = videoArea().toFloat().removeFromBottom (34);
            g.setColour (juce::Colours::black.withAlpha (0.6f)); g.fillRect (band);
            g.setColour (col::text); g.setFont (font (14, 1));
            g.drawText (overlay, band.reduced (10, 0), juce::Justification::centredLeft);
        }
    }
    void mouseUp (const juce::MouseEvent& e) override { if (e.mouseWasClicked() && onTap) onTap(); }
};

/** The preview turned sideways to fill the phone. Tap to go back. */
class FullView : public juce::Component
{
public:
    juce::Image frame;
    std::function<void()> onTap;
    juce::Rectangle<int> videoSize() const
    {
        // the video is drawn rotated a quarter turn: its width runs down the screen
        const float W = (float) getHeight(), H = (float) getWidth();
        float w = juce::jmin (W, H * 16.0f / 9.0f);
        return { (int) w, (int) (w * 9.0f / 16.0f) };
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black);
        if (! frame.isValid()) return;
        auto vs = videoSize();
        const auto c = getLocalBounds().getCentre().toFloat();
        g.addTransform (juce::AffineTransform::rotation (juce::MathConstants<float>::halfPi, c.x, c.y));
        g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);
        g.drawImage (frame, juce::Rectangle<float> ((float) vs.getWidth(), (float) vs.getHeight()).withCentre (c));
    }
    void mouseUp (const juce::MouseEvent& e) override { if (e.mouseWasClicked() && onTap) onTap(); }
};

/** Which style plays when: one block per style, fades shaded, hard cuts as bright lines. Tap to jump there. */
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
        drawScreen (g, r, 6);
        auto in = r.reduced (3);
        if (note.isNotEmpty() || plan == nullptr || duration <= 0 || plan->steps.empty())
        {
            g.setColour (col::text3); g.setFont (font (12.5f));
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

// ------------------------------------------------------------ style lists

class StyleList : public juce::ListBoxModel
{
public:
    std::vector<std::unique_ptr<Viz>>* all = nullptr;
    std::function<void (int)> onPick;
    int getNumRows() override { return all ? (int) all->size() : 0; }
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
    {
        auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (2, 2);
        if (selected) drawKey (g, r, true, false, false, 7);
        g.setColour (selected ? col::black.withAlpha (0.5f) : col::text3);
        g.setFont (monoFont (13));
        g.drawText (juce::String (row + 1).paddedLeft ('0', 2), r.withWidth (36).translated (10, 0), juce::Justification::centredLeft);
        g.setColour (selected ? col::black : col::text2);
        g.setFont (font (16, selected ? 2 : 0));
        g.drawText ((*all)[(size_t) row]->name(), r.withTrimmedLeft (48), juce::Justification::centredLeft);
    }
    void selectedRowsChanged (int row) override { if (row >= 0 && onPick) onPick (row); }
};

/** Tap styles to add them to the pattern (numbered in the order picked); tap again to take one out. */
class PickList : public juce::ListBoxModel
{
public:
    std::vector<std::unique_ptr<Viz>>* all = nullptr;
    std::vector<int>* picks = nullptr;
    std::function<void()> onChange;
    int getNumRows() override { return all ? (int) all->size() : 0; }
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool) override
    {
        auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (2, 2);
        const auto it = std::find (picks->begin(), picks->end(), row);
        const bool on = it != picks->end();
        auto box = r.removeFromLeft (44).withSizeKeepingCentre (30, 30);
        if (on) drawKey (g, box, true, false, false, 6); else drawScreen (g, box, 6);
        if (on)
        {
            g.setColour (col::black); g.setFont (monoFont (13));
            g.drawText (juce::String ((int) (it - picks->begin()) + 1), box, juce::Justification::centred);
        }
        g.setColour (col::text3); g.setFont (monoFont (12));
        g.drawText (juce::String (row + 1).paddedLeft ('0', 2), r.removeFromLeft (34).translated (6, 0), juce::Justification::centredLeft);
        g.setColour (on ? col::text : col::text2); g.setFont (font (16, on ? 2 : 0));
        g.drawText ((*all)[(size_t) row]->name(), r, juce::Justification::centredLeft);
    }
    void listBoxItemClicked (int row, const juce::MouseEvent&) override
    {
        auto it = std::find (picks->begin(), picks->end(), row);
        if (it != picks->end()) picks->erase (it); else picks->push_back (row);
        if (onChange) onChange();
    }
};

class PickerPanel : public juce::Component
{
public:
    PickerPanel()
    {
        title = makeLabel ("PICK STYLES", 13, col::text3, 2);
        hint = makeLabel ("Tap the styles you want, in the order you want them. Tap again to take one out.", 13.5f, col::text2);
        hint->setJustificationType (juce::Justification::topLeft);
        count = makeLabel ({}, 13, col::text3, 3);
        count->setJustificationType (juce::Justification::centredRight);
        list.setRowHeight (kRow + 4);
        list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        allBtn = makeButton ("All", [this] { picks->clear(); for (int i = 0; i < model.getNumRows(); ++i) picks->push_back (i); changed(); });
        noneBtn = makeButton ("None", [this] { picks->clear(); changed(); });
        doneBtn = makeButton ("Done", [this] { if (onDone) onDone(); });
        for (juce::Component* c : std::initializer_list<juce::Component*> { title.get(), hint.get(), count.get(), &list, allBtn.get(), noneBtn.get(), doneBtn.get() }) addAndMakeVisible (c);
    }
    void attach (std::vector<std::unique_ptr<Viz>>& all, std::vector<int>& p)
    {
        model.all = &all; model.picks = &p; picks = &p;
        model.onChange = [this] { changed(); };
        list.setModel (&model);
    }
    ~PickerPanel() override { list.setModel (nullptr); }
    void refresh() { list.updateContent(); list.repaint(); count->setText (juce::String ((int) picks->size()) + " picked", juce::dontSendNotification); }
    void paint (juce::Graphics& g) override { g.fillAll (col::bg); }
    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 8);
        auto top = r.removeFromTop (24); count->setBounds (top.removeFromRight (110)); title->setBounds (top);
        hint->setBounds (r.removeFromTop (40)); r.removeFromTop (4);
        auto b = r.removeFromBottom (kRow); r.removeFromBottom (kGap);
        const int bw = (b.getWidth() - 2 * kGap) / 3;
        allBtn->setBounds (b.removeFromLeft (bw)); b.removeFromLeft (kGap);
        noneBtn->setBounds (b.removeFromLeft (bw)); b.removeFromLeft (kGap);
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

// ------------------------------------------------------------ the app

class MainComponent : public juce::Component, private juce::Timer, private juce::FocusChangeListener
{
public:
    enum Tab { tabStyle = 0, tabPatterns, tabLook, tabEffects, tabExport, kTabs };

    MainComponent()
    {
        all = makeAll();
        workers = std::make_unique<Workers> (Workers::recommended());
        renderer.setWorkers (workers.get());
        loadPrefs();

        // ---- top: title, track, open
        appTitle = makeLabel ("BC VISUALIZER", 12, col::text3, 2);
        trackLabel = makeLabel ("Demo loop", 14, col::text2, 1);
        openBtn = makeButton ("Open track", [this] { chooseTrack(); });
        addAndMakeVisible (*appTitle); addAndMakeVisible (*trackLabel); addAndMakeVisible (*openBtn);

        // ---- preview, transport, timeline
        preview.onTap = [this] { setFullscreen (true); };
        full.onTap = [this] { setFullscreen (false); };
        addAndMakeVisible (preview);
        addChildComponent (full);
        playBtn = makeButton ("Play", [this] { togglePlay(); });
        stopBtn = makeButton ("Stop", [this] { stop(); });
        timeLabel = makeLabel ("0:00 / 0:00", 13, col::text3, 3);
        timeLabel->setJustificationType (juce::Justification::centredRight);
        position.setSliderStyle (juce::Slider::LinearHorizontal);
        position.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        position.setRange (0, 1, 0);
        position.onDragStart = [this] { scrubbing = true; };
        position.onDragEnd = [this] { scrubbing = false; seek (position.getValue() * track->seconds()); };
        timeline.onSeek = [this] (double t) { seek (t); };
        for (juce::Component* c : std::initializer_list<juce::Component*> { playBtn.get(), stopBtn.get(), timeLabel.get(), &position, &timeline }) addAndMakeVisible (c);

        // ---- tabs
        const char* tabNames[] = { "Style", "Patterns", "Look", "Effects", "Export" };
        for (int i = 0; i < kTabs; ++i)
        {
            auto* t = tabs.add (new juce::TextButton (tabNames[i]));
            t->setRadioGroupId (51); t->setClickingTogglesState (true);
            t->onClick = [this, i] { if (tabs[i]->getToggleState()) showTab (i); };
            addAndMakeVisible (t);
        }

        buildStylePage();
        buildPatternsPage();
        buildLookPage();
        buildEffectsPage();
        buildExportPage();
        picker.attach (all, patterns.picks);
        picker.onDone = [this] { showPicker (false); };
        picker.onChange = [this] { savePrefs(); patternsChanged(); };
        addChildComponent (picker);

        // ---- audio out
        devices.initialiseWithDefaultDevices (0, 2);
        devices.addAudioCallback (&player);
        player.setSource (&transport);

        juce::Desktop::getInstance().addFocusChangeListener (this);
        styles.selectRow (juce::jlimit (0, (int) all.size() - 1, look.style), true);
        styles.scrollToEnsureRowIsOnscreen (look.style);
        useDemo();
        showTab (juce::jlimit (0, kTabs - 1, prefs().getIntValue ("tab", 0)));
        syncPatternControls();
        startTimerHz (10);
        setSize (412, 892);
    }

    ~MainComponent() override
    {
        ++scanGeneration;    // a scan still running gives up
        stopTimer();
        juce::Desktop::getInstance().removeFocusChangeListener (this);
        if (job) job->stopThread (15000);
        loader.removeAllJobs (true, 10000);
        transport.setSource (nullptr);
        player.setSource (nullptr);
        devices.removeAudioCallback (&player);
        styles.setModel (nullptr);
    }

    // ================================================================ layout
    void paint (juce::Graphics& g) override { g.fillAll (col::bg); }

    juce::BorderSize<int> insets() const
    {
        if (testInsets.has_value()) return *testInsets;
        if (auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()) return d->safeAreaInsets;
        return {};
    }

    void resized() override
    {
        auto safe = insets().subtractedFrom (getLocalBounds());
        full.setBounds (getLocalBounds());
        picker.setBounds (safe);
        auto r = safe.reduced (8, 4);
        const bool typing = titleEd.hasKeyboardFocus (false) || artistEd.hasKeyboardFocus (false);

        auto top = r.removeFromTop (kRow);
        openBtn->setBounds (top.removeFromRight (124));
        top.removeFromRight (kGap);
        appTitle->setBounds (top.removeFromTop (16));
        trackLabel->setBounds (top);
        r.removeFromTop (4);

        if (! typing)   // with the keyboard up, the preview steps aside so the box you're typing in stays visible
        {
            const int ph = juce::roundToInt (r.getWidth() * 9.0f / 16.0f) + 12;
            preview.setBounds (r.removeFromTop (ph));
            r.removeFromTop (4);
            auto tr = r.removeFromTop (kRow);
            playBtn->setBounds (tr.removeFromLeft (80)); tr.removeFromLeft (6);
            stopBtn->setBounds (tr.removeFromLeft (68)); tr.removeFromLeft (8);
            timeLabel->setBounds (tr.removeFromRight (96));
            position.setBounds (tr.reduced (0, 2));
            r.removeFromTop (4);
            timeline.setBounds (r.removeFromTop (30));
            r.removeFromTop (kGap);
        }
        for (auto* c : std::initializer_list<juce::Component*> { &preview, playBtn.get(), stopBtn.get(), timeLabel.get(), &position, &timeline })
            c->setVisible (! typing);

        auto tb = r.removeFromTop (kRow);
        const int tw = (tb.getWidth() - (kTabs - 1) * 4) / kTabs;
        for (int i = 0; i < kTabs; ++i) { tabs[i]->setBounds (i == kTabs - 1 ? tb : tb.removeFromLeft (tw)); if (i < kTabs - 1) tb.removeFromLeft (4); }
        r.removeFromTop (kGap);
        pageArea = r;
        layoutPages();
        frameDirty = true;
    }

    void layoutPages()
    {
        auto r = pageArea;
        // Style: random row on top, the list fills the rest
        stylePage.setBounds (r);
        {
            auto s = stylePage.getLocalBounds();
            auto rnd = s.removeFromTop (kRow);
            randomBtn->setBounds (rnd.removeFromLeft (110)); rnd.removeFromLeft (kGap);
            styleLock.setBounds (rnd.removeFromLeft (kRow)); styleLockLbl->setBounds (rnd.removeFromLeft (70));
            colourLock.setBounds (rnd.removeFromLeft (kRow)); colourLockLbl->setBounds (rnd);
            s.removeFromTop (4);
            const bool note = patterns.mode != Patterns::single;
            patternsNote->setVisible (note);
            if (note) { patternsNote->setBounds (s.removeFromTop (34)); }
            blurb->setBounds (s.removeFromTop (38));
            s.removeFromTop (4);
            if (styles.getBounds() != s) { styles.setBounds (s); styles.scrollToEnsureRowIsOnscreen (look.style); }
        }
        for (auto* v : { &patternsView, &lookView, &fxView, &exportView }) v->setBounds (r);
        const int w = juce::jmax (200, r.getWidth() - 12);   // room for the scrollbar
        layoutPatterns (w); layoutLook (w); layoutEffects (w); layoutExport (w);
    }

    void layoutPatterns (int w)
    {
        auto s = juce::Rectangle<int> (0, 0, w, 3000);
        auto seg = [&] (juce::OwnedArray<juce::TextButton>& b)
        {
            auto row = s.removeFromTop (kRow);
            const int bw = (row.getWidth() - (b.size() - 1) * 6) / b.size();
            for (int i = 0; i < b.size(); ++i) { b[i]->setBounds (i == b.size() - 1 ? row : row.removeFromLeft (bw)); row.removeFromLeft (6); }
            s.removeFromTop (kGap);
        };
        modeLbl->setBounds (s.removeFromTop (20));
        seg (modeBtns);
        patternsInfo->setBounds (s.removeFromTop (40)); s.removeFromTop (4);
        {
            auto row = s.removeFromTop (kRow);
            pickBtn->setBounds (row.removeFromLeft ((row.getWidth() - kGap) / 2)); row.removeFromLeft (kGap);
            rerollBtn->setBounds (row);
            s.removeFromTop (kGap);
        }
        whenLbl->setBounds (s.removeFromTop (20));
        seg (whenBtns);
        auto sliderRow = [&] (juce::Label& lbl, juce::Slider& sl)
        {
            lbl.setBounds (s.removeFromTop (20));
            sl.setBounds (s.removeFromTop (kRow - 4));
            s.removeFromTop (6);
        };
        if (patterns.when == Patterns::followSong) { sliderRow (*minLbl, minSlider); sliderRow (*maxLbl, maxSlider); }
        else { everyLbl->setBounds (s.removeFromTop (20)); everyBox.setBounds (s.removeFromTop (kRow)); s.removeFromTop (6); }
        sliderRow (*fadeLbl, fadeSlider);
        cutBtn.setBounds (s.removeFromTop (kRow));
        shuffleBtn.setBounds (s.removeFromTop (kRow));
        patternsPane.setSize (w, s.getY() + 6);
    }

    void layoutLook (int w)
    {
        auto s = juce::Rectangle<int> (0, 0, w, 3000);
        size_t li = 0;
        auto row = [&] (juce::Component& c) { lookLabels[li++]->setBounds (s.removeFromTop (20)); c.setBounds (s.removeFromTop (kRow)); s.removeFromTop (kGap); };
        row (titleEd); row (artistEd);
        {
            auto lab = s.removeFromTop (20); lookLabels[li++]->setBounds (lab);
            auto rr = s.removeFromTop (kRow);
            colourLock2.setBounds (rr.removeFromRight (kRow)); rr.removeFromRight (4);
            tintBox.setBounds (rr);
            s.removeFromTop (kGap);
        }
        row (bgBox); row (textBox);
        row (intensity);
        row (reactBox);
        auto cov = s.removeFromTop (kRow); coverBtn->setBounds (cov.removeFromLeft (cov.getWidth() * 2 / 3 - 4)); cov.removeFromLeft (kGap); clearCoverBtn->setBounds (cov);
        s.removeFromTop (kGap);
        for (auto* t : { &fadesBtn, &vignetteBtn, &progressBtn, &badgeBtn }) t->setBounds (s.removeFromTop (kRow));
        lookPane.setSize (w, s.getY() + 6);
    }

    void layoutEffects (int w)
    {
        auto s = juce::Rectangle<int> (0, 0, w, 3000);
        fxIntro->setBounds (s.removeFromTop (54)); s.removeFromTop (4);
        fxReactLbl->setBounds (s.removeFromTop (20)); fxReactBox.setBounds (s.removeFromTop (kRow)); s.removeFromTop (kGap);
        mirrorBox.setBounds (s.removeFromTop (kRow)); s.removeFromTop (kGap);
        const int colW = (w - kGap) / 2;
        for (int i = 0; i < fxToggles.size(); ++i) fxToggles[i]->setBounds ((i % 2) * (colW + kGap), s.getY() + (i / 2) * kRow, colW, kRow);
        s.removeFromTop (((fxToggles.size() + 1) / 2) * kRow + kGap);
        auto b = s.removeFromTop (kRow);
        fxAllOn->setBounds (b.removeFromLeft (b.getWidth() / 2 - 4)); b.removeFromLeft (kGap); fxAllOff->setBounds (b);
        fxPane.setSize (w, s.getY() + 6);
    }

    void layoutExport (int w)
    {
        auto s = juce::Rectangle<int> (0, 0, w, 3000);
        auto two = [&] (juce::Label& la, juce::Component& a, juce::Label& lb, juce::Component& b)
        {
            auto lab = s.removeFromTop (20); la.setBounds (lab.removeFromLeft (lab.getWidth() / 2)); lb.setBounds (lab.withTrimmedLeft (kGap));
            auto rr = s.removeFromTop (kRow); a.setBounds (rr.removeFromLeft (rr.getWidth() / 2)); b.setBounds (rr.withTrimmedLeft (kGap));
            s.removeFromTop (kGap);
        };
        two (*sizeLbl, sizeBox, *fpsLbl, fpsBox);
        codecLbl->setBounds (s.removeFromTop (20)); codecBox.setBounds (s.removeFromTop (kRow)); s.removeFromTop (kGap);
        keepCoolBtn.setBounds (s.removeFromTop (kRow));
        coolHint->setBounds (s.removeFromTop (36)); s.removeFromTop (4);
        auto ex = s.removeFromTop (kRow + 6); exportBtn->setBounds (ex.removeFromLeft (ex.getWidth() * 2 / 3 - 4)); ex.removeFromLeft (kGap); cancelBtn->setBounds (ex);
        s.removeFromTop (kGap);
        progress.setBounds (s.removeFromTop (24)); s.removeFromTop (6);
        speed->setBounds (s.removeFromTop (40)); s.removeFromTop (4);
        status->setBounds (s.removeFromTop (58)); s.removeFromTop (4);
        shareBtn->setBounds (s.removeFromTop (kRow));
        exportPane.setSize (w, s.getY() + 6);
    }

    void showTab (int t)
    {
        currentTab = t;
        tabs[t]->setToggleState (true, juce::dontSendNotification);
        prefs().setValue ("tab", t);
        stylePage.setVisible (t == tabStyle);
        patternsView.setVisible (t == tabPatterns);
        lookView.setVisible (t == tabLook);
        fxView.setVisible (t == tabEffects);
        exportView.setVisible (t == tabExport);
        layoutPages();
    }

    void setFullscreen (bool on)
    {
        fullscreen = on;
        full.setVisible (on);
        if (on) full.toFront (false);
        frameDirty = true;
    }

    void showPicker (bool on)
    {
        picker.refresh();
        picker.setVisible (on);
        if (on) picker.toFront (false);
    }

    // ================================================================ pages
    void buildStylePage()
    {
        addAndMakeVisible (stylePage);
        stylesModel.all = &all;
        stylesModel.onPick = [this] (int i)
        {
            look.style = i;
            all[(size_t) i]->reset();
            blurb->setText (all[(size_t) i]->blurb(), juce::dontSendNotification);
            savePrefs();
            if (patterns.mode == Patterns::single) patternsChanged();
            frameDirty = true;
        };
        styles.setModel (&stylesModel);
        styles.setRowHeight (kRow + 2);
        styles.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        randomBtn = makeButton ("Random", [this] { randomLook(); });
        styleLockLbl = makeLabel ("Keep style", 12.5f, col::text3, 1);
        colourLockLbl = makeLabel ("Keep colour", 12.5f, col::text3, 1);
        blurb = makeLabel ({}, 13.5f, col::text2);
        blurb->setJustificationType (juce::Justification::topLeft);
        patternsNote = makeLabel ({}, 13, col::text3);
        patternsNote->setJustificationType (juce::Justification::topLeft);
        styleLock.setToggleState (prefs().getBoolValue ("lockStyle", false), juce::dontSendNotification);
        styleLock.onClick = [this] { prefs().setValue ("lockStyle", styleLock.getToggleState()); };
        colourLock.setToggleState (prefs().getBoolValue ("lockColour", false), juce::dontSendNotification);
        colourLock.onClick = [this] { prefs().setValue ("lockColour", colourLock.getToggleState()); colourLock2.setToggleState (colourLock.getToggleState(), juce::dontSendNotification); };
        for (juce::Component* c : std::initializer_list<juce::Component*> { randomBtn.get(), &styleLock, styleLockLbl.get(), &colourLock, colourLockLbl.get(), blurb.get(), patternsNote.get(), &styles })
            stylePage.addAndMakeVisible (c);
        blurb->setText (all[(size_t) look.style]->blurb(), juce::dontSendNotification);
    }

    void buildPatternsPage()
    {
        setupView (patternsView, patternsPane);
        modeLbl = paneLabel ("Patterns", patternsPane);
        whenLbl = paneLabel ("When it switches", patternsPane);
        const auto modes = Patterns::modeNames(), whens = Patterns::whenNames();
        for (int i = 0; i < modes.size(); ++i)
        {
            auto* b = modeBtns.add (new juce::TextButton (modes[i]));
            b->setRadioGroupId (61); b->setClickingTogglesState (true);
            b->onClick = [this, i] { if (modeBtns[i]->getToggleState() && patterns.mode != i) { patterns.mode = i; patternsEdited(); } };
            patternsPane.addAndMakeVisible (b);
        }
        for (int i = 0; i < whens.size(); ++i)
        {
            auto* b = whenBtns.add (new juce::TextButton (whens[i]));
            b->setRadioGroupId (62); b->setClickingTogglesState (true);
            b->onClick = [this, i] { if (whenBtns[i]->getToggleState() && patterns.when != i) { patterns.when = i; patternsEdited(); layoutPages(); } };
            patternsPane.addAndMakeVisible (b);
        }
        patternsInfo = makeLabel ({}, 13, col::text2);
        patternsInfo->setJustificationType (juce::Justification::topLeft);
        patternsPane.addAndMakeVisible (*patternsInfo);
        pickBtn = makeButton ("Pick styles", [this] { showPicker (true); });
        rerollBtn = makeButton ("Re-roll", [this] { patterns.seed = (juce::uint32) juce::Random::getSystemRandom().nextInt() | 1u; patternsEdited(); });
        patternsPane.addAndMakeVisible (*pickBtn); patternsPane.addAndMakeVisible (*rerollBtn);

        auto slider = [this] (juce::Slider& s, double lo, double hi, double step, double v)
        {
            s.setSliderStyle (juce::Slider::LinearHorizontal);
            s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            s.setRange (lo, hi, step);
            s.setValue (v, juce::dontSendNotification);
            s.onValueChange = [this] { readPatternSliders(); };
            s.onDragEnd = [this] { patternsEdited(); };
            patternsPane.addAndMakeVisible (s);
        };
        minLbl = paneLabel ({}, patternsPane); maxLbl = paneLabel ({}, patternsPane); fadeLbl = paneLabel ({}, patternsPane);
        everyLbl = paneLabel ("Switch every", patternsPane);
        slider (minSlider, 2, 60, 1, patterns.minSeconds);
        slider (maxSlider, 4, 120, 1, patterns.maxSeconds);
        slider (fadeSlider, 0.5, 8, 0.5, patterns.fadeSeconds);
        for (auto v : Patterns::everyChoices()) everyBox.addItem (juce::String ((int) v) + " seconds", (int) v);
        styleCombo (everyBox);
        everyBox.onChange = [this] { patterns.everySeconds = everyBox.getSelectedId(); patternsEdited(); };
        patternsPane.addAndMakeVisible (everyBox);
        cutBtn.setButtonText ("Cut on slams (hard cut when it hits)");
        cutBtn.onClick = [this] { patterns.cutOnSlams = cutBtn.getToggleState(); patternsEdited(); };
        shuffleBtn.setButtonText ("Shuffle the picked styles");
        shuffleBtn.onClick = [this] { patterns.shuffle = shuffleBtn.getToggleState(); patternsEdited(); };
        patternsPane.addAndMakeVisible (cutBtn); patternsPane.addAndMakeVisible (shuffleBtn);
    }

    void buildLookPage()
    {
        setupView (lookView, lookPane);
        for (auto* s : { "Title", "Artist", "Colour", "Background", "Text", "Intensity", "Reactions" })
            lookLabels.push_back (paneLabel (s, lookPane));
        for (auto* e : { &titleEd, &artistEd })
        {
            e->setFont (font (16));
            e->setIndents (8, 12);
            e->onTextChange = [this] { look.title = titleEd.getText(); look.artist = artistEd.getText(); savePrefs(); frameDirty = true; };
            e->onReturnKey = [this] { unfocusAllComponents(); };
            lookPane.addAndMakeVisible (*e);
        }
        artistEd.setText (look.artist, false);
        combo (tintBox, tintNames(), look.tint, [this] (int i) { look.tint = i; }, lookPane);
        combo (bgBox, backgroundNames(), look.background, [this] (int i) { look.background = i; }, lookPane);
        combo (textBox, textNames(), look.text, [this] (int i) { look.text = i; }, lookPane);
        combo (reactBox, reactionNames(), look.reactions, [this] (int i) { look.reactions = i; fxReactBox.setSelectedItemIndex (i, juce::dontSendNotification); renderer.reset(); }, lookPane);
        colourLock2.setToggleState (colourLock.getToggleState(), juce::dontSendNotification);
        colourLock2.onClick = [this] { colourLock.setToggleState (colourLock2.getToggleState(), juce::sendNotificationSync); };
        lookPane.addAndMakeVisible (colourLock2);
        intensity.setSliderStyle (juce::Slider::LinearHorizontal);
        intensity.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        intensity.setRange (0.25, 2.0, 0.01);
        intensity.setValue (look.intensity, juce::dontSendNotification);
        intensity.onValueChange = [this] { look.intensity = (float) intensity.getValue(); savePrefs(); frameDirty = true; };
        lookPane.addAndMakeVisible (intensity);
        coverBtn = makeButton ("Cover art", [this] { chooseCover(); });
        clearCoverBtn = makeButton ("Clear", [this] { look.cover = {}; prefs().setValue ("cover", juce::String()); frameDirty = true; });
        lookPane.addAndMakeVisible (*coverBtn); lookPane.addAndMakeVisible (*clearCoverBtn);
        toggle (fadesBtn, "Fade in and out", look.fades, [this] (bool b) { look.fades = b; }, lookPane);
        toggle (vignetteBtn, "Vignette and grain", look.vignette, [this] (bool b) { look.vignette = b; }, lookPane);
        toggle (progressBtn, "Progress line", look.progress, [this] (bool b) { look.progress = b; }, lookPane);
        toggle (badgeBtn, "Cover next to title", look.coverBadge, [this] (bool b) { look.coverBadge = b; }, lookPane);
    }

    void buildEffectsPage()
    {
        setupView (fxView, fxPane);
        fxIntro = makeLabel ("Each effect follows the music while Reactions is on. Switch off the ones you don't want; "
                             "a fixed mirror stays on even with Reactions off.", 13, col::text3);
        fxIntro->setJustificationType (juce::Justification::topLeft);
        fxPane.addAndMakeVisible (*fxIntro);
        fxReactLbl = paneLabel ("Reactions", fxPane);
        combo (fxReactBox, reactionNames(), look.reactions, [this] (int i) { look.reactions = i; reactBox.setSelectedItemIndex (i, juce::dontSendNotification); renderer.reset(); }, fxPane);
        combo (mirrorBox, mirrorNames(), look.mirror, [this] (int i) { look.mirror = i; renderer.reset(); }, fxPane);
        auto names = effectNames();
        for (int i = 0; i < kEffectCount; ++i)
        {
            auto* t = fxToggles.add (new juce::ToggleButton (names[i]));
            t->setToggleState (look.effectOn ((Effect) i), juce::dontSendNotification);
            t->onClick = [this, t, i] { look.setEffect ((Effect) i, t->getToggleState()); savePrefs(); frameDirty = true; };
            fxPane.addAndMakeVisible (t);
        }
        fxAllOn = makeButton ("All on", [this] { setAllEffects (true); });
        fxAllOff = makeButton ("All off", [this] { setAllEffects (false); });
        fxPane.addAndMakeVisible (*fxAllOn); fxPane.addAndMakeVisible (*fxAllOff);
    }

    void buildExportPage()
    {
        setupView (exportView, exportPane);
        sizeLbl = paneLabel ("Size", exportPane); fpsLbl = paneLabel ("Frame rate", exportPane); codecLbl = paneLabel ("Video codec", exportPane);
        combo (sizeBox, { "1080p", "1440p", "4K" }, sizeIndex, [this] (int i) { sizeIndex = i; updateExportInfo(); }, exportPane);
        combo (fpsBox, { "30 fps", "60 fps" }, fps60 ? 1 : 0, [this] (int i) { fps60 = i == 1; updateExportInfo(); }, exportPane);
        combo (codecBox, { "H.264 (plays anywhere)", "HEVC (half the file size)" }, hevc ? 1 : 0, [this] (int i) { hevc = i == 1; updateExportInfo(); }, exportPane);
        keepCoolBtn.setButtonText ("Keep cool (ease off when the phone gets hot)");
        keepCoolBtn.setToggleState (keepCool, juce::dontSendNotification);
        keepCoolBtn.onClick = [this] { keepCool = keepCoolBtn.getToggleState(); if (job) job->keepCool = keepCool; savePrefs(); };
        exportPane.addAndMakeVisible (keepCoolBtn);
        coolHint = makeLabel ({}, 12.5f, col::text3);
        coolHint->setJustificationType (juce::Justification::topLeft);
        exportPane.addAndMakeVisible (*coolHint);
        exportBtn = makeButton ("Export MP4", [this] { startExport(); });
        cancelBtn = makeButton ("Cancel", [this] { if (job) job->signalThreadShouldExit(); });
        cancelBtn->setEnabled (false);
        shareBtn = makeButton ("Share the last video", [this] { shareLast(); });
        shareBtn->setEnabled (false);
        speed = makeLabel ({}, 13, col::text2, 3);
        speed->setJustificationType (juce::Justification::topLeft);
        status = makeLabel ({}, 13, col::text3);
        status->setJustificationType (juce::Justification::topLeft);
        for (juce::Component* c : std::initializer_list<juce::Component*> { exportBtn.get(), cancelBtn.get(), shareBtn.get(), speed.get(), status.get(), &progress })
            exportPane.addAndMakeVisible (c);
        updateExportInfo();
    }

    void updateExportInfo()
    {
        int bps = 0;
        auto rate = bitrateFor (sizeIndex, fps60, bps);
        if (hevc) rate = juce::String ((int) (bps * 0.6 / 1000000)) + " Mbps";
        coolHint->setText ("Long renders at 4K heat the phone up. Keep it plugged in. " + juce::String (hevc ? "HEVC " : "H.264 ") + rate + " at this size.",
                           juce::dontSendNotification);
        if (! job) status->setText ("Videos go to " + outputFolderName() + ". Keep the app open while it renders.", juce::dontSendNotification);
        savePrefs();
    }

    // ================================================================ patterns
    void readPatternSliders()
    {
        patterns.minSeconds = minSlider.getValue();
        patterns.maxSeconds = juce::jmax (maxSlider.getValue(), patterns.minSeconds + 1);
        patterns.fadeSeconds = fadeSlider.getValue();
        minLbl->setText ("Shortest: " + juce::String ((int) patterns.minSeconds) + " s on a style", juce::dontSendNotification);
        maxLbl->setText ("Longest: " + juce::String ((int) patterns.maxSeconds) + " s on a style", juce::dontSendNotification);
        fadeLbl->setText ("Fade: " + juce::String (patterns.fadeSeconds, 1) + " s", juce::dontSendNotification);
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
        shuffleBtn.setToggleState (patterns.shuffle, juce::dontSendNotification);
        shuffleBtn.setEnabled (patterns.mode == Patterns::picked);
        pickBtn->setButtonText ("Pick styles (" + juce::String ((int) patterns.picks.size()) + ")");
        rerollBtn->setEnabled (patterns.mode == Patterns::random || (patterns.mode == Patterns::picked && patterns.shuffle));
        readPatternSliders();
        layoutPages();
    }

    void patternsChanged()
    {
        plan = makePlan (patterns, events, look.style, (int) all.size());
        stylePlayer.reset();
        renderer.reset();
        timeline.plan = &plan;
        timeline.duration = track ? track->seconds() : 0;
        const int switches = (int) plan.steps.size() - 1;
        juce::String info;
        if (patterns.mode == Patterns::single) info = "Single: the style from the Style tab plays all the way through.";
        else if (scanning) info = "Listening to the track for section changes...";
        else if (patterns.mode == Patterns::picked && patterns.picks.size() < 2)
            info = "Pick at least two styles to switch between (" + juce::String ((int) patterns.picks.size()) + " picked).";
        else
            info = (patterns.mode == Patterns::random ? juce::String ("Random from all ") + juce::String ((int) all.size())
                                                      : juce::String ((int) patterns.picks.size()) + " picked" + (patterns.shuffle ? ", shuffled" : ", in order"))
                 + ": " + juce::String (switches) + (switches == 1 ? " switch" : " switches") + " over " + fmtTime (timeline.duration) + ". The strip under the preview shows when.";
        patternsInfo->setText (info, juce::dontSendNotification);
        patternsNote->setText (patterns.mode == Patterns::picked ? "Patterns is on (Picked): the video switches between your picks. Tap Single on Patterns to use only this style."
                                                                  : "Patterns is on (Random): the video switches styles. Tap Single on Patterns to use only this style.",
                               juce::dontSendNotification);
        pickBtn->setButtonText ("Pick styles (" + juce::String ((int) patterns.picks.size()) + ")");
        timeline.note = scanning && patterns.mode != Patterns::single ? "Listening for section changes..." : juce::String();
        timeline.repaint();
        layoutPages();
        frameDirty = true;
    }

    // ================================================================ random look
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
        frameDirty = true;
    }

    void setAllEffects (bool on)
    {
        for (int i = 0; i < kEffectCount; ++i) { look.setEffect ((Effect) i, on); fxToggles[i]->setToggleState (on, juce::dontSendNotification); }
        savePrefs(); frameDirty = true;
    }

    // ================================================================ tracks and covers
    void useDemo()
    {
        auto t = std::make_shared<Track>();
        setDemoTrack (*t, 40);
        setTrack (t, "Demo loop", "Demo loop");
    }

    void setTrack (std::shared_ptr<Track> t, const juce::String& label, const juce::String& title)
    {
        stop();
        transport.setSource (nullptr);
        track = std::move (t);
        source = std::make_unique<juce::MemoryAudioSource> (track->audio, false, false);
        transport.setSource (source.get(), 0, nullptr, Track::rate, 2);
        clockT = 0; analyzer.reset(); renderer.reset(); stylePlayer.reset();
        trackLabel->setText (label + "   " + fmtTime (track->seconds()), juce::dontSendNotification);
        titleEd.setText (title, true);
        look.title = title;
        events = {};
        startScan();
        frameDirty = true;
    }

    void startScan()
    {
        scanning = true;
        const int gen = ++scanGeneration;
        auto t = track;
        juce::Component::SafePointer<MainComponent> safe (this);
        loader.addJob ([t, gen, safe]
        {
            auto ev = std::make_shared<SongEvents> (scanSong (t->audio, Track::rate, [safe, gen] { return safe == nullptr || safe->scanGeneration != gen; }));
            juce::MessageManager::callAsync ([safe, gen, ev]
            {
                if (safe == nullptr || safe->scanGeneration != gen) return;
                safe->events = *ev; safe->scanning = false; safe->patternsChanged();
            });
        });
        patternsChanged();
    }

    void chooseTrack()
    {
        chooser = std::make_unique<juce::FileChooser> ("Open a track", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc) { auto u = fc.getURLResult(); if (! u.isEmpty()) openTrackUrl (u); });
    }

    void openTrackUrl (const juce::URL& url)
    {
        trackLabel->setText ("Loading...", juce::dontSendNotification);
        juce::Component::SafePointer<MainComponent> safe (this);
        loader.addJob ([url, safe]
        {
            juce::String name, err;
            auto file = importDocument (url, "track", name);
            auto t = std::make_shared<Track>();
            const bool ok = file.existsAsFile() && loadTrack (file, *t, err);
            if (! file.existsAsFile()) err = "Couldn't open that file.";
            juce::MessageManager::callAsync ([safe, ok, t, name, err]
            {
                if (safe == nullptr) return;
                if (! ok) { safe->trackLabel->setText (err, juce::dontSendNotification); return; }
                const auto title = name.containsChar ('.') ? name.upToLastOccurrenceOf (".", false, false) : name;
                safe->setTrack (t, name, title);
            });
        });
    }

    void chooseCover()
    {
        chooser = std::make_unique<juce::FileChooser> ("Cover art", juce::File(), "*.png;*.jpg;*.jpeg");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  auto u = fc.getURLResult();
                                  if (u.isEmpty()) return;
                                  juce::String name;
                                  auto f = importDocument (u, "cover", name);
                                  auto img = juce::ImageFileFormat::loadFrom (f);
                                  if (! img.isValid()) { status->setText ("Couldn't read that image.", juce::dontSendNotification); return; }
                                  look.cover = img;
                                  if (look.background == 0) { look.background = 1; bgBox.setSelectedItemIndex (1, juce::dontSendNotification); }
                                  prefs().setValue ("cover", f.getFullPathName());
                                  savePrefs(); frameDirty = true;
                              });
    }

    // ================================================================ playback and frames
    bool haveDevice() { return devices.getCurrentAudioDevice() != nullptr; }

    void togglePlay()
    {
        if (job) return;
        if (playing) { pause(); return; }
        if (clockT >= track->seconds() - 0.05) seek (0);
        playing = true;
        if (haveDevice()) { transport.setPosition (clockT); transport.start(); }
        lastTick = juce::Time::getMillisecondCounterHiRes();
        playBtn->setButtonText ("Pause");
    }
    void pause() { playing = false; transport.stop(); playBtn->setButtonText ("Play"); }
    void stop() { pause(); seek (0); }
    void seek (double t)
    {
        clockT = juce::jlimit (0.0, juce::jmax (0.0, track ? track->seconds() : 0.0), t);
        transport.setPosition (clockT);
        analyzer.reset(); renderer.reset(); stylePlayer.reset();
        analyzerT = -1;
        frameDirty = true;
    }

    void onVBlank()
    {
        if (job || ! track) return;
        if (playing)
        {
            if (haveDevice()) clockT = transport.getCurrentPosition();
            else { auto now = juce::Time::getMillisecondCounterHiRes(); clockT += (now - lastTick) / 1000.0; lastTick = now; }
            if (clockT >= track->seconds()) { pause(); clockT = track->seconds(); }
            frameDirty = true;
        }
        if (! frameDirty) return;
        if (skipNext) { skipNext = false; return; }     // too slow for every refresh: draw every other one
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        tick();
        skipNext = playing && juce::Time::getMillisecondCounterHiRes() - t0 > 7.0;
    }

public:
    void tick()
    {
        frameDirty = false;
        juce::Rectangle<int> area = fullscreen ? full.videoSize() : preview.videoArea();
        if (area.isEmpty() || ! track) return;
        // render near the screen's real pixels, capped so it keeps up at 120 Hz
        float scale = 2.0f;
        if (auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()) scale = (float) d->scale;
        const float cap = fullscreen ? 1600.0f : 1100.0f;
        const float k = juce::jmin (scale, cap / (float) juce::jmax (1, area.getWidth()));
        const auto size = juce::Rectangle<int> (juce::jmax (16, juce::roundToInt (area.getWidth() * k)), juce::jmax (9, juce::roundToInt (area.getHeight() * k)));
        auto& img = fullscreen ? full.frame : preview.frame;
        if (! img.isValid() || img.getBounds() != size)
            img = juce::Image (juce::Image::ARGB, size.getWidth(), size.getHeight(), true, juce::SoftwareImageType());
        double fps = 30;
        if (analyzerT >= 0 && clockT > analyzerT) fps = juce::jlimit (24.0, 120.0, 1.0 / (clockT - analyzerT));
        if (analyzerT != clockT) analyzer.analyse (track->audio, Track::rate, clockT, fps, frame);
        analyzerT = clockT;
        frame.duration = track->seconds();
        frame.index++;
        {
            juce::Graphics g (img);
            auto now = stylePlayer.at (clockT, plan, all);
            if (now.a != nullptr) renderer.render (g, img.getBounds().toFloat(), frame, *now.a, look, now.b, now.mix);
        }
        (fullscreen ? (juce::Component&) full : (juce::Component&) preview).repaint();
        if (! scrubbing && track->seconds() > 0) position.setValue (clockT / track->seconds(), juce::dontSendNotification);
        timeLabel->setText (fmtTime (clockT) + " / " + fmtTime (track->seconds()), juce::dontSendNotification);
        timeline.setPlayhead (clockT);
    }

private:
    void timerCallback() override
    {
        if (job == nullptr) return;
        if (job->finished) { finishExport(); return; }
        const int v = job->thumbVersion;
        if (v != thumbSeen) { thumbSeen = v; preview.frame = job->getThumb(); }
        preview.overlay = "Rendering " + juce::String ((int) (job->progress * 100)) + "%";
        preview.repaint();
        progress.setValue (job->progress, juce::String ((int) (job->progress * 100)) + "%");
        const double fps = job->framesPerSecond, left = job->secondsLeft;
        juce::String s;
        if (fps > 0) s << juce::String (fps, 1) << " fps (" << juce::String (fps / exportFps, 2) << "x real time)   " << (left >= 0 ? fmtTime (left) + " left" : juce::String());
        else s << "Starting...";
        s << "\n" << job->encoderInfo << "   Phone: " << heatName (job->heatNow) << (job->easing ? ", easing off" : "");
        speed->setText (s, juce::dontSendNotification);
    }

    void startExport()
    {
        if (job || ! track) return;
        if (scanning && patterns.mode != Patterns::single) { status->setText ("Still listening to the track for section changes; try again in a moment.", juce::dontSendNotification); return; }
        pause();
        setFullscreen (false);
        static const int sizes[3][2] = { { 1920, 1080 }, { 2560, 1440 }, { 3840, 2160 } };
        const auto name = juce::File::createLegalFileName (look.title.isNotEmpty() ? look.title : "BC Visualizer");
        outputFolder().createDirectory();
        auto out = outputFolder().getNonexistentChildFile (name, ".mp4", false);
        exportFps = fps60 ? 60 : 30;
        exportTrack = track;
        job = std::make_unique<ExportJob> (*exportTrack, look, out, sizes[sizeIndex][0], sizes[sizeIndex][1], exportFps);
        job->plan = plan;
        job->threads = Workers::recommended();
        job->hevc = hevc;
        int bps = 0; bitrateFor (sizeIndex, fps60, bps);
        job->bitrate = bps;
        job->keepCool = keepCool;
        job->heat = [] { return thermalStatus(); };
        job->startThread (juce::Thread::Priority::high);
        juce::Desktop::getInstance().setScreenSaverEnabled (false);    // keep the screen on while it renders
        exportBtn->setEnabled (false); cancelBtn->setEnabled (true); openBtn->setEnabled (false); playBtn->setEnabled (false);
        status->setText ("Rendering " + out.getFileName() + "...", juce::dontSendNotification);
        thumbSeen = -1;
        if (currentTab != tabExport) showTab (tabExport);
    }

    void finishExport()
    {
        job->stopThread (10000);
        const double dur = exportTrack ? exportTrack->seconds() : 0;
        if (job->ok)
        {
            lastVideo = job->output;
            status->setText ("Saved " + job->output.getFileName() + " to " + outputFolderName() + " in " + fmtTime (job->secondsTaken)
                             + (dur > 0 ? " (" + juce::String (dur / juce::jmax (0.1, job->secondsTaken), 2) + "x real time)" : juce::String()) + ". Ready for YouTube.",
                             juce::dontSendNotification);
        }
        else if (job->cancelled) status->setText ("Export cancelled.", juce::dontSendNotification);
        else status->setText ("Export failed: " + job->error, juce::dontSendNotification);
        progress.setValue (job->ok ? 1.0 : 0.0, job->ok ? "Done" : "");
        job.reset();
        exportTrack.reset();
        juce::Desktop::getInstance().setScreenSaverEnabled (true);
        exportBtn->setEnabled (true); cancelBtn->setEnabled (false); openBtn->setEnabled (true); playBtn->setEnabled (true);
        shareBtn->setEnabled (lastVideo.existsAsFile());
        preview.overlay.clear();
        stylePlayer.reset(); renderer.reset(); analyzer.reset(); analyzerT = -1;
        frameDirty = true;
    }

    void shareLast()
    {
        if (! lastVideo.existsAsFile()) return;
        shareBox = juce::ContentSharer::shareFilesScoped ({ juce::URL (lastVideo) }, [this] (bool ok, const juce::String& err)
        {
            if (! ok && err.isNotEmpty()) status->setText ("Sharing didn't work: " + err, juce::dontSendNotification);
        }, this);
    }

    // ================================================================ prefs
    void loadPrefs()
    {
        auto& p = prefs();
        look.style = juce::jlimit (0, (int) all.size() - 1, p.getIntValue ("style", 0));
        look.artist = p.getValue ("artist", "binaryimage");
        look.tint = juce::jlimit (0, tintNames().size() - 1, p.getIntValue ("tint", 0));
        look.background = p.getIntValue ("background", 0);
        look.text = p.getIntValue ("text", 0);
        look.fades = p.getBoolValue ("fades", true);
        look.vignette = p.getBoolValue ("vignette", true);
        look.progress = p.getBoolValue ("progress", false);
        look.coverBadge = p.getBoolValue ("coverBadge", true);
        look.reactions = juce::jlimit (0, 2, p.getIntValue ("reactions", 2));
        look.intensity = (float) p.getDoubleValue ("intensity", 1.0);
        look.mirror = juce::jlimit (0, (int) kMirrorCount - 1, p.getIntValue ("mirror", mirrorAuto));
        look.effectsOff = p.getIntValue ("effectsOff", 0);
        sizeIndex = juce::jlimit (0, 2, p.getIntValue ("size", 0));
        fps60 = p.getBoolValue ("fps60", false);
        hevc = p.getBoolValue ("hevc", false);
        keepCool = p.getBoolValue ("keepCool", true);
        juce::File cov (p.getValue ("cover"));
        if (cov.existsAsFile()) look.cover = juce::ImageFileFormat::loadFrom (cov);

        patterns.mode = juce::jlimit (0, 2, p.getIntValue ("pmode", Patterns::single));
        patterns.picks.clear();
        for (auto& s : juce::StringArray::fromTokens (p.getValue ("picks"), ",", {}))
            if (s.trim().isNotEmpty()) { const int v = s.getIntValue(); if (v >= 0 && v < (int) all.size()) patterns.picks.push_back (v); }
        patterns.shuffle = p.getBoolValue ("shuffle", false);
        patterns.when = juce::jlimit (0, 1, p.getIntValue ("when", Patterns::followSong));
        patterns.minSeconds = p.getDoubleValue ("minS", 8);
        patterns.maxSeconds = p.getDoubleValue ("maxS", 30);
        patterns.everySeconds = p.getDoubleValue ("every", 16);
        patterns.fadeSeconds = p.getDoubleValue ("fade", 2.5);
        patterns.cutOnSlams = p.getBoolValue ("cut", true);
        patterns.seed = (juce::uint32) p.getIntValue ("seed", 1);
    }
    void savePrefs()
    {
        auto& p = prefs();
        p.setValue ("style", look.style); p.setValue ("artist", look.artist); p.setValue ("tint", look.tint);
        p.setValue ("background", look.background); p.setValue ("text", look.text); p.setValue ("fades", look.fades);
        p.setValue ("vignette", look.vignette); p.setValue ("progress", look.progress); p.setValue ("coverBadge", look.coverBadge);
        p.setValue ("reactions", look.reactions); p.setValue ("intensity", (double) look.intensity);
        p.setValue ("mirror", look.mirror); p.setValue ("effectsOff", look.effectsOff);
        p.setValue ("size", sizeIndex); p.setValue ("fps60", fps60); p.setValue ("hevc", hevc); p.setValue ("keepCool", keepCool);
        p.setValue ("pmode", patterns.mode);
        juce::StringArray picks; for (int s : patterns.picks) picks.add (juce::String (s));
        p.setValue ("picks", picks.joinIntoString (","));
        p.setValue ("shuffle", patterns.shuffle); p.setValue ("when", patterns.when);
        p.setValue ("minS", patterns.minSeconds); p.setValue ("maxS", patterns.maxSeconds); p.setValue ("every", patterns.everySeconds);
        p.setValue ("fade", patterns.fadeSeconds); p.setValue ("cut", patterns.cutOnSlams); p.setValue ("seed", (int) patterns.seed);
    }

    // ================================================================ little builders
    void setupView (juce::Viewport& v, juce::Component& pane)
    {
        v.setViewedComponent (&pane, false);
        v.setScrollBarsShown (true, false);
        v.setScrollBarThickness (6);
        v.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::all);
        addChildComponent (v);
    }
    std::unique_ptr<juce::Label> paneLabel (const juce::String& s, juce::Component& parent)
    {
        auto l = makeLabel (s, 12.5f, col::text3, 1);
        parent.addAndMakeVisible (*l);
        return l;
    }
    void combo (juce::ComboBox& c, const juce::StringArray& items, int sel, std::function<void (int)> on, juce::Component& parent)
    {
        c.addItemList (items, 1); styleCombo (c); c.setSelectedItemIndex (sel, juce::dontSendNotification);
        c.onChange = [this, &c, on] { on (c.getSelectedItemIndex()); savePrefs(); frameDirty = true; };
        parent.addAndMakeVisible (c);
    }
    void toggle (juce::ToggleButton& t, const juce::String& s, bool on, std::function<void (bool)> fn, juce::Component& parent)
    {
        t.setButtonText (s); t.setToggleState (on, juce::dontSendNotification);
        t.onClick = [this, &t, fn] { fn (t.getToggleState()); savePrefs(); frameDirty = true; };
        parent.addAndMakeVisible (t);
    }
    void globalFocusChanged (juce::Component*) override
    {
        const bool typing = titleEd.hasKeyboardFocus (false) || artistEd.hasKeyboardFocus (false);
        if (typing != wasTyping) { wasTyping = typing; resized(); }
    }

public:
    // ================================================================ for the self test
    void setTestInsets (juce::BorderSize<int> b) { testInsets = b; resized(); }
    int getTabCount() const { return kTabs; }
    void selectTab (int t) { showTab (t); }
    void openPicker (bool on) { showPicker (on); }
    void openFull (bool on) { setFullscreen (on); }
    void setPatternMode (int m, int when) { patterns.mode = m; patterns.when = when; if (patterns.picks.size() < 3) patterns.picks = { 0, 12, 40, 53 }; patternsEdited(); }
    void typeTitle (bool on) { wasTyping = on; if (on) titleEd.grabKeyboardFocus(); else unfocusAllComponents(); resized(); }
    void scanNow() { ++scanGeneration; events = scanSong (track->audio, Track::rate); scanning = false; patternsChanged(); }
    const Plan& getPlan() const { return plan; }
    Look& getLook() { return look; }
    Patterns& getPatterns() { return patterns; }
    std::shared_ptr<Track> getTrack() { return track; }
    void showTime (double t) { for (double x = 0; x <= t; x += 1.0 / 30) { clockT = x; tick(); } }

private:
    std::vector<std::unique_ptr<Viz>> all;
    std::unique_ptr<Workers> workers;
    Look look;
    Patterns patterns;
    std::shared_ptr<Track> track, exportTrack;
    SongEvents events;
    Plan plan;
    Renderer renderer;
    Analyzer analyzer;
    StylePlayer stylePlayer;
    Frame frame;
    int sizeIndex = 0, currentTab = 0, exportFps = 30, thumbSeen = -1;
    bool fps60 = false, hevc = false, keepCool = true;
    bool playing = false, scrubbing = false, frameDirty = true, fullscreen = false, skipNext = false, scanning = false, wasTyping = false;
    std::atomic<int> scanGeneration { 0 };
    double clockT = 0, lastTick = 0, analyzerT = -1;
    std::optional<juce::BorderSize<int>> testInsets;
    juce::Rectangle<int> pageArea;
    juce::File lastVideo;

    // top
    std::unique_ptr<juce::Label> appTitle, trackLabel, timeLabel;
    std::unique_ptr<juce::TextButton> openBtn, playBtn, stopBtn;
    PreviewView preview;
    FullView full;
    juce::Slider position;
    Timeline timeline;
    juce::OwnedArray<juce::TextButton> tabs;
    // style
    juce::Component stylePage;
    StyleList stylesModel; juce::ListBox styles;
    std::unique_ptr<juce::TextButton> randomBtn;
    LockToggle styleLock { "style" }, colourLock { "colour" }, colourLock2 { "colour" };
    std::unique_ptr<juce::Label> styleLockLbl, colourLockLbl, blurb, patternsNote;
    // patterns
    juce::Viewport patternsView; juce::Component patternsPane;
    juce::OwnedArray<juce::TextButton> modeBtns, whenBtns;
    std::unique_ptr<juce::Label> modeLbl, whenLbl, patternsInfo, minLbl, maxLbl, fadeLbl, everyLbl;
    std::unique_ptr<juce::TextButton> pickBtn, rerollBtn;
    juce::Slider minSlider, maxSlider, fadeSlider;
    juce::ComboBox everyBox;
    juce::ToggleButton cutBtn, shuffleBtn;
    PickerPanel picker;
    // look
    juce::Viewport lookView; juce::Component lookPane;
    std::vector<std::unique_ptr<juce::Label>> lookLabels;
    juce::TextEditor titleEd, artistEd;
    juce::ComboBox tintBox, bgBox, textBox, reactBox;
    juce::Slider intensity;
    std::unique_ptr<juce::TextButton> coverBtn, clearCoverBtn;
    juce::ToggleButton fadesBtn, vignetteBtn, progressBtn, badgeBtn;
    // effects
    juce::Viewport fxView; juce::Component fxPane;
    std::unique_ptr<juce::Label> fxIntro, fxReactLbl;
    juce::ComboBox fxReactBox, mirrorBox;
    juce::OwnedArray<juce::ToggleButton> fxToggles;
    std::unique_ptr<juce::TextButton> fxAllOn, fxAllOff;
    // export
    juce::Viewport exportView; juce::Component exportPane;
    std::unique_ptr<juce::Label> sizeLbl, fpsLbl, codecLbl, coolHint, speed, status;
    juce::ComboBox sizeBox, fpsBox, codecBox;
    juce::ToggleButton keepCoolBtn;
    std::unique_ptr<juce::TextButton> exportBtn, cancelBtn, shareBtn;
    ProgressPill progress;
    juce::ScopedMessageBox shareBox;

    std::unique_ptr<juce::FileChooser> chooser;
    juce::ThreadPool loader { 1 };
    juce::AudioDeviceManager devices;
    juce::AudioSourcePlayer player;
    juce::AudioTransportSource transport;
    std::unique_ptr<juce::MemoryAudioSource> source;
    std::unique_ptr<ExportJob> job;
    juce::VBlankAttachment vblank { this, [this] { onVBlank(); } };
};

// ------------------------------------------------------------ self test (CI: Linux build and the Android emulator)

static void writePng (const juce::Image& img, const juce::File& f)
{
    f.deleteFile();
    juce::FileOutputStream os (f);
    juce::PNGImageFormat().writeImageToStream (img, os);
}

/** controls too small to hit with a finger */
static void findSmallTargets (juce::Component& c, juce::StringArray& out)
{
    for (auto* ch : c.getChildren())
    {
        if (! ch->isVisible() || dynamic_cast<juce::ListBox*> (ch) != nullptr) continue;
        const bool button = dynamic_cast<juce::Button*> (ch) != nullptr, box = dynamic_cast<juce::ComboBox*> (ch) != nullptr || dynamic_cast<juce::TextEditor*> (ch) != nullptr;
        const bool slider = dynamic_cast<juce::Slider*> (ch) != nullptr;
        if ((button || box) && ch->getHeight() < 40) out.add ("\"" + ch->getName() + (dynamic_cast<juce::Button*> (ch) ? ((juce::Button*) ch)->getButtonText() : juce::String()) + "\": too small to tap (" + juce::String (ch->getHeight()) + " px tall)");
        else if (slider && ch->getHeight() < 30) out.add ("slider too thin to drag (" + juce::String (ch->getHeight()) + " px)");
        else if (! button && ! box && ! slider) findSmallTargets (*ch, out);
    }
}

/** Every tab, the pattern modes, the picker, typing and full screen: screenshots plus the button check. */
static int layoutCheck (MainComponent& mc, const juce::File& dir, const juce::String& sizeName, juce::StringArray& report)
{
    int problems = 0;
    struct State { const char* name; int tab, mode, when; bool picker, typing, fullscreen; };
    const State states[] = {
        { "style",            0, Patterns::single, 0, false, false, false },
        { "patterns-single",  1, Patterns::single, 0, false, false, false },
        { "patterns-random",  1, Patterns::random, 0, false, false, false },
        { "patterns-timer",   1, Patterns::picked, 1, false, false, false },
        { "style-patterns-on",0, Patterns::picked, 1, false, false, false },
        { "picker",           1, Patterns::picked, 0, true,  false, false },
        { "look",             2, Patterns::random, 0, false, false, false },
        { "look-typing",      2, Patterns::random, 0, false, true,  false },
        { "effects",          3, Patterns::random, 0, false, false, false },
        { "export",           4, Patterns::random, 0, false, false, false },
        { "fullscreen",       0, Patterns::random, 0, false, false, true  },
    };
    for (auto& st : states)
    {
        mc.setPatternMode (st.mode, st.when);
        mc.selectTab (st.tab);
        mc.openPicker (st.picker);
        mc.typeTitle (st.typing);
        mc.openFull (st.fullscreen);
        mc.showTime (2.0);
        auto found = st.fullscreen ? juce::StringArray() : findLayoutProblems (mc);
        if (! st.fullscreen) findSmallTargets (mc, found);
        problems += found.size();
        report.add ((found.isEmpty() ? "PASS  " : "FAIL  ") + sizeName + "  " + st.name + "   (" + juce::String (listControls (mc).size()) + " controls)");
        for (auto& f : found) report.add ("        " + f);
        writePng (mc.createComponentSnapshot (mc.getLocalBounds(), true, 2.0f), dir.getChildFile ("phone-" + sizeName + "-" + st.name + ".png"));
        mc.openPicker (false); mc.typeTitle (false); mc.openFull (false);
    }
    return problems;
}

static juce::String describePlan (const Plan& p, const std::vector<std::unique_ptr<Viz>>& all)
{
    juce::String s;
    for (auto& st : p.steps)
        s << "  " << fmtTime (st.fadeStart) << (st.fade > 0 ? "  fade " + juce::String (st.fade, 1) + " s" : juce::String ("  cut       ")) << "  -> "
          << (st.style + 1) << " " << all[(size_t) st.style]->name() << "\n";
    return s;
}

/** Exports with and without patterns, H.264 and HEVC; returns failures. Each line also says how fast it went. */
static int exportTests (const juce::File& dir, juce::StringArray& report, bool full)
{
    int failures = 0;
    auto all = makeAll();
    Track demo; setDemoTrack (demo, 24);
    auto events = scanSong (demo.audio, Track::rate);
    report.add ("song scan: " + juce::String ((int) events.events.size()) + " events in " + fmtTime (events.duration));
    struct Run { const char* name; int w, h, fps; bool hevc; int mode, when; double every; int tint; bool required = true; };
    std::vector<Run> runs {
        { "patterns-random-640x360",   640,  360, 30, false, Patterns::random, Patterns::timer, 4, 6 },
        { "patterns-follow-1280x720", 1280,  720, 30, false, Patterns::random, Patterns::followSong, 16, 9 },
        { "single-1080p30",           1920, 1080, 30, false, Patterns::single, 0, 16, 0 },
        { "hevc-1280x720-60",         1280,  720, 60, true,  Patterns::picked, Patterns::timer, 4, 14 },
    };
    // 4K: the phone's hardware encoder does it; the emulator's software one may not, so it only reports there
    if (full) runs.push_back ({ "4k30",  3840, 2160, 30, false, Patterns::random, Patterns::timer, 4, 6, false });
    for (auto& r : runs)
    {
        Patterns p; p.mode = r.mode; p.when = r.when; p.everySeconds = r.every; p.minSeconds = 3; p.maxSeconds = 8; p.fadeSeconds = 1.5;
        p.picks = { 3, 20, 48, 60 }; p.seed = 7;
        Look look; look.tint = r.tint; look.title = r.name;
        const int seconds = r.w >= 3840 ? 3 : 8;
        Track t; t.audio.setSize (2, (int) (Track::rate * seconds)); for (int ch = 0; ch < 2; ++ch) t.audio.copyFrom (ch, 0, demo.audio, ch, (int) (Track::rate * 6), t.audio.getNumSamples());
        auto ev = scanSong (t.audio, Track::rate);
        auto out = dir.getChildFile (juce::String ("test-") + r.name + ".mp4");
        ExportJob job (t, look, out, r.w, r.h, r.fps);
        job.plan = makePlan (p, ev, 5, (int) all.size());
        job.threads = Workers::recommended();
        job.hevc = r.hevc;
        const bool ok = job.runNow() && out.getSize() > 20000;
        const double frames = seconds * (double) r.fps;
        report.add (juce::String (ok ? "PASS  " : (r.required ? "FAIL  " : "INFO  ")) + r.name + ": " + juce::String (out.getSize() / 1024) + " KB, " + juce::String (job.secondsTaken, 1) + " s ("
                    + juce::String (frames / juce::jmax (0.01, job.secondsTaken), 1) + " fps), " + job.encoderInfo + ", " + juce::String ((int) job.plan.steps.size()) + " styles"
                    + (ok ? juce::String() : "  error: " + job.error));
        if (! ok && r.required) ++failures;
    }
    // the cores must draw exactly what one core draws (noise, glitches, trails and fades included)
    {
        Track t; setDemoTrack (t, 6);
        auto renderSeq = [&] (int threads)
        {
            auto styles = makeAll();
            Workers w (threads); Renderer rr; rr.setWorkers (threads > 1 ? &w : nullptr);
            Analyzer an; Frame fr; Look look; look.tint = 6;
            juce::Image img (juce::Image::ARGB, 640, 360, true, juce::SoftwareImageType());
            for (int f = 0; f < 45; ++f)
            {
                an.analyse (t.audio, Track::rate, f / 30.0, 30, fr); fr.index = f; fr.duration = 6;
                juce::Graphics g (img);
                rr.render (g, img.getBounds().toFloat(), fr, *styles[12], look, f > 20 ? styles[44].get() : nullptr, juce::jlimit (0.0f, 1.0f, (f - 20) / 20.0f));
            }
            return img;
        };
        const int n = juce::jmax (2, Workers::recommended());
        auto a = renderSeq (1), b = renderSeq (n);
        juce::Image::BitmapData da (a, juce::Image::BitmapData::readOnly), db (b, juce::Image::BitmapData::readOnly);
        int diff = 0;
        for (int y = 0; y < a.getHeight(); ++y) diff += std::memcmp (da.getLinePointer (y), db.getLinePointer (y), (size_t) a.getWidth() * 4) != 0 ? 1 : 0;
        report.add (juce::String (diff == 0 ? "PASS  " : "FAIL  ") + "1 core and " + juce::String (n) + " cores draw the same picture (" + juce::String (diff) + " rows differ)");
        if (diff != 0) ++failures;
    }
    // a real track: where it hears the sections change, and the plan that makes
    if (auto path = juce::SystemStats::getEnvironmentVariable ("BCV_TEST_AUDIO", {}); path.isNotEmpty())
    {
        Track t; juce::String err;
        if (loadTrack (juce::File (path), t, err))
        {
            auto ev = scanSong (t.audio, Track::rate);
            juce::String s = juce::File (path).getFileName() + ": " + juce::String ((int) ev.events.size()) + " events:";
            for (auto& e : ev.events) s << " " << juce::String (e.t, 1) << (e.slam ? "(slam)" : "");
            report.add (s);
            Patterns p; p.mode = Patterns::random; p.minSeconds = 4; p.maxSeconds = 12; p.fadeSeconds = 2; p.seed = 3;
            report.add ("plan (random, follow the song, 4-12 s, cut on slams):\n" + describePlan (makePlan (p, ev, 0, (int) all.size()), all));
        }
        else report.add ("couldn't load " + path + ": " + err);
    }
    // speed: one 1080p frame on one core vs all of them, and during a crossfade
    {
        Track t; setDemoTrack (t, 6);
        auto bench = [&] (int threads, bool fade)
        {
            Workers w (threads); Renderer rr; rr.setWorkers (threads > 1 ? &w : nullptr);
            Analyzer an; Frame fr; Look look; look.tint = 9;
            juce::Image img (juce::Image::ARGB, 1920, 1080, true, juce::SoftwareImageType());
            double ms = 0;
            for (int f = 0; f < 40; ++f)
            {
                an.analyse (t.audio, Track::rate, 1 + f / 30.0, 30, fr); fr.index = f; fr.duration = 6;
                auto t0 = juce::Time::getMillisecondCounterHiRes();
                { juce::Graphics g (img); rr.render (g, img.getBounds().toFloat(), fr, *all[30], look, fade ? all[50].get() : nullptr, 0.5f); }
                if (f >= 10) ms += juce::Time::getMillisecondCounterHiRes() - t0;
            }
            return ms / 30.0;
        };
        const int n = Workers::recommended();
        report.add ("speed at 1080p: 1 core " + juce::String (bench (1, false), 1) + " ms/frame, " + juce::String (n) + " cores " + juce::String (bench (n, false), 1)
                    + " ms, crossfade on " + juce::String (n) + " cores " + juce::String (bench (n, true), 1) + " ms");
    }
    return failures;
}

static int runSelfTest (const juce::File& dir, MainComponent* live)
{
    dir.createDirectory();
    juce::StringArray report, layout;
    int problems = 0;
    if (live != nullptr)
    {
        // on the phone: the real window size and the real system bars
        problems += layoutCheck (*live, dir, juce::String (live->getWidth()) + "x" + juce::String (live->getHeight()), layout);
    }
    else
    {
        MainComponent mc;
        mc.scanNow();
        struct Size { const char* name; int w, h, top, bottom; };
        const Size sizes[] = { { "s26ultra", 412, 892, 36, 24 }, { "small", 360, 760, 28, 48 }, { "big", 480, 1040, 40, 20 } };
        for (auto& s : sizes)
        {
            mc.setSize (s.w, s.h);
            mc.setTestInsets ({ s.top, 0, s.bottom, 0 });
            problems += layoutCheck (mc, dir, s.name, layout);
        }
        // patterns follow the demo's sections
        mc.scanNow();
        mc.setPatternMode (Patterns::random, Patterns::followSong);
        auto all = makeAll();
        report.add ("plan for the 40 s demo (random, follow the song):\n" + describePlan (mc.getPlan(), all));
        mc.setPatternMode (Patterns::picked, Patterns::timer);
        report.add ("plan (picked, every " + juce::String ((int) mc.getPatterns().everySeconds) + " s):\n" + describePlan (mc.getPlan(), all));
    }
    layout.insert (0, juce::String (problems) + " problems");
    dir.getChildFile ("layout-check.txt").replaceWithText (layout.joinIntoString ("\n") + "\n");
    const int failures = exportTests (dir, report, live != nullptr);
    report.insert (0, juce::String (failures) + " export failures, " + juce::String (problems) + " layout problems, " + juce::String (Workers::recommended()) + " cores");
    dir.getChildFile ("selftest.txt").replaceWithText (report.joinIntoString ("\n") + "\n");
    return (problems == 0 ? 0 : 3) + (failures == 0 ? 0 : 4);
}

// ------------------------------------------------------------ window and app

class MainWindow : public juce::DocumentWindow
{
public:
    MainWindow() : juce::DocumentWindow ("BC Visualizer", col::bg, juce::DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new MainComponent(), true);
       #if JUCE_ANDROID || JUCE_IOS
        setFullScreen (true);
       #else
        setResizable (true, false);
        centreWithSize (412, 892);
       #endif
        setVisible (true);
    }
    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    MainComponent* main() { return dynamic_cast<MainComponent*> (getContentComponent()); }
};

class App : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "BC Visualizer"; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String& cmd) override
    {
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        auto args = juce::StringArray::fromTokens (cmd, true);
        const int st = args.indexOf ("--selftest");
        if (st >= 0 && st + 1 < args.size())
        {
            setApplicationReturnValue (runSelfTest (juce::File::getCurrentWorkingDirectory().getChildFile (args[st + 1].unquoted()), nullptr));
            quit();
            return;
        }
        window = std::make_unique<MainWindow>();
       #if JUCE_ANDROID
        // the emulator test in CI sets this, then reads the results from Download/BC Visualizer selftest
        char prop[PROP_VALUE_MAX] = {};
        if (__system_property_get ("debug.bcv.selftest", prop) > 0 && juce::String (prop) == "1")
        {
            juce::Timer::callAfterDelay (2500, [this]
            {
                juce::File dir ("/storage/emulated/0/Download/BC Visualizer selftest");
                const int r = runSelfTest (dir, window ? window->main() : nullptr);
                dir.getChildFile ("done.txt").replaceWithText ("exit " + juce::String (r) + "\n");
            });
        }
       #endif
    }
    void shutdown() override { window.reset(); juce::LookAndFeel::setDefaultLookAndFeel (nullptr); }
    void systemRequestedQuit() override { quit(); }

private:
    tk::Look lookAndFeel;
    std::unique_ptr<MainWindow> window;
};
}

START_JUCE_APPLICATION (bcv::App)
