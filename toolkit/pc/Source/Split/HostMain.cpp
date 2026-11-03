// BC Split Host: owns the real interface (the Model 12's ASIO driver) and shares it with every app
// that picks "BC Split" as its ASIO device. Starts by itself when the first app opens Split, sits in
// the tray, and quits a little while after the last app lets go.
#include "Common/JuceHeader.h"
#include "Common/Look.h"
#include "Common/Widgets.h"
#include "SplitCore.h"
#include "SplitTestClock.h"
#include <map>

namespace tk::splithost
{
static juce::PropertiesFile& settings()
{
    static std::unique_ptr<juce::PropertiesFile> f = [] {
        juce::PropertiesFile::Options o;
        o.applicationName = "split";
        o.filenameSuffix = ".xml";
        o.folderName = "binaryimage Toolkit";
        o.osxLibrarySubFolder = "Application Support";
        return std::make_unique<juce::PropertiesFile> (o);
    }();
    return *f;
}

// ============================================================ engine

class Engine : private juce::AudioIODeviceCallback
{
public:
    explicit Engine (bool testMode) : test (testMode), clock (core) {}
    ~Engine() { close(); core.destroy(); }

    bool start()
    {
        if (! core.create()) return false;
        if (test) { clock.start (48000, 256); return true; }
        asio.reset (juce::AudioIODeviceType::createAudioIODeviceType_ASIO());
        if (asio == nullptr) { core.setState (split::hostError, "ASIO isn't available on this computer."); return true; }
        asio->scanForDevices();
        openSaved();
        return true;
    }

    juce::StringArray deviceNames() const
    {
        juce::StringArray out;
        if (test) { out.add ("Split test clock"); return out; }
        if (asio == nullptr) return out;
        for (auto& n : asio->getDeviceNames()) if (n != split::kDriverName) out.add (n);
        return out;
    }

    juce::String preferredDevice() const
    {
        auto names = deviceNames();
        auto saved = settings().getValue ("device");
        if (names.contains (saved)) return saved;
        for (auto& n : names) if (n.containsIgnoreCase ("TASCAM") || n.containsIgnoreCase ("Model 12")) return n;
        for (auto& n : names)
            if (! (n.containsIgnoreCase ("ASIO4ALL") || n.containsIgnoreCase ("Flex") || n.containsIgnoreCase ("Realtek")
                   || n.containsIgnoreCase ("Voicemeeter") || n.containsIgnoreCase ("Generic")))
                return n;
        return names[0];
    }

    void openSaved() { open (preferredDevice(), settings().getDoubleValue ("rate", 0), settings().getIntValue ("buffer", 0)); }

    /** open the interface with every input and output on; rate / buffer 0 = the driver's own choice */
    void open (const juce::String& name, double rate, int buffer)
    {
        if (test) { clock.stop(); clock.start (rate > 0 ? rate : 48000, buffer > 0 ? buffer : 256); return; }
        close();
        if (name.isEmpty()) { core.setState (split::hostError, "No ASIO interface found. Install your interface's ASIO driver (TASCAM Model 12)."); return; }
        dev.reset (asio->createDevice (name, name));
        if (dev == nullptr) { core.setState (split::hostError, "Couldn't load the " + name.toStdString() + " driver."); return; }
        juce::BigInteger ins, outs;
        ins.setRange (0, juce::jmin (split::kMaxCh, dev->getInputChannelNames().size()), true);
        outs.setRange (0, juce::jmin (split::kMaxCh, dev->getOutputChannelNames().size()), true);
        if (rate <= 0) rate = dev->getCurrentSampleRate() > 0 ? dev->getCurrentSampleRate() : 48000;
        if (buffer <= 0) buffer = dev->getDefaultBufferSize();
        buffer = juce::jmin (buffer, split::kMaxFrames);
        auto err = dev->open (ins, outs, rate, buffer);
        if (err.isNotEmpty())
        {
            core.setState (split::hostError, (name + " wouldn't open: " + err + ". If an app is using it directly, point that app at BC Split instead.").toStdString());
            dev.reset();
            return;
        }
        settings().setValue ("device", name);
        settings().setValue ("rate", rate);
        settings().setValue ("buffer", buffer);
        dev->start (this);
    }

    void close()
    {
        if (dev != nullptr) { dev->stop(); dev->close(); dev.reset(); }
    }

    /** a few times a second on the message thread */
    void housekeeping()
    {
        core.housekeeping();
        int who = -1;
        double r = core.pendingRateRequest (who);
        if (r > 0)
        {
            if (core.attachedCount() <= 1) open (currentName(), r, currentBuffer());
            core.clearRateRequest();
        }
    }

    juce::String currentName() const { return test ? juce::String ("Split test clock") : dev != nullptr ? dev->getName() : juce::String(); }
    double currentRate() const { auto* sh = core.shared(); return sh ? sh->sampleRate : 0; }
    int currentBuffer() const { auto* sh = core.shared(); return sh ? sh->bufferSize : 0; }
    juce::Array<double> rates() const { return dev ? dev->getAvailableSampleRates() : juce::Array<double> { 44100.0, 48000.0, 88200.0, 96000.0 }; }
    juce::Array<int> buffers() const { return dev ? dev->getAvailableBufferSizes() : juce::Array<int> { 64, 128, 256, 512, 1024 }; }
    bool hasPanel() const { return dev != nullptr && dev->hasControlPanel(); }
    void showPanel() { if (dev != nullptr && dev->showControlPanel()) open (currentName(), currentRate(), currentBuffer()); }

    split::HostCore core;

private:
    void audioDeviceAboutToStart (juce::AudioIODevice* d) override
    {
        std::vector<std::string> ins, outs;
        auto inN = d->getInputChannelNames(), outN = d->getOutputChannelNames();
        auto activeIn = d->getActiveInputChannels(), activeOut = d->getActiveOutputChannels();
        for (int i = 0; i < inN.size() && (int) ins.size() < split::kMaxCh; ++i) if (activeIn[i]) ins.push_back (inN[i].toStdString());
        for (int i = 0; i < outN.size() && (int) outs.size() < split::kMaxCh; ++i) if (activeOut[i]) outs.push_back (outN[i].toStdString());
        core.publish (d->getCurrentSampleRate(), d->getCurrentBufferSizeSamples(), ins, outs,
                      d->getInputLatencyInSamples(), d->getOutputLatencyInSamples(), d->getName().toStdString());
    }

    void audioDeviceIOCallbackWithContext (const float* const* in, int nin, float* const* out, int nout, int n,
                                           const juce::AudioIODeviceCallbackContext&) override
    {
        core.process (in, nin, out, nout, n);
    }

    void audioDeviceStopped() override { core.setState (split::hostReconfiguring); }
    void audioDeviceError (const juce::String& e) override { core.setState (split::hostError, e.toStdString()); }

    bool test;
    split::TestClock clock;
    std::unique_ptr<juce::AudioIODeviceType> asio;
    std::unique_ptr<juce::AudioIODevice> dev;
};

// ============================================================ window

class Meters : public Screen
{
public:
    explicit Meters (split::HostCore& c) : core (c) {}
    void paintScreen (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        auto* sh = core.shared();
        if (sh == nullptr) return;
        const int n = juce::jmax (1, (int) sh->numIns);
        const float w = r.getWidth() / n;
        for (int c = 0; c < sh->numIns; ++c)
        {
            auto cell = juce::Rectangle<float> (r.getX() + c * w, r.getY(), w, r.getHeight()).reduced (3, 0);
            auto bar = cell.withTrimmedBottom (34);
            const float db = juce::Decibels::gainToDecibels (sh->inPeak[c], -60.0f);
            const float lvl = juce::jlimit (0.0f, 1.0f, (db + 60) / 60);
            const int segs = 24;
            for (int s = 0; s < segs; ++s)
            {
                auto seg = juce::Rectangle<float> (bar.getX() + 2, bar.getBottom() - (s + 1) * bar.getHeight() / segs + 1, bar.getWidth() - 4, bar.getHeight() / segs - 2);
                g.setColour (s < lvl * segs ? (s > segs - 3 ? col::white : juce::Colour (0xffd8d8d8)) : juce::Colour (0xff1a1a1a));
                g.fillRect (seg);
            }
            g.setColour (col::text2); g.setFont (font (10.5f));
            juce::String nm (sh->inNames[c]);
            g.drawFittedText (nm.isEmpty() ? juce::String (c + 1) : nm, cell.removeFromBottom (32).toNearestInt(), juce::Justification::centredTop, 2, 0.6f);
        }
    }
    split::HostCore& core;
};

class HostComponent : public juce::Component, private juce::Timer
{
public:
    explicit HostComponent (Engine& e) : engine (e), meters (e.core)
    {
        for (auto* s : { &top, &apps, &ins }) addAndMakeVisible (s);
        top.setTitle ("Interface"); apps.setTitle ("Apps using Split"); ins.setTitle ("Inputs");
        styleCombo (device); styleCombo (rate); styleCombo (buffer);
        for (auto* c : { &device, &rate, &buffer }) top.addAndMakeVisible (c);
        device.onChange = [this] { if (! updating) engine.open (device.getText(), engine.currentRate(), engine.currentBuffer()); refresh(); };
        rate.onChange = [this] { if (! updating) engine.open (engine.currentName(), rate.getText().getDoubleValue(), engine.currentBuffer()); refresh(); };
        buffer.onChange = [this] { if (! updating) engine.open (engine.currentName(), engine.currentRate(), buffer.getText().getIntValue()); refresh(); };
        panelBtn = makeButton ("Interface panel", [this] { engine.showPanel(); refresh(); });
        retryBtn = makeButton ("Try again", [this] { engine.openSaved(); refresh(); });
        // after changing the buffer in the interface's own panel: reopen it at whatever the driver now wants
        reloadBtn = makeButton ("Reload", [this] { engine.open (engine.currentName(), engine.currentRate(), 0); refresh(); });
        top.addAndMakeVisible (*panelBtn); top.addAndMakeVisible (*retryBtn); top.addAndMakeVisible (*reloadBtn);
        warn = makeLabel ("", 13.5f, col::white, 2);
        warn->setJustificationType (juce::Justification::topLeft);
        apps.addAndMakeVisible (*warn);
        for (auto* l : { "Real interface", "Sample rate", "Buffer" }) { auto lb = makeLabel (l, 12, col::text3); top.addAndMakeVisible (*lb); lbls.push_back (std::move (lb)); }
        status = makeLabel ("", 14, col::text, 1);
        status->setJustificationType (juce::Justification::topLeft);
        top.addAndMakeVisible (*status);
        appList = makeLabel ("", 14, col::text);
        appList->setJustificationType (juce::Justification::topLeft);
        apps.addAndMakeVisible (*appList);
        help = makeLabel ("Pick \"BC Split\" as the ASIO device in Ableton, Toolkit and Rocksmith (RS_ASIO.ini: Driver=BC Split). "
                          "Split keeps the interface open and shares it, so they can all run at once. Their outputs go out two buffers "
                          "later than they would direct (about 11 ms at 256), and Split reports that so recordings still line up.", 12, col::text3);
        help->setJustificationType (juce::Justification::topLeft);
        apps.addAndMakeVisible (*help);
        ins.addAndMakeVisible (meters);
        refresh();
        startTimerHz (15);
    }

    void refresh()
    {
        updating = true;
        device.clear (juce::dontSendNotification);
        auto names = engine.deviceNames();
        device.addItemList (names, 1);
        device.setSelectedId (names.indexOf (engine.currentName()) + 1, juce::dontSendNotification);
        rate.clear (juce::dontSendNotification);
        int i = 1;
        for (auto r : engine.rates()) { rate.addItem (juce::String ((int) r) + " Hz", i); if (std::abs (r - engine.currentRate()) < 1) rate.setSelectedId (i, juce::dontSendNotification); ++i; }
        buffer.clear (juce::dontSendNotification);
        i = 1;
        for (auto b : engine.buffers())
        {
            if (b > split::kMaxFrames) continue;
            buffer.addItem (juce::String (b) + " samples", i);
            if (b == engine.currentBuffer()) buffer.setSelectedId (i, juce::dontSendNotification);
            ++i;
        }
        panelBtn->setEnabled (engine.hasPanel());
        updating = false;
        timerCallback();
    }

    void timerCallback() override
    {
        auto* sh = engine.core.shared();
        if (sh == nullptr) return;
        juce::String s;
        switch (sh->hostState.load())
        {
            case split::hostRunning:
                s << "Running on " << sh->deviceName << "\n" << juce::String (sh->sampleRate / 1000, 1) << " kHz   " << sh->bufferSize << " samples ("
                  << juce::String (sh->bufferSize * 1000.0 / juce::jmax (1.0, sh->sampleRate), 1) << " ms)   " << sh->numIns << " in / " << sh->numOuts << " out";
                break;
            case split::hostError: s << "Not running: " << sh->error; break;
            case split::hostReconfiguring: s << "Switching..."; break;
            default: s << "Starting..."; break;
        }
        status->setText (s, juce::dontSendNotification);
        retryBtn->setVisible (sh->hostState.load() == split::hostError);
        juce::String a;
        auto cl = engine.core.clients();
        // anyone falling behind in the last few seconds? (each late block is a short gap in that app's sound)
        const auto now = juce::Time::getMillisecondCounter();
        juce::StringArray behind;
        for (auto& c : cl)
        {
            auto& h = history[c.index];
            if (h.name != juce::String (c.name) || c.late < h.late) h = { juce::String (c.name), c.late, now, 0 };
            if (now - h.since > 4000) { h.rate = (double) (c.late - h.late) * 60000.0 / (now - h.since); h.late = c.late; h.since = now; }
            if (h.rate > 3) behind.add (juce::String (c.name));
        }
        warn->setText (behind.isEmpty() ? juce::String()
                       : behind.joinIntoString (", ") + (behind.size() == 1 ? " is" : " are") + " falling behind (you'd hear ticks). Raise the buffer in Interface panel, then press Reload.",
                       juce::dontSendNotification);
        if (cl.empty()) a << "Nothing yet. Open Ableton, Toolkit or Rocksmith with BC Split as the ASIO device.";
        for (auto& c : cl)
            a << (c.running ? "PLAYING   " : "idle         ") << juce::String (c.name) << (c.late > 0 ? "   (" + juce::String ((int) c.late) + " late blocks)" : juce::String()) << "\n";
        appList->setText (a, juce::dontSendNotification);
        apps.setTitle ("Apps using Split  /  " + juce::String ((int) cl.size()));
        meters.repaint();
    }

    void paint (juce::Graphics& g) override { g.fillAll (col::bg); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12);
        top.setBounds (r.removeFromTop (200)); r.removeFromTop (10);
        ins.setBounds (r.removeFromBottom (210)); r.removeFromBottom (10);
        apps.setBounds (r);
        auto t = top.content();
        auto labels = t.removeFromTop (18), row = t.removeFromTop (36);
        const int ws[] = { 300, 130, 150 };
        juce::Component* cs[] = { &device, &rate, &buffer };
        int x = row.getX();
        for (int k = 0; k < 3; ++k) { lbls[(size_t) k]->setBounds (x, labels.getY(), ws[k], 18); cs[k]->setBounds (x, row.getY(), ws[k], 34); x += ws[k] + 10; }
        panelBtn->setBounds (x, row.getY() - 3, 150, 40);
        reloadBtn->setBounds (x + 158, row.getY() - 3, 100, 40);
        t.removeFromTop (10);
        retryBtn->setBounds (t.removeFromRight (120).removeFromTop (40));
        status->setBounds (t);
        auto a = apps.content();
        help->setBounds (a.removeFromBottom (54));
        warn->setBounds (a.removeFromBottom (40));
        appList->setBounds (a);
        meters.setBounds (ins.content());
    }

private:
    Engine& engine;
    Slab top, apps, ins;
    juce::ComboBox device, rate, buffer;
    std::unique_ptr<juce::TextButton> panelBtn, retryBtn, reloadBtn;
    std::unique_ptr<juce::Label> warn;
    struct LateHistory { juce::String name; uint32_t late = 0; juce::uint32 since = 0; double rate = 0; };
    std::map<int, LateHistory> history;
    std::vector<std::unique_ptr<juce::Label>> lbls;
    std::unique_ptr<juce::Label> status, appList, help;
    Meters meters;
    bool updating = false;
};

class HostWindow : public juce::DocumentWindow
{
public:
    HostWindow (Engine& e) : juce::DocumentWindow ("BC Split", col::bg, juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new HostComponent (e), false);
        setResizable (true, false);
        centreWithSize (880, 640);
    }
    void closeButtonPressed() override { setVisible (false); if (onClose) onClose(); }
    std::function<void()> onClose;
};

#if JUCE_WINDOWS
class Tray : public juce::SystemTrayIconComponent
{
public:
    std::function<void()> onShow, onQuit;
    Tray()
    {
        juce::Image img (juce::Image::ARGB, 32, 32, true);
        juce::Graphics g (img);
        g.setColour (juce::Colours::white);
        g.fillRoundedRectangle (4, 13, 8, 6, 2);
        juce::Path p; p.startNewSubPath (12, 16); p.lineTo (26, 6); p.startNewSubPath (12, 16); p.lineTo (26, 16); p.startNewSubPath (12, 16); p.lineTo (26, 26);
        g.strokePath (p, juce::PathStrokeType (3.0f));
        setIconImage (img, img);
        setIconTooltip ("BC Split");
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu()) { if (onShow) onShow(); return; }
        juce::PopupMenu m;
        m.addItem (1, "Show Split");
        m.addItem (2, "Quit Split (apps using it will stop)");
        m.showMenuAsync ({}, [this] (int r) { if (r == 1 && onShow) onShow(); if (r == 2 && onQuit) onQuit(); });
    }
};
#endif
}

// ============================================================ app

class SplitHostApp : public juce::JUCEApplication, private juce::Timer
{
public:
    const juce::String getApplicationName() override { return "BC Split"; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String& cmd) override
    {
        auto args = juce::StringArray::fromTokens (cmd, true);
        background = args.contains ("--background");
        stay = args.contains ("--stay");
        int shot = args.indexOf ("--screenshot");
        if (shot >= 0 && shot + 1 < args.size()) screenshotFile = juce::File::getCurrentWorkingDirectory().getChildFile (args[shot + 1].unquoted());
        juce::LookAndFeel::setDefaultLookAndFeel (&look);
        engine = std::make_unique<tk::splithost::Engine> (args.contains ("--test"));
        if (! engine->start()) { quit(); return; }
        window = std::make_unique<tk::splithost::HostWindow> (*engine);
        window->onClose = [this] { if (engine->core.attachedCount() == 0 && ! stay) quit(); };
        window->setVisible (! background || screenshotFile != juce::File());
#if JUCE_WINDOWS
        tray = std::make_unique<tk::splithost::Tray>();
        tray->onShow = [this] { showWindow(); };
        tray->onQuit = [this] { quit(); };
#endif
        startedAt = juce::Time::getMillisecondCounter();
        log ("started: " + cmd);
        lastClient = startedAt;
        startTimer (100);
    }

    void showWindow() { window->setVisible (true); window->toFront (true); }
    void anotherInstanceStarted (const juce::String&) override { showWindow(); }

    void timerCallback() override
    {
        engine->housekeeping();
        const auto now = juce::Time::getMillisecondCounter();
        int n = engine->core.attachedCount();
        if (n > 0) lastClient = now;
        // started by an app opening Split: quit 20 s after the last one lets go, unless the window is up
        if (background && ! stay && n == 0 && ! window->isVisible() && now - lastClient > 20000) quit();
        if (screenshotFile != juce::File() && ! shotTaken)
        {
            int running = 0;
            for (auto& c : engine->core.clients()) if (c.running) ++running;
            if (running >= 2) { if (shotAt == 0) shotAt = now + 700; }
            if (running != lastRunning) { lastRunning = running; log ("apps running: " + juce::String (running)); }
            if ((shotAt != 0 && now > shotAt) || now - startedAt > 10000) takeShot();
        }
    }

    void log (const juce::String& t)
    {
        if (screenshotFile != juce::File())
            screenshotFile.withFileExtension ("log").appendText (juce::Time::getCurrentTime().toString (false, true, true, true) + "  " + t + "\n");
    }

    void takeShot()
    {
        shotTaken = true;
        log ("taking screenshot");
        auto* c = window->getContentComponent();
        auto img = c->createComponentSnapshot (c->getLocalBounds(), true, 1.0f);
        screenshotFile.deleteFile();
        juce::FileOutputStream os (screenshotFile);
        bool ok = os.openedOk() && juce::PNGImageFormat().writeImageToStream (img, os);
        log (ok ? "saved " + screenshotFile.getFullPathName() : "couldn't write " + screenshotFile.getFullPathName());
    }

    void shutdown() override
    {
        stopTimer();
#if JUCE_WINDOWS
        tray.reset();
#endif
        window.reset();
        engine.reset();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override { quit(); }

private:
    tk::Look look;
    std::unique_ptr<tk::splithost::Engine> engine;
    std::unique_ptr<tk::splithost::HostWindow> window;
#if JUCE_WINDOWS
    std::unique_ptr<tk::splithost::Tray> tray;
#endif
    bool background = false, stay = false, shotTaken = false;
    juce::File screenshotFile;
    juce::uint32 startedAt = 0, lastClient = 0, shotAt = 0;
    int lastRunning = -1;
};

START_JUCE_APPLICATION (SplitHostApp)
