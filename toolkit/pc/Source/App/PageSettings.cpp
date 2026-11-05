#include "Pages.h"
#include "Common/Link.h"
#include "SplitCore.h"

namespace tk
{
namespace
{
class SettingsPage : public Page
{
public:
    SettingsPage (Engine& e) : Page (e, "settings", "Settings",
                                     "Audio interface, BC Link, PC audio capture and where things get saved.")
    {
        for (auto* s : { &audio, &link, &places }) addAndMakeVisible (s);
        audio.setTitle ("Audio interface"); link.setTitle ("BC Link + PC audio"); places.setTitle ("Folders + defaults");
        selector = std::make_unique<juce::AudioDeviceSelectorComponent> (engine.devices, 0, 32, 0, 2, false, false, false, false);
        audio.addAndMakeVisible (*selector);
        // anything you do in the picker is your choice, and gets saved (a fallback the app made on its own doesn't)
        selector->addMouseListener (this, true);
        deviceNote = makeLabel ("", 12.5f, col::white, 3);
        deviceNote->setJustificationType (juce::Justification::topLeft);
        audio.addChildComponent (*deviceNote);
        backBtn = makeButton ("Back to my device", [this] { engine.retryWantedDevice(); refreshDeviceNote(); });
        audio.addChildComponent (*backBtn);
        audioTip = makeLabel ("Pick your interface's ASIO driver and turn on the inputs your synths are plugged into. "
                              "If Ableton has the same ASIO driver open, most interfaces only allow one app at a time: "
                              "pick BC Split here and in Ableton (and RS_ASIO) so they share it, or use BC Link in Ableton.", 12, col::text3);
        audioTip->setJustificationType (juce::Justification::topLeft);
        audio.addAndMakeVisible (*audioTip);

        linkInfo = makeLabel ("", 13, col::text);
        linkInfo->setJustificationType (juce::Justification::topLeft);
        link.addAndMakeVisible (*linkInfo);
        loopBtn.setButtonText ("Capture PC audio (what you hear) as a source");
        loopBtn.setToggleState (engine.loopbackRunning(), juce::dontSendNotification);
        loopBtn.onClick = [this] {
            if (loopBtn.getToggleState()) loopBtn.setToggleState (engine.startLoopback(), juce::dontSendNotification);
            else engine.stopLoopback();
            Settings::get().set ("loopbackOn", loopBtn.getToggleState());
        };
        link.addAndMakeVisible (loopBtn);
        splitInfo = makeLabel ("", 13, col::text);
        splitInfo->setJustificationType (juce::Justification::topLeft);
        link.addAndMakeVisible (*splitInfo);
        splitBtn = makeButton ("Open Split", [] { launchSplit ("--show"); });
        link.addAndMakeVisible (*splitBtn);
        linkHow = makeLabel ("", 12, col::text3);
        linkHow->setJustificationType (juce::Justification::topLeft);
        link.addAndMakeVisible (*linkHow);

        jam.addItemList ({ "2 minutes", "5 minutes", "10 minutes", "15 minutes", "30 minutes" }, 1);
        const int jm[] = { 2, 5, 10, 15, 30 };
        for (int i = 0; i < 5; ++i) if (jm[i] == engine.jamMinutes()) jam.setSelectedId (i + 1, juce::dontSendNotification);
        jam.onChange = [this] { const int m[] = { 2, 5, 10, 15, 30 }; engine.setJamMinutes (m[jam.getSelectedId() - 1]); Settings::get().set ("jamMinutes", m[jam.getSelectedId() - 1]); };
        styleCombo (jam);
        a4.setRange (415, 466, 0.5); a4.setValue (Settings::get().dbl ("a4", 440), juce::dontSendNotification);
        a4.setSliderStyle (juce::Slider::LinearHorizontal); a4.setTextBoxStyle (juce::Slider::TextBoxRight, false, 80, 22); a4.setTextValueSuffix (" Hz");
        a4.setScrollWheelEnabled (false);
        a4.setDoubleClickReturnValue (true, 440.0);
        a4.onValueChange = [this] { Settings::get().set ("a4", a4.getValue()); };
        for (auto* c : std::initializer_list<juce::Component*> { &jam, &a4 }) places.addAndMakeVisible (c);
        jamLbl = makeLabel ("Jam recorder keeps", 12, col::text3); a4Lbl = makeLabel ("Tuning reference A4", 12, col::text3);
        places.addAndMakeVisible (*jamLbl); places.addAndMakeVisible (*a4Lbl);
        folderInfo = makeLabel ("", 12.5f, col::text2);
        folderInfo->setJustificationType (juce::Justification::topLeft);
        places.addAndMakeVisible (*folderInfo);
        openMusic = makeButton ("Open Music folder", [] { musicFolder().startAsProcess(); });
        openData = makeButton ("Open settings folder", [] { appDataFolder().createDirectory(); appDataFolder().startAsProcess(); });
        rescan = makeButton ("Rescan MIDI devices", [this] { if (auto* top = getTopLevelComponent()) rescanMidi (top); refreshInfo(); });
        for (auto* b : { openMusic.get(), openData.get(), rescan.get() }) places.addAndMakeVisible (b);
        refreshInfo();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.eventComponent != this && selector->isParentOf (e.eventComponent)) engine.userIsPickingDevice();
    }

    void refreshDeviceNote()
    {
        auto p = engine.deviceProblem();
        const bool show = p.isNotEmpty();
        if (show != deviceNote->isVisible()) { deviceNote->setVisible (show); backBtn->setVisible (show); resized(); }
        deviceNote->setText (p, juce::dontSendNotification);
        backBtn->setButtonText ("Back to " + engine.wantedDevice());
    }

    static void rescanMidi (juce::Component* c)
    {
        if (auto* m = dynamic_cast<MidiPicker*> (c)) m->refresh();
        for (auto* ch : c->getChildren()) rescanMidi (ch);
    }

    /** Split installs on its own: find it through the driver's COM registration, wherever it went */
    static juce::File splitHostExe()
    {
       #if JUCE_WINDOWS
        auto dll = juce::WindowsRegistry::getValue ("HKEY_CLASSES_ROOT\\CLSID\\" + juce::String (split::kClsid) + "\\InprocServer32\\");
        if (dll.isNotEmpty())
        {
            auto f = juce::File (dll).getSiblingFile (split::kHostExe);
            if (f.existsAsFile()) return f;
        }
       #endif
        return juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory).getChildFile ("BC Split").getChildFile (split::kHostExe);
    }
    static void launchSplit (const juce::String& args) { auto f = splitHostExe(); if (f.existsAsFile()) f.startAsProcess (args); }

    /** what BC Split is doing right now, read straight from its shared memory */
    static juce::String splitStatus()
    {
        split::SharedRegion r;
        if (! r.open (split::kRegionName, sizeof (split::Shared))) return "BC Split: not running (it starts by itself when an app opens it).";
        auto* sh = static_cast<split::Shared*> (r.data());
        if (sh->magic != split::kMagic || ! split::processAlive (sh->hostPid.load())) return "BC Split: not running (it starts by itself when an app opens it).";
        juce::String s ("BC Split: ");
        if (sh->hostState.load() == split::hostError) return s + "stopped: " + juce::String (sh->error);
        s << "sharing " << sh->deviceName << " at " << juce::String (sh->sampleRate / 1000, 1) << " kHz / " << sh->bufferSize << "\n   ";
        juce::StringArray apps;
        for (auto& c : sh->clients) if (c.state.load() != split::clientFree) apps.add (juce::String (c.name));
        s << (apps.isEmpty() ? juce::String ("no apps on it right now") : apps.joinIntoString (", "));
        return s;
    }

    void refreshInfo()
    {
        juce::String s;
        s << "Music:  " << musicFolder().getFullPathName() << "\n";
        auto k = mpcDrive();
        s << "MPC drive (Purple Idea):  " << (k.isDirectory() ? k.getFullPathName() + "  (plugged in)" : juce::String ("not plugged in, exports go to Music\\MPC Songs only")) << "\n";
        s << "MPC exports:  " << juce::StringArray ([] { juce::StringArray a; for (auto& f : mpcExportFolders ("...")) a.add (f.getFullPathName()); return a; }()).joinIntoString ("  +  ") << "\n";
        s << "Recordings:  Music\\Toolkit Recordings      Jams:  Music\\Toolkit Jams\n";
        s << "Samples:  Music\\Toolkit Samples      Wavetables:  Music\\Toolkit Wavetables      Videos:  Music\\BC Visualizer\n";
        s << "Patch backups:  " << Settings::get().folder ("backupRoot", musicFolder().getChildFile ("Synth Patches")).getFullPathName() << "\n";
        s << "MIDI:  " << engine.midiIns().size() << " inputs, " << engine.midiOuts().size() << " outputs";
        folderInfo->setText (s, juce::dontSendNotification);
    }

    void tick() override
    {
        if (++slow % 15) return;
        refreshDeviceNote();
        if (! a4.isMouseButtonDown())
        {
            const double want = Settings::get().dbl ("a4", 440);   // the Meters page has the same control
            if (std::abs (want - a4.getValue()) > 0.01) a4.setValue (want, juce::dontSendNotification);
        }
        juce::String s;
        s << "Listening on 127.0.0.1:" << link::kPort << "\n";
        int n = engine.linkCount();
        s << (n == 0 ? juce::String ("No Ableton tracks linked right now.") : juce::String (n) + (n == 1 ? " track linked:" : " tracks linked:")) << "\n";
        for (auto* src : engine.sources()) if (src->kind == Source::linked) s << "   " << engine.sourceLabel (src) << "   " << juce::String (src->sr.load() / 1000, 1) << " kHz\n";
        s << "\nPC audio capture: " << (engine.loopbackAvailable() ? (engine.loopbackRunning() ? "running" : "available") : "not available on this system");
        linkInfo->setText (s, juce::dontSendNotification);
        splitInfo->setText (splitStatus(), juce::dontSendNotification);
        splitBtn->setEnabled (splitHostExe().existsAsFile());
        if (! splitHostExe().existsAsFile()) splitInfo->setText ("BC Split isn't installed. It's a separate installer (BC-Split-Setup) if you want Ableton, Toolkit and Rocksmith on the interface at once.", juce::dontSendNotification);
        linkHow->setText ("BC Link is a VST3 that comes with the app. Copy \"BC Link.vst3\" into C:\\Program Files\\Common Files\\VST3, rescan plugins in Ableton, "
                          "and drop it at the end of any track (synth tracks, the master, a return). It passes audio through untouched and streams a copy here, named after the track. "
                          "Everything that track plays shows up as a source on every page.\n\n"
                          "PC audio capture records whatever Windows is playing through its default output (YouTube, a DAW using Windows audio). "
                          "It can't hear Ableton while Ableton runs on ASIO: use BC Link for that.", juce::dontSendNotification);
        loopBtn.setToggleState (engine.loopbackRunning(), juce::dontSendNotification);
        loopBtn.setEnabled (engine.loopbackAvailable());
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto left = r.removeFromLeft (r.getWidth() / 2);
        audio.setBounds (left.withTrimmedRight (5));
        auto rr = r.withTrimmedLeft (5);
        link.setBounds (rr.removeFromTop (rr.getHeight() / 2));
        rr.removeFromTop (10);
        places.setBounds (rr);
        auto a = audio.content();
        if (deviceNote->isVisible())
        {
            auto note = a.removeFromTop (64);
            backBtn->setBounds (note.removeFromRight (juce::jmin (190, note.getWidth() / 3)).removeFromTop (40));
            deviceNote->setBounds (note.withTrimmedRight (8));
            a.removeFromTop (6);
        }
        audioTip->setBounds (a.removeFromBottom (70));
        selector->setBounds (a);
        auto l = link.content();
        linkInfo->setBounds (l.removeFromTop (120));
        loopBtn.setBounds (l.removeFromTop (30)); l.removeFromTop (10);
        auto sp = l.removeFromTop (44);
        splitBtn->setBounds (sp.removeFromRight (140));
        splitInfo->setBounds (sp);
        l.removeFromTop (8);
        linkHow->setBounds (l);
        auto p = places.content();
        // jam + tuning share a row when there's room, otherwise one row each (so the slider never collapses to nothing)
        const bool oneRow = p.getWidth() >= 130 + 150 + 20 + 140 + 220;
        auto row = p.removeFromTop (34);
        jamLbl->setBounds (row.removeFromLeft (130)); jam.setBounds (row.removeFromLeft (150));
        if (! oneRow) { p.removeFromTop (6); row = p.removeFromTop (34); } else row.removeFromLeft (20);
        a4Lbl->setBounds (row.removeFromLeft (140)); a4.setBounds (row);
        p.removeFromTop (10);
        const int btnH = flowLayout (p, { { openMusic.get(), 170 }, { openData.get(), 180 }, { rescan.get(), 190 } }, 40, 8, false);
        flowLayout (p.removeFromBottom (btnH), { { openMusic.get(), 170 }, { openData.get(), 180 }, { rescan.get(), 190 } }, 40, 8);
        p.removeFromBottom (6);
        folderInfo->setBounds (p);
    }

    Slab audio, link, places;
    std::unique_ptr<juce::AudioDeviceSelectorComponent> selector;
    std::unique_ptr<juce::Label> audioTip, linkInfo, splitInfo, linkHow, jamLbl, a4Lbl, folderInfo, deviceNote;
    std::unique_ptr<juce::TextButton> openMusic, openData, rescan, splitBtn, backBtn;
    juce::ToggleButton loopBtn;
    juce::ComboBox jam;
    juce::Slider a4;
    int slow = 0;
};
}

std::unique_ptr<Page> makeSettingsPage (Engine& e) { return std::make_unique<SettingsPage> (e); }
}
