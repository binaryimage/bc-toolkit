#include "Pages.h"
#include "MainWindow.h"
#include "Common/Dsp.h"
#include <map>

namespace tk
{
namespace
{
struct Entry
{
    juce::String path, name, folder;
    juce::int64 modified = 0;
    double seconds = 0, sr = 0, bpm = 0, bright = 0, crest = 0;
    int channels = 0, keyRoot = -1;
    bool minor = false, loop = false, ok = false;

    juce::String keyName() const
    {
        if (keyRoot < 0) return {};
        return pitchClass (keyRoot + 60) + (minor ? "m" : "");
    }
    juce::String character() const
    {
        juce::StringArray t;
        t.add (bright < 0.28 ? "dark" : bright > 0.62 ? "bright" : "mid");
        if (crest > 14) t.add ("punchy"); else if (crest < 7) t.add ("dense");
        if (seconds > 6 && ! loop) t.add ("long");
        return t.joinIntoString (", ");
    }
    juce::var toVar() const
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("p", path); o->setProperty ("m", modified); o->setProperty ("s", seconds); o->setProperty ("r", sr);
        o->setProperty ("b", bpm); o->setProperty ("br", bright); o->setProperty ("c", crest); o->setProperty ("ch", channels);
        o->setProperty ("k", keyRoot); o->setProperty ("mi", minor); o->setProperty ("l", loop); o->setProperty ("ok", ok);
        return juce::var (o);
    }
    static Entry fromVar (const juce::var& v)
    {
        Entry e;
        e.path = v["p"].toString(); e.modified = (juce::int64) v["m"]; e.seconds = v["s"]; e.sr = v["r"]; e.bpm = v["b"];
        e.bright = v["br"]; e.crest = v["c"]; e.channels = v["ch"]; e.keyRoot = v["k"]; e.minor = v["mi"]; e.loop = v["l"]; e.ok = v["ok"];
        juce::File f (e.path); e.name = f.getFileNameWithoutExtension(); e.folder = f.getParentDirectory().getFileName();
        return e;
    }
};

Entry analyse (const juce::File& f)
{
    Entry e;
    e.path = f.getFullPathName(); e.name = f.getFileNameWithoutExtension(); e.folder = f.getParentDirectory().getFileName();
    e.modified = f.getLastModificationTime().toMilliseconds();
    std::unique_ptr<juce::AudioFormatReader> r (formats().createReaderFor (f));
    if (r == nullptr) return e;
    e.sr = r->sampleRate; e.channels = (int) r->numChannels;
    e.seconds = r->lengthInSamples / juce::jmax (1.0, r->sampleRate);
    const int len = (int) std::min<juce::int64> (r->lengthInSamples, (juce::int64) (r->sampleRate * 30));
    juce::AudioBuffer<float> b (juce::jmin (2, (int) r->numChannels), len);
    r->read (&b, 0, len, 0, true, b.getNumChannels() > 1);
    float pk = peakOf (b);
    if (pk < 1e-5f) { e.ok = true; return e; }
    auto mono = mixToMono (b);
    double rms = juce::Decibels::decibelsToGain (rmsDb (mono.getReadPointer (0), mono.getNumSamples()));
    e.crest = juce::Decibels::gainToDecibels (pk / juce::jmax (1e-6, rms));
    e.bright = brightness (spectralCentroid (b, e.sr));
    if (e.seconds >= 2.0)
    {
        e.bpm = estimateBpm (b, e.sr);
        if (e.bpm > 0)
        {
            // a loop is a file whose length lands near a whole number of bars at that tempo
            double barsF = e.seconds / (240.0 / e.bpm);
            e.loop = barsF >= 0.9 && std::abs (barsF - std::round (barsF)) < 0.08;
        }
    }
    auto k = estimateKey (b, e.sr);
    if (k.confidence > 0.55) { e.keyRoot = k.root; e.minor = k.minor; }
    e.ok = true;
    return e;
}

class LibraryPage : public Page, private juce::TableListBoxModel, private juce::Thread
{
public:
    enum Col { cName = 1, cFolder, cLen, cBpm, cKey, cChar, cType };

    LibraryPage (Engine& e) : Page (e, "library", "Sample library",
                                    "Scans your sample folders and tags every file with tempo, key, length and character. Click to hear it, drag it into Ableton."),
                              juce::Thread ("library scan")
    {
        addAndMakeVisible (top); top.setTitle ("Folders");
        addAndMakeVisible (tableSlab); tableSlab.setTitle ("Samples");
        folders.setMultiLine (true); folders.setReturnKeyStartsNewLine (true);
        folders.setText (Settings::get().str ("libraryFolders", defaultFolders()), juce::dontSendNotification);
        folders.onFocusLost = [this] { Settings::get().set ("libraryFolders", folders.getText()); };
        folders.setFont (monoFont (12.5f));
        top.addAndMakeVisible (folders);
        addBtn = makeButton ("Add folder...", [this] { addFolder(); });
        scanBtn = makeButton ("Scan", [this] { startScan(); });
        stopBtn = makeButton ("Stop", [this] { signalThreadShouldExit(); });
        for (auto* b : { addBtn.get(), scanBtn.get(), stopBtn.get() }) top.addAndMakeVisible (b);
        top.addAndMakeVisible (pill);

        search.setTextToShowWhenEmpty ("Search names and folders", col::text3);
        search.onTextChange = [this] { filter(); };
        kind.addItemList ({ "Everything", "Loops", "One-shots" }, 1); kind.setSelectedId (1, juce::dontSendNotification);
        key.addItem ("Any key", 1);
        const char* pcs[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        for (int i = 0; i < 12; ++i) { key.addItem (juce::String (pcs[i]) + "m (and " + pcs[(i + 3) % 12] + ")", 100 + i); }
        key.setSelectedId (1, juce::dontSendNotification);
        tone.addItemList ({ "Any character", "Dark", "Mid", "Bright", "Punchy" }, 1); tone.setSelectedId (1, juce::dontSendNotification);
        bpmLo.addItem ("Any BPM", 1); bpmHi.addItem ("and up", 1);
        for (int b = 60; b <= 180; b += 5) { bpmLo.addItem ("From " + juce::String (b), b); bpmHi.addItem ("to " + juce::String (b), b); }
        bpmLo.setSelectedId (1, juce::dontSendNotification); bpmHi.setSelectedId (1, juce::dontSendNotification);
        for (auto* c : std::initializer_list<juce::ComboBox*> { &kind, &key, &tone, &bpmLo, &bpmHi }) { styleCombo (*c); c->onChange = [this] { filter(); }; tableSlab.addAndMakeVisible (c); }
        tableSlab.addAndMakeVisible (search);
        auto& h = table.getHeader();
        h.addColumn ("Name", cName, 300, 120, 900);
        h.addColumn ("Folder", cFolder, 160, 60, 400);
        h.addColumn ("Length", cLen, 80, 60, 120);
        h.addColumn ("BPM", cBpm, 70, 50, 100);
        h.addColumn ("Key", cKey, 70, 50, 100);
        h.addColumn ("Character", cChar, 170, 80, 300);
        h.addColumn ("Type", cType, 90, 60, 120);
        h.setSortColumnId (cName, true);
        table.setModel (this);
        table.setRowHeight (30);
        table.setColour (juce::ListBox::backgroundColourId, col::screen);
        table.setMultipleSelectionEnabled (false);
        tableSlab.addAndMakeVisible (table);
        table.addMouseListener (this, true);
        chopBtn = makeButton ("To chop station", [this] { sendTo ("chop"); });
        mangleBtn = makeButton ("To mangle", [this] { sendTo ("mangle"); });
        videoBtn = makeButton ("To video", [this] { sendTo ("video"); });
        revealBtn = makeButton ("Show in folder", [this] { if (auto* e = selected()) juce::File (e->path).revealToUser(); });
        for (auto* b : { chopBtn.get(), mangleBtn.get(), videoBtn.get(), revealBtn.get() }) tableSlab.addAndMakeVisible (b);
        tipLbl = makeLabel ("Click a row to hear it. Drag a row straight into Ableton or the MPC software.", 12, col::text3);
        tableSlab.addAndMakeVisible (*tipLbl);
        loadCache();
    }

    ~LibraryPage() override { stopThread (5000); }

    static juce::String defaultFolders()
    {
        juce::StringArray s;
        s.add (musicFolder().getFullPathName());
        if (mpcDrive().isDirectory()) s.add (mpcDrive().getFullPathName());
        return s.joinIntoString ("\n");
    }

    void addFolder()
    {
        chooser = std::make_unique<juce::FileChooser> ("Add a sample folder", musicFolder());
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc) {
            auto d = fc.getResult();
            if (! d.isDirectory()) return;
            auto t = folders.getText().trim();
            folders.setText ((t.isEmpty() ? juce::String() : t + "\n") + d.getFullPathName());
            Settings::get().set ("libraryFolders", folders.getText());
        });
    }

    // ------------------------------------------------------------ cache
    juce::File cacheFile() const { return appDataFolder().getChildFile ("library.json"); }

    void loadCache()
    {
        auto v = juce::JSON::parse (cacheFile());
        if (auto* a = v.getArray()) for (auto& x : *a) all.push_back (Entry::fromVar (x));
        filter();
    }

    void saveCache()
    {
        juce::Array<juce::var> a;
        for (auto& e : all) a.add (e.toVar());
        cacheFile().getParentDirectory().createDirectory();
        cacheFile().replaceWithText (juce::JSON::toString (juce::var (a), true));
    }

    // ------------------------------------------------------------ scanning
    void startScan()
    {
        if (isThreadRunning()) return;
        Settings::get().set ("libraryFolders", folders.getText());
        roots.clear();
        for (auto& l : juce::StringArray::fromLines (folders.getText()))
            if (juce::File::isAbsolutePath (l.trim()) && juce::File (l.trim()).isDirectory()) roots.add (l.trim());
        startThread();
    }

    void run() override
    {
        std::map<juce::String, Entry> known;
        {
            const juce::ScopedLock sl (lock);
            for (auto& e : all) known[e.path] = e;
        }
        juce::Array<juce::File> files;
        setStatus ("Finding files...");
        for (auto& r : roots)
            for (const auto& it : juce::RangedDirectoryIterator (juce::File (r), true, "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg", juce::File::findFiles))
            {
                if (threadShouldExit()) break;
                files.add (it.getFile());
                if (files.size() > 60000) break;
            }
        std::vector<Entry> out;
        int fresh = 0;
        for (int i = 0; i < files.size() && ! threadShouldExit(); ++i)
        {
            auto& f = files.getReference (i);
            progress = (double) i / juce::jmax (1, files.size());
            auto it = known.find (f.getFullPathName());
            if (it != known.end() && it->second.modified == f.getLastModificationTime().toMilliseconds()) out.push_back (it->second);
            else { out.push_back (analyse (f)); ++fresh; }
            if (i % 25 == 0) setStatus (juce::String (i) + " of " + juce::String (files.size()) + " (" + juce::String (fresh) + " new)  " + f.getFileName());
        }
        if (threadShouldExit()) { setStatus ("Stopped. Nothing was lost; scan again to finish."); return; }
        {
            const juce::ScopedLock sl (lock);
            all = std::move (out);
        }
        saveCache();
        progress = 1;
        setStatus (juce::String (files.size()) + " files, " + juce::String (fresh) + " analysed this time.");
        juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<LibraryPage> (this)] { if (sp) sp->filter(); });
    }

    // ------------------------------------------------------------ filter + table
    void filter()
    {
        const juce::ScopedLock sl (lock);
        rows.clear();
        auto q = search.getText().trim();
        int k = key.getSelectedId() >= 100 ? key.getSelectedId() - 100 : -1;
        double lo = bpmLo.getSelectedId() > 1 ? bpmLo.getSelectedId() : 0, hi = bpmHi.getSelectedId() > 1 ? bpmHi.getSelectedId() + 0.99 : 1000;
        bool bpmFilter = lo > 0 || hi < 1000;
        for (int i = 0; i < (int) all.size(); ++i)
        {
            auto& e = all[(size_t) i];
            if (q.isNotEmpty() && ! (e.name.containsIgnoreCase (q) || e.folder.containsIgnoreCase (q))) continue;
            if (kind.getSelectedId() == 2 && ! e.loop) continue;
            if (kind.getSelectedId() == 3 && (e.loop || e.seconds > 3)) continue;
            if (k >= 0 && ! ((e.minor && e.keyRoot == k) || (! e.minor && e.keyRoot == (k + 3) % 12))) continue;
            if (tone.getSelectedId() == 2 && e.bright >= 0.28) continue;
            if (tone.getSelectedId() == 3 && (e.bright < 0.28 || e.bright > 0.62)) continue;
            if (tone.getSelectedId() == 4 && e.bright <= 0.62) continue;
            if (tone.getSelectedId() == 5 && e.crest <= 14) continue;
            if (bpmFilter && (e.bpm < lo || e.bpm > hi)) continue;
            rows.push_back (i);
        }
        sortOrderChanged (table.getHeader().getSortColumnId(), table.getHeader().isSortedForwards());
        tableSlab.setTitle ("Samples  /  " + juce::String ((int) rows.size()) + " shown of " + juce::String ((int) all.size()));
    }

    int getNumRows() override { return (int) rows.size(); }

    void paintRowBackground (juce::Graphics& g, int row, int w, int h, bool selected) override
    {
        if (selected) { g.setColour (juce::Colour (0xffe6e6e6)); g.fillRect (0, 0, w, h); }
        else if (row % 2) { g.setColour (juce::Colours::white.withAlpha (0.025f)); g.fillRect (0, 0, w, h); }
    }

    void paintCell (juce::Graphics& g, int row, int col, int w, int h, bool selected) override
    {
        if (row >= (int) rows.size()) return;
        const auto& e = all[(size_t) rows[(size_t) row]];
        juce::String t;
        switch (col)
        {
            case cName: t = e.name; break;
            case cFolder: t = e.folder; break;
            case cLen: t = fmtTime (e.seconds); break;
            case cBpm: t = e.bpm > 0 ? juce::String (e.bpm, 1) : "-"; break;
            case cKey: t = e.keyName().isEmpty() ? "-" : e.keyName(); break;
            case cChar: t = e.ok ? e.character() : "unreadable"; break;
            case cType: t = e.loop ? "loop" : e.seconds <= 3 ? "one-shot" : "phrase"; break;
        }
        g.setColour (selected ? juce::Colours::black : col == cName ? col::text : col::text2);
        g.setFont (col == cBpm || col == cKey || col == cLen ? font (13, 3) : font (13.5f, col == cName ? 1 : 0));
        g.drawText (t, 8, 0, w - 12, h, juce::Justification::centredLeft);
    }

    void sortOrderChanged (int col, bool fwd) override
    {
        auto key = [&] (const Entry& e) -> double {
            switch (col)
            {
                case cLen: return e.seconds; case cBpm: return e.bpm; case cKey: return e.keyRoot * 2 + (e.minor ? 1 : 0);
                case cChar: return e.bright; case cType: return e.loop ? 0 : e.seconds <= 3 ? 1 : 2; default: return 0;
            }
        };
        std::stable_sort (rows.begin(), rows.end(), [&] (int a, int b) {
            const auto& x = all[(size_t) a]; const auto& y = all[(size_t) b];
            bool less = (col == cName) ? x.name.compareNatural (y.name) < 0 : (col == cFolder) ? x.folder.compareNatural (y.folder) < 0 : key (x) < key (y);
            bool more = (col == cName) ? y.name.compareNatural (x.name) < 0 : (col == cFolder) ? y.folder.compareNatural (x.folder) < 0 : key (y) < key (x);
            return fwd ? less : more;
        });
        table.updateContent();
        table.repaint();
    }

    const Entry* selected() const
    {
        int r = table.getSelectedRow();
        return r >= 0 && r < (int) rows.size() ? &all[(size_t) rows[(size_t) r]] : nullptr;
    }

    void cellClicked (int row, int, const juce::MouseEvent&) override
    {
        if (row >= (int) rows.size()) return;
        juce::AudioBuffer<float> b; double sr;
        if (readAudio (juce::File (all[(size_t) rows[(size_t) row]].path), b, sr, 30)) { preview = std::move (b); engine.play (preview, sr); }
    }

    juce::var getDragSourceDescription (const juce::SparseSet<int>&) override { return "library"; }

    void sendTo (const juce::String& page)
    {
        auto* e = selected();
        if (e == nullptr || main == nullptr) return;
        juce::AudioBuffer<float> b; double sr;
        if (readAudio (juce::File (e->path), b, sr, 600)) main->sendAudio (page, b, sr, e->name);
    }

    void mouseDrag (const juce::MouseEvent& ev) override
    {
        if (ev.getDistanceFromDragStart() < 8 || draggingOut) return;
        if (auto* e = selected())
        {
            draggingOut = true;
            juce::DragAndDropContainer::performExternalDragDropOfFiles ({ e->path }, false, this, [this] { draggingOut = false; });
        }
    }

    void tick() override
    {
        pill.setValue (isThreadRunning() ? progress.load() : (all.empty() ? 0.0 : 1.0), getStatus());
        scanBtn->setEnabled (! isThreadRunning());
        stopBtn->setEnabled (isThreadRunning());
    }

    void resized() override
    {
        auto r = getLocalBounds();
        top.setBounds (r.removeFromTop (150));
        r.removeFromTop (10);
        tableSlab.setBounds (r);
        auto t = top.content();
        auto right = t.removeFromRight (520);
        folders.setBounds (t);
        right.removeFromLeft (12);
        rowLayout (right.removeFromTop (44), { { addBtn.get(), 150 }, { scanBtn.get(), 110 }, { stopBtn.get(), 90 } });
        right.removeFromTop (12);
        pill.setBounds (right.removeFromTop (28));

        auto c = tableSlab.content();
        rowLayout (c.removeFromTop (36), { { &search, 300 }, { &kind, 140 }, { &key, 190 }, { &tone, 160 }, { &bpmLo, 130 }, { &bpmHi, 110 } });
        c.removeFromTop (8);
        auto bottom = c.removeFromBottom (42);
        rowLayout (bottom, { { chopBtn.get(), 160 }, { mangleBtn.get(), 130 }, { videoBtn.get(), 120 }, { revealBtn.get(), 150 }, { tipLbl.get(), 600 } });
        c.removeFromBottom (6);
        table.setBounds (c);
        table.getHeader().setStretchToFitActive (true);
    }

    void demo() override
    {
        const juce::ScopedLock sl (lock);
        all.clear();
        juce::Random rnd (5);
        const char* names[] = { "Rust Kick 04", "Fifth Ward break 92", "Hydra Dark Pad C", "Tape Hiss Room", "2600 Growl Loop 118", "Snare Crack Wet", "Leviasynth Choir Dm",
                                "Messenger Sub D", "Dust Vinyl Loop 86", "Metal Hit 12", "VHS Strings Fm", "Rim Shot Dry", "Phrygian Bass Loop 120", "Hat Closed 03",
                                "MiniFreak Bell Am", "Basement Loop 90", "Noise Swell", "Clap Stack 02", "Drone Bowed G#", "EBM Bass Loop 124" };
        const char* folders_[] = { "Kicks", "Breaks", "Pads", "FX", "Loops", "Snares", "Pads", "Bass", "Loops", "Perc", "Pads", "Perc", "Loops", "Hats", "Keys", "Breaks", "FX", "Claps", "Drones", "Loops" };
        for (int i = 0; i < 20; ++i)
        {
            Entry e; e.name = names[i]; e.folder = folders_[i]; e.ok = true;
            juce::String n (names[i]);
            bool loopy = n.containsIgnoreCase ("loop") || n.containsIgnoreCase ("break");
            e.bpm = loopy ? n.getTrailingIntValue() : 0;
            e.loop = loopy;
            e.seconds = loopy ? 4 * 240.0 / e.bpm : 0.3 + rnd.nextFloat() * (n.contains ("Pad") || n.contains ("Drone") ? 8 : 1.4);
            e.bright = n.contains ("Hat") || n.contains ("Bell") || n.contains ("Hiss") ? 0.7 : n.contains ("Dark") || n.contains ("Sub") || n.contains ("Drone") ? 0.15 : 0.25 + rnd.nextFloat() * 0.35;
            e.crest = n.contains ("Kick") || n.contains ("Snare") || n.contains ("Rim") || n.contains ("Clap") ? 18 : 6 + rnd.nextFloat() * 8;
            if (! (n.contains ("Kick") || n.contains ("Hat") || n.contains ("Clap") || n.contains ("Snare") || n.contains ("Rim") || n.contains ("Hiss") || n.contains ("Noise")))
            { e.keyRoot = rnd.nextInt (12); e.minor = rnd.nextFloat() < 0.75f; }
            e.path = "C:/Users/you/Music/Samples/" + e.folder + "/" + e.name + ".wav";
            all.push_back (e);
        }
        setStatus ("20 files, 20 analysed this time.");
        progress = 1;
        juce::ScopedUnlock ul (lock);
        filter();
        table.selectRow (4);
    }

    Slab top, tableSlab;
    juce::TextEditor folders, search;
    std::unique_ptr<juce::TextButton> addBtn, scanBtn, stopBtn, chopBtn, mangleBtn, videoBtn, revealBtn;
    std::unique_ptr<juce::Label> tipLbl;
    juce::ComboBox kind, key, tone, bpmLo, bpmHi;
    juce::TableListBox table;
    ProgressPill pill;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::StringArray roots;
    juce::CriticalSection lock;
    std::vector<Entry> all;
    std::vector<int> rows;
    std::atomic<double> progress { 0 };
    void setStatus (const juce::String& t) { const juce::ScopedLock sl (statusLock); status = t; }
    juce::String getStatus() const { const juce::ScopedLock sl (statusLock); return status; }
    juce::CriticalSection statusLock;
    juce::String status { "Press Scan. The first scan takes a while; after that only new or changed files are analysed." };
    juce::AudioBuffer<float> preview;
    bool draggingOut = false;
};
}

std::unique_ptr<Page> makeLibraryPage (Engine& e) { return std::make_unique<LibraryPage> (e); }
}
