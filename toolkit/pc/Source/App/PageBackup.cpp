#include "Pages.h"

namespace tk
{
namespace
{
struct SynthProfile
{
    const char* id; const char* name;
    const char* folderHint;          // what the manager app is called / what to look for
    juce::StringArray searchWords;   // folder names to look for when hunting for the library
    const char* sysexHint;
};

const SynthProfile kSynths[] = {
    { "hydrasynth", "Hydrasynth", "ASM Hydrasynth Manager keeps its patch library on your PC. Point this at that folder (Find it looks in Documents and AppData).",
      { "Hydrasynth", "ASM" },
      "Start Listen, then send a patch or bank from the synth or from Hydrasynth Manager. Whatever arrives is saved exactly as sent, so it can go back the same way." },
    { "leviasynth", "Leviasynth", "ASM Leviasynth Manager keeps its library on your PC. Point this at that folder.",
      { "Leviasynth", "ASM" },
      "Start Listen, then send a patch or bank from the synth or its manager app. Saved byte for byte." },
    { "minifreak", "MiniFreak", "MiniFreak V / Arturia Software Center keep presets under Documents or ProgramData\\Arturia. Point this at the MiniFreak preset folder.",
      { "MiniFreak", "Arturia" },
      "The MiniFreak's patches are normally synced with MiniFreak V, so the folder backup is the main one. Listen still catches any SysEx it sends." },
    { "messenger", "Moog Messenger", "If you use a Moog editor or librarian for the Messenger, point this at its preset folder.",
      { "Messenger", "Moog" },
      "Start Listen, then trigger a dump from the Messenger's menu or its editor. Saved byte for byte." },
    { "other", "Anything else", "Any folder you want snapshotted: Ableton presets, MPC programs, another synth's library.",
      { },
      "Any synth that can send SysEx: start Listen, trigger the dump on the synth, then Save." },
};

juce::File backupRoot() { return Settings::get().folder ("backupRoot", musicFolder().getChildFile ("Synth Patches")); }

/** Copies a folder tree; returns number of files. */
int copyTree (const juce::File& from, const juce::File& to, std::function<bool()> keepGoing)
{
    int n = 0;
    for (const auto& it : juce::RangedDirectoryIterator (from, true, "*", juce::File::findFiles))
    {
        if (keepGoing && ! keepGoing()) break;
        auto rel = it.getFile().getRelativePathFrom (from);
        auto dst = to.getChildFile (rel);
        dst.getParentDirectory().createDirectory();
        if (it.getFile().copyFileTo (dst)) ++n;
    }
    return n;
}

/** A fingerprint of a folder (names, sizes, dates) so we only snapshot when something changed. */
juce::String fingerprint (const juce::File& dir)
{
    juce::int64 h = 1469598103934665603LL;
    int n = 0;
    for (const auto& it : juce::RangedDirectoryIterator (dir, true, "*", juce::File::findFiles))
    {
        auto s = it.getFile().getRelativePathFrom (dir) + juce::String (it.getFileSize()) + juce::String (it.getModificationTime().toMilliseconds());
        h = (h ^ s.hashCode64()) * 1099511628211LL;
        ++n;
    }
    return juce::String::toHexString (h) + ":" + juce::String (n);
}

class BackupPage : public Page, private juce::Thread, private juce::ListBoxModel
{
public:
    BackupPage (Engine& e) : Page (e, "backup", "Patch backup",
                                   "Snapshots your synths' patches into your own dated folders: the manager apps' libraries, plus raw SysEx dumps. No editor, just safe copies."),
                             juce::Thread ("backup")
    {
        addAndMakeVisible (side); side.setTitle ("Synths");
        for (int i = 0; i < 5; ++i)
        {
            auto b = makeButton (kSynths[i].name, [this, i] { setSynth (i); }, true);
            b->setRadioGroupId (31);
            side.addAndMakeVisible (*b); synthBtns.push_back (std::move (b));
        }
        rootLbl = makeLabel ("", 12, col::text3);
        rootLbl->setJustificationType (juce::Justification::topLeft);
        side.addAndMakeVisible (*rootLbl);
        rootBtn = makeButton ("Change backup folder...", [this] { chooseRoot(); });
        openRootBtn = makeButton ("Open backups", [] { backupRoot().createDirectory(); backupRoot().startAsProcess(); });
        allBtn = makeButton ("Back up everything now", [this] { backupAll(); });
        for (auto* b : { rootBtn.get(), openRootBtn.get(), allBtn.get() }) side.addAndMakeVisible (b);
        autoBtn.setButtonText ("Snapshot changed folders when Toolkit opens");
        autoBtn.setToggleState (Settings::get().flag ("backupOnLaunch", true), juce::dontSendNotification);
        autoBtn.onClick = [this] { Settings::get().set ("backupOnLaunch", autoBtn.getToggleState()); };
        side.addAndMakeVisible (autoBtn);

        addAndMakeVisible (folderSlab); folderSlab.setTitle ("Library folder");
        folderHint = makeLabel ("", 12.5f, col::text2);
        folderHint->setJustificationType (juce::Justification::topLeft);
        folderSlab.addAndMakeVisible (*folderHint);
        folderEd.onTextChange = [this] { Settings::get().set ("backupFolder_" + juce::String (kSynths[synth].id), folderEd.getText().trim()); };
        folderSlab.addAndMakeVisible (folderEd);
        browseBtn = makeButton ("Browse...", [this] { browse(); });
        findBtn = makeButton ("Find it", [this] { find(); });
        snapBtn = makeButton ("Snapshot now", [this] { snapshotCurrent(); });
        for (auto* b : { browseBtn.get(), findBtn.get(), snapBtn.get() }) folderSlab.addAndMakeVisible (b);
        folderStatus = makeLabel ("", 12.5f, col::text2); folderSlab.addAndMakeVisible (*folderStatus);

        addAndMakeVisible (sysexSlab); sysexSlab.setTitle ("SysEx dump");
        sysexHint = makeLabel ("", 12.5f, col::text2);
        sysexHint->setJustificationType (juce::Justification::topLeft);
        sysexSlab.addAndMakeVisible (*sysexHint);
        midiIn = std::make_unique<MidiPicker> (false, "backupIn"); styleCombo (*midiIn);
        midiOut = std::make_unique<MidiPicker> (true, "backupOut"); styleCombo (*midiOut);
        sysexSlab.addAndMakeVisible (*midiIn); sysexSlab.addAndMakeVisible (*midiOut);
        inLbl = makeLabel ("From synth (MIDI in)", 12, col::text3); outLbl = makeLabel ("To synth (MIDI out)", 12, col::text3);
        sysexSlab.addAndMakeVisible (*inLbl); sysexSlab.addAndMakeVisible (*outLbl);
        listenBtn = makeButton ("Listen", [this] { toggleListen(); }, true);
        saveSyxBtn = makeButton ("Save capture", [this] { saveCapture(); });
        clearBtn = makeButton ("Clear", [this] { const juce::ScopedLock sl (capLock); captured.clear(); });
        sendBtn = makeButton ("Send a .syx to the synth...", [this] { pickAndSend(); });
        for (auto* b : { listenBtn.get(), saveSyxBtn.get(), clearBtn.get(), sendBtn.get() }) sysexSlab.addAndMakeVisible (b);
        pace.setRange (10, 400, 5); pace.setValue (Settings::get().num ("sysexPace", 80), juce::dontSendNotification);
        pace.setSliderStyle (juce::Slider::LinearHorizontal); pace.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 22); pace.setTextValueSuffix (" ms");
        pace.onValueChange = [this] { Settings::get().set ("sysexPace", (int) pace.getValue()); };
        paceLbl = makeLabel ("Gap between messages when sending", 12, col::text3);
        sysexSlab.addAndMakeVisible (pace); sysexSlab.addAndMakeVisible (*paceLbl);
        sysexSlab.addAndMakeVisible (pill);

        addAndMakeVisible (histSlab); histSlab.setTitle ("Backups");
        hist.setModel (this); hist.setRowHeight (34);
        hist.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        histSlab.addAndMakeVisible (hist);
        openBtn = makeButton ("Open", [this] { if (auto f = selectedBackup(); f.exists()) f.startAsProcess(); });
        restoreBtn = makeButton ("Put back into library folder...", [this] { restore(); });
        for (auto* b : { openBtn.get(), restoreBtn.get() }) histSlab.addAndMakeVisible (b);

        setSynth (juce::jlimit (0, 4, Settings::get().num ("backupSynth", 0)));
        if (Settings::get().flag ("backupOnLaunch", true)) juce::Timer::callAfterDelay (4000, [sp = juce::Component::SafePointer<BackupPage> (this)] { if (sp) sp->backupAll (true); });
    }

    ~BackupPage() override { engine.stopListening(); stopThread (4000); }

    void setSynth (int i)
    {
        synth = i;
        Settings::get().set ("backupSynth", i);
        for (int k = 0; k < 5; ++k) synthBtns[(size_t) k]->setToggleState (k == i, juce::dontSendNotification);
        folderHint->setText (kSynths[i].folderHint, juce::dontSendNotification);
        sysexHint->setText (kSynths[i].sysexHint, juce::dontSendNotification);
        folderEd.setText (Settings::get().str ("backupFolder_" + juce::String (kSynths[i].id)), juce::dontSendNotification);
        folderSlab.setTitle ("Library folder  /  " + juce::String (kSynths[i].name));
        sysexSlab.setTitle ("SysEx dump  /  " + juce::String (kSynths[i].name));
        refreshHistory();
        updateFolderStatus();
    }

    juce::File synthDir (int i) const { return backupRoot().getChildFile (kSynths[i].name); }

    void updateFolderStatus()
    {
        juce::File f (folderEd.getText().trim());
        if (folderEd.getText().trim().isEmpty()) folderStatus->setText ("No folder set yet.", juce::dontSendNotification);
        else if (! juce::File::isAbsolutePath (folderEd.getText().trim()) || ! f.isDirectory()) folderStatus->setText ("That folder doesn't exist on this PC.", juce::dontSendNotification);
        else
        {
            int n = f.getNumberOfChildFiles (juce::File::findFiles, "*");
            auto last = Settings::get().str ("backupLast_" + juce::String (kSynths[synth].id));
            folderStatus->setText (juce::String (n) + " files at the top level." + (last.isNotEmpty() ? "  Last snapshot: " + last : juce::String()), juce::dontSendNotification);
        }
    }

    void chooseRoot()
    {
        chooser = std::make_unique<juce::FileChooser> ("Where should backups go?", backupRoot());
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc) {
            if (fc.getResult().isDirectory()) { Settings::get().set ("backupRoot", fc.getResult().getFullPathName()); refreshHistory(); resized(); }
        });
    }

    void browse()
    {
        chooser = std::make_unique<juce::FileChooser> ("Pick the library folder", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory));
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc) {
            if (fc.getResult().isDirectory()) { folderEd.setText (fc.getResult().getFullPathName()); updateFolderStatus(); }
        });
    }

    /** Hunts a few levels deep in the usual places for a folder named after the synth. */
    void find()
    {
        auto& p = kSynths[synth];
        if (p.searchWords.isEmpty()) { folderStatus->setText ("Pick the folder with Browse.", juce::dontSendNotification); return; }
        juce::Array<juce::File> roots { juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
                                        juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory),
                                        juce::File::getSpecialLocation (juce::File::commonApplicationDataDirectory),
                                        juce::File::getSpecialLocation (juce::File::commonDocumentsDirectory) };
#if JUCE_WINDOWS
        roots.add (juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("AppData/Local"));
#endif
        juce::Array<juce::File> hits;
        std::function<void (const juce::File&, int)> walk = [&] (const juce::File& d, int depth) {
            if (depth > 4 || hits.size() > 12) return;
            for (auto& c : d.findChildFiles (juce::File::findDirectories, false))
            {
                if (c.getFileName().containsIgnoreCase (p.searchWords[0])) hits.add (c);
                else if (depth < 2 || p.searchWords.size() < 2 || c.getFileName().containsIgnoreCase (p.searchWords[1]) || depth < 1) walk (c, depth + 1);
            }
        };
        for (auto& r : roots) if (r.isDirectory()) walk (r, 0);
        if (hits.isEmpty()) { folderStatus->setText ("Didn't find a " + juce::String (p.searchWords[0]) + " folder. Use Browse.", juce::dontSendNotification); return; }
        if (hits.size() == 1) { folderEd.setText (hits[0].getFullPathName()); updateFolderStatus(); return; }
        juce::PopupMenu m;
        for (int i = 0; i < hits.size(); ++i) m.addItem (i + 1, hits[i].getFullPathName());
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (findBtn.get()), [this, hits] (int r) {
            if (r > 0) { folderEd.setText (hits[r - 1].getFullPathName()); updateFolderStatus(); }
        });
    }

    // ------------------------------------------------------------ folder snapshots (background thread)
    void snapshotCurrent() { queue = { synth }; force = true; startThread(); }

    void backupAll (bool onlyChanged = false)
    {
        if (isThreadRunning()) return;
        queue.clear();
        for (int i = 0; i < 5; ++i) queue.push_back (i);
        force = ! onlyChanged;
        startThread();
    }

    void run() override
    {
        int made = 0, skipped = 0;
        for (size_t q = 0; q < queue.size() && ! threadShouldExit(); ++q)
        {
            int i = queue[q];
            auto id = juce::String (kSynths[i].id);
            juce::File src (Settings::get().str ("backupFolder_" + id));
            if (! juce::File::isAbsolutePath (src.getFullPathName()) || ! src.isDirectory()) continue;
            progress = (double) q / queue.size();
            setStatus ("Checking " + juce::String (kSynths[i].name) + "...");
            auto fp = fingerprint (src);
            if (! force && fp == Settings::get().str ("backupPrint_" + id)) { ++skipped; continue; }
            auto dst = synthDir (i).getChildFile (stamp() + " library");
            setStatus ("Copying " + juce::String (kSynths[i].name) + "...");
            int n = copyTree (src, dst, [this] { return ! threadShouldExit(); });
            dst.getChildFile ("_from.txt").replaceWithText ("Copied from: " + src.getFullPathName() + "\n" + juce::String (n) + " files\n" + juce::Time::getCurrentTime().toString (true, true) + "\n");
            Settings::get().set ("backupPrint_" + id, fp);
            Settings::get().set ("backupLast_" + id, stamp());
            ++made;
        }
        progress = 1;
        setStatus (made == 0 && skipped > 0 ? "Nothing changed since the last snapshot." : juce::String (made) + (made == 1 ? " snapshot saved." : " snapshots saved."));
        juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<BackupPage> (this)] { if (sp) { sp->refreshHistory(); sp->updateFolderStatus(); } });
    }

    // ------------------------------------------------------------ SysEx
    void toggleListen()
    {
        if (! listenBtn->getToggleState()) { engine.stopListening(); return; }
        if (! engine.listen (midiIn->id(), [this] (const juce::MidiMessage& m) {
                if (! m.isSysEx()) return;
                const juce::ScopedLock sl (capLock);
                captured.push_back (juce::MemoryBlock (m.getRawData(), (size_t) m.getRawDataSize()));
            }))
        {
            listenBtn->setToggleState (false, juce::dontSendNotification);
            setStatus ("Couldn't open that MIDI input. Is another app using it?");
        }
    }

    void saveCapture()
    {
        juce::MemoryBlock all;
        int count = 0;
        {
            const juce::ScopedLock sl (capLock);
            for (auto& m : captured) { all.append (m.getData(), m.getSize()); ++count; }
        }
        if (count == 0) { setStatus ("Nothing captured yet."); return; }
        auto dir = synthDir (synth);
        dir.createDirectory();
        auto f = dir.getChildFile (stamp() + " SysEx.syx");
        f.replaceWithData (all.getData(), all.getSize());
        setStatus ("Saved " + juce::String (count) + " messages (" + juce::String ((int) all.getSize()) + " bytes) to " + f.getFileName());
        refreshHistory();
    }

    void pickAndSend()
    {
        chooser = std::make_unique<juce::FileChooser> ("Send which .syx?", synthDir (synth), "*.syx");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc) {
            if (fc.getResult().existsAsFile()) sendFile (fc.getResult());
        });
    }

    void sendFile (const juce::File& f)
    {
        juce::MemoryBlock raw;
        f.loadFileAsData (raw);
        auto msgs = splitSysex (raw);
        if (msgs.empty()) { setStatus ("No SysEx messages in that file."); return; }
        auto out = midiOut->id();
        if (out.isEmpty()) { setStatus ("Pick a MIDI out first."); return; }
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Send " + juce::String ((int) msgs.size()) + " messages?",
            "This sends " + f.getFileName() + " to the synth. Depending on what's inside it may overwrite patches on the synth.", "Send", "Cancel", nullptr,
            juce::ModalCallbackFunction::create ([this, msgs, out] (int r) {
                if (r != 1) return;
                sender = std::make_unique<Sender> (*this, msgs, out, (int) pace.getValue());
                sender->startThread();
            }));
    }

    struct Sender : public juce::Thread
    {
        Sender (BackupPage& p, std::vector<juce::MemoryBlock> m, juce::String o, int gap) : juce::Thread ("sysex send"), page (p), msgs (std::move (m)), out (std::move (o)), gapMs (gap) {}
        ~Sender() override { stopThread (3000); }
        void run() override
        {
            for (size_t i = 0; i < msgs.size() && ! threadShouldExit(); ++i)
            {
                page.engine.sendNow (out, juce::MidiMessage (msgs[i].getData(), (int) msgs[i].getSize()));
                page.progress = (double) (i + 1) / msgs.size();
                page.setStatus ("Sending " + juce::String ((int) i + 1) + " of " + juce::String ((int) msgs.size()));
                // big messages need longer: add 1 ms per 32 bytes on top of the gap (31250 baud is ~3 bytes/ms on DIN)
                wait (gapMs + (int) (msgs[i].getSize() / 32));
            }
            page.setStatus ("Sent.");
        }
        BackupPage& page; std::vector<juce::MemoryBlock> msgs; juce::String out; int gapMs;
    };

    // ------------------------------------------------------------ history
    void refreshHistory()
    {
        backups.clear();
        for (auto& f : synthDir (synth).findChildFiles (juce::File::findFilesAndDirectories, false))
            if (! f.isHidden()) backups.add (f);
        std::sort (backups.begin(), backups.end(), [] (const juce::File& a, const juce::File& b) { return a.getFileName() > b.getFileName(); });
        hist.updateContent(); hist.repaint();
        histSlab.setTitle ("Backups  /  " + juce::String (kSynths[synth].name) + "  /  " + juce::String (backups.size()));
        rootLbl->setText ("Backups go to\n" + backupRoot().getFullPathName() + "\n(one folder per synth, one dated folder or .syx per snapshot)", juce::dontSendNotification);
    }

    juce::File selectedBackup() const { int r = hist.getSelectedRow(); return juce::isPositiveAndBelow (r, backups.size()) ? backups[r] : juce::File(); }

    int getNumRows() override { return backups.size(); }
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool sel) override
    {
        if (! juce::isPositiveAndBelow (row, backups.size())) return;
        auto f = backups[row];
        auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (2, 2);
        drawKey (g, r, sel, false, false, 6);
        g.setColour (sel ? juce::Colours::black : col::text); g.setFont (font (13.5f, 1));
        g.drawText (f.getFileName(), r.reduced (12, 0).toNearestInt(), juce::Justification::centredLeft);
        juce::String info = ! f.exists() ? juce::String() : f.isDirectory() ? juce::String (f.getNumberOfChildFiles (juce::File::findFiles, "*")) + " items" : juce::File::descriptionOfSizeInBytes (f.getSize());
        g.setColour (sel ? juce::Colour (0xff404040) : col::text3); g.setFont (font (11.5f));
        g.drawText (info, r.reduced (12, 0).toNearestInt(), juce::Justification::centredRight);
    }

    void restore()
    {
        auto b = selectedBackup();
        if (! b.exists()) return;
        if (b.hasFileExtension ("syx")) { sendFile (b); return; }
        juce::File dst (folderEd.getText().trim());
        if (! dst.isDirectory()) { setStatus ("Set the library folder first."); return; }
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::WarningIcon, "Put this snapshot back?",
            "Files in " + dst.getFullPathName() + " with the same names will be replaced. A safety snapshot of the folder as it is now is taken first. "
            "Close the manager app before you do this.", "Put it back", "Cancel", nullptr,
            juce::ModalCallbackFunction::create ([this, b, dst] (int r) {
                if (r != 1) return;
                auto safety = synthDir (synth).getChildFile (stamp() + " before restore");
                copyTree (dst, safety, nullptr);
                int n = 0;
                for (const auto& it : juce::RangedDirectoryIterator (b, true, "*", juce::File::findFiles))
                {
                    if (it.getFile().getFileName() == "_from.txt") continue;
                    auto to = dst.getChildFile (it.getFile().getRelativePathFrom (b));
                    to.getParentDirectory().createDirectory();
                    if (it.getFile().copyFileTo (to)) ++n;
                }
                setStatus ("Put back " + juce::String (n) + " files. Safety copy: " + safety.getFileName());
                refreshHistory();
            }));
    }

    void setStatus (const juce::String& s) { const juce::ScopedLock sl (statusLock); status = s; }
    juce::String getStatus() const { const juce::ScopedLock sl (statusLock); return status; }

    void tick() override
    {
        int n; size_t bytes = 0;
        {
            const juce::ScopedLock sl (capLock);
            n = (int) captured.size();
            for (auto& m : captured) bytes += m.getSize();
        }
        auto st = getStatus();
        if (listenBtn->getToggleState() && ! (sender && sender->isThreadRunning()))
            st = "Listening: " + juce::String (n) + " messages, " + juce::String ((int) bytes) + " bytes so far";
        pill.setValue (isThreadRunning() || (sender && sender->isThreadRunning()) ? progress.load() : (n > 0 ? 1.0 : 0.0), st);
        saveSyxBtn->setEnabled (n > 0);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        side.setBounds (r.removeFromLeft (330));
        r.removeFromLeft (10);
        auto right = r.removeFromRight (r.getWidth() * 42 / 100);
        histSlab.setBounds (right.withTrimmedLeft (10));
        folderSlab.setBounds (r.removeFromTop (250));
        r.removeFromTop (10);
        sysexSlab.setBounds (r);

        auto s = side.content();
        for (auto& b : synthBtns) { b->setBounds (s.removeFromTop (42)); s.removeFromTop (3); }
        s.removeFromTop (14);
        autoBtn.setBounds (s.removeFromTop (28)); s.removeFromTop (8);
        allBtn->setBounds (s.removeFromTop (44)); s.removeFromTop (4);
        openRootBtn->setBounds (s.removeFromTop (40)); s.removeFromTop (4);
        rootBtn->setBounds (s.removeFromTop (40)); s.removeFromTop (8);
        rootLbl->setBounds (s.removeFromTop (70));

        auto f = folderSlab.content();
        folderHint->setBounds (f.removeFromTop (40));
        auto row = f.removeFromTop (36);
        if (row.getWidth() - 250 < 200) { folderEd.setBounds (row); f.removeFromTop (6); row = f.removeFromTop (36); rowLayout (row, { { browseBtn.get(), 110 }, { findBtn.get(), 120 } }); }
        else rowLayout (row, { { &folderEd, row.getWidth() - 250 }, { browseBtn.get(), 110 }, { findBtn.get(), 120 } });
        f.removeFromTop (8);
        rowLayout (f.removeFromTop (44), { { snapBtn.get(), 170 }, { folderStatus.get(), 600 } });

        auto x = sysexSlab.content();
        sysexHint->setBounds (x.removeFromTop (40));
        auto labels = x.removeFromTop (18);
        auto pick = x.removeFromTop (34);
        int half = pick.getWidth() / 2 - 6;
        inLbl->setBounds (labels.removeFromLeft (half)); labels.removeFromLeft (12); outLbl->setBounds (labels);
        midiIn->setBounds (pick.removeFromLeft (half)); pick.removeFromLeft (12); midiOut->setBounds (pick);
        x.removeFromTop (10);
        rowLayout (x.removeFromTop (44), { { listenBtn.get(), 110 }, { saveSyxBtn.get(), 140 }, { clearBtn.get(), 90 }, { sendBtn.get(), 240 } });
        x.removeFromTop (8);
        auto pr = x.removeFromTop (30);
        if (pr.getWidth() < 230 + 200) { paceLbl->setBounds (pr); pr = x.removeFromTop (30); }
        else paceLbl->setBounds (pr.removeFromLeft (230));
        pace.setBounds (pr.withWidth (juce::jmin (360, pr.getWidth())));
        x.removeFromTop (8);
        pill.setBounds (x.removeFromTop (28));

        auto h = histSlab.content();
        rowLayout (h.removeFromBottom (44), { { openBtn.get(), 90 }, { restoreBtn.get(), 260 } });
        h.removeFromBottom (6);
        hist.setBounds (h);
    }

    void demo() override
    {
        setSynth (0);
        folderEd.setText ("C:\\Users\\you\\Documents\\ASM\\Hydrasynth\\Patches", juce::dontSendNotification);
        folderStatus->setText ("214 files at the top level.  Last snapshot: 2026-11-02 09-12-44", juce::dontSendNotification);
        backups.clear();
        for (auto* n : { "2026-11-02 09-12-44 library", "2026-11-01 22-40-03 SysEx.syx", "2026-10-30 18-05-51 library", "2026-10-24 11-30-17 library", "2026-10-24 11-29-02 before restore" })
            backups.add (synthDir (0).getChildFile (n));
        hist.updateContent(); hist.selectRow (0);
        histSlab.setTitle ("Backups  /  Hydrasynth  /  5");
        const juce::ScopedLock sl (capLock);
        captured.assign (3, juce::MemoryBlock (1240));
        listenBtn->setToggleState (true, juce::dontSendNotification);
    }

    Slab side, folderSlab, sysexSlab, histSlab;
    std::vector<std::unique_ptr<juce::TextButton>> synthBtns;
    std::unique_ptr<juce::TextButton> rootBtn, openRootBtn, allBtn, browseBtn, findBtn, snapBtn, listenBtn, saveSyxBtn, clearBtn, sendBtn, openBtn, restoreBtn;
    std::unique_ptr<juce::Label> rootLbl, folderHint, folderStatus, sysexHint, inLbl, outLbl, paceLbl;
    juce::ToggleButton autoBtn;
    juce::TextEditor folderEd;
    std::unique_ptr<MidiPicker> midiIn, midiOut;
    juce::Slider pace;
    ProgressPill pill;
    juce::ListBox hist;
    juce::Array<juce::File> backups;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<Sender> sender;
    std::vector<int> queue;
    bool force = false;
    int synth = 0;
    std::atomic<double> progress { 0 };
    juce::CriticalSection capLock, statusLock;
    std::vector<juce::MemoryBlock> captured;
    juce::String status { "Ready." };
};
}

std::unique_ptr<Page> makeBackupPage (Engine& e) { return std::make_unique<BackupPage> (e); }
}
