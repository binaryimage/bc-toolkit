#include "MainWindow.h"
#include "Common/LayoutCheck.h"
#include <map>
#include "Pages.h"

namespace tk
{
// ------------------------------------------------------------ shared page helpers

void SourcePicker::refresh()
{
    auto prev = getText();
    list = engine.sources();
    clear (juce::dontSendNotification);
    for (int i = 0; i < list.size(); ++i) addItem (engine.sourceLabel (list[i]), i + 1);
    auto want = prev.isNotEmpty() ? prev : Settings::get().str (settingsKey);
    for (int i = 0; i < list.size(); ++i) if (engine.sourceLabel (list[i]) == want) { setSelectedId (i + 1, juce::dontSendNotification); return; }
    if (list.size() > 0) setSelectedId (1, juce::dontSendNotification);
}

Source* SourcePicker::selected() const
{
    int i = getSelectedId() - 1;
    return juce::isPositiveAndBelow (i, list.size()) ? list[i] : nullptr;
}

void SourcePicker::remember() { if (settingsKey.isNotEmpty()) Settings::get().set (settingsKey, getText()); }

void MidiPicker::refresh()
{
    list = outs ? juce::MidiOutput::getAvailableDevices() : juce::MidiInput::getAvailableDevices();
    clear (juce::dontSendNotification);
    if (list.isEmpty()) { addItem (outs ? "(no MIDI outputs)" : "(no MIDI inputs)", 1); setSelectedId (1, juce::dontSendNotification); setEnabled (false); return; }
    setEnabled (true);
    for (int i = 0; i < list.size(); ++i) addItem (list[i].name, i + 1);
    auto want = Settings::get().str (settingsKey);
    for (int i = 0; i < list.size(); ++i) if (list[i].name == want) { setSelectedId (i + 1, juce::dontSendNotification); return; }
    // nothing saved yet: a real synth, not Windows' built-in GM synth or a controller's own control port
    for (int i = 0; i < list.size(); ++i) if (! isUtilityPort (list[i].name)) { setSelectedId (i + 1, juce::dontSendNotification); return; }
    setSelectedId (1, juce::dontSendNotification);
}

bool MidiPicker::isUtilityPort (const juce::String& n)
{
    return n.containsIgnoreCase ("Microsoft GS") || n.containsIgnoreCase ("Wavetable Synth")
        || (n.containsIgnoreCase ("MPC") && (n.containsIgnoreCase ("Public") || n.containsIgnoreCase ("Private") || n.containsIgnoreCase ("DAW")))
        || n.containsIgnoreCase ("StreamDeck") || n.containsIgnoreCase ("MackieControl") || n.containsIgnoreCase ("loopMIDI");
}

juce::String MidiPicker::id() const
{
    int i = getSelectedId() - 1;
    return juce::isPositiveAndBelow (i, list.size()) ? list[i].identifier : juce::String();
}

void MidiPicker::remember() { if (settingsKey.isNotEmpty()) Settings::get().set (settingsKey, getText()); }

void fillNotes (juce::ComboBox& c, int lo, int hi)
{
    c.clear (juce::dontSendNotification);
    for (int n = lo; n <= hi; ++n) c.addItem (noteName (n), n + 1);
}

int flowLayout (juce::Rectangle<int> r, std::initializer_list<std::pair<juce::Component*, int>> items, int rowHeight, int gap, bool place)
{
    int x = r.getX(), y = r.getY();
    for (auto& [c, w] : items)
    {
        if (c == nullptr) continue;
        const int cw = juce::jmin (w, r.getWidth());
        if (x > r.getX() && x + cw > r.getRight()) { x = r.getX(); y += rowHeight + gap; }
        if (place) c->setBounds (x, y, cw, rowHeight);
        x += cw + gap;
    }
    return y + rowHeight - r.getY();
}

void rowLayout (juce::Rectangle<int> r, std::initializer_list<std::pair<juce::Component*, int>> items, int gap)
{
    // when the row is wider than the space (narrow windows), everything shrinks by the same share instead of
    // the last items being squeezed to nothing
    int total = 0, n = 0;
    for (auto& [c, w] : items) if (c != nullptr) { total += w; ++n; }
    const int room = r.getWidth() - gap * juce::jmax (0, n - 1);
    const double k = total > room && total > 0 ? juce::jmax (0.0, (double) room / total) : 1.0;
    for (auto& [c, w] : items)
    {
        if (c == nullptr) continue;
        c->setBounds (r.removeFromLeft ((int) std::floor (w * k)));
        r.removeFromLeft (gap);
    }
}

// ------------------------------------------------------------ main component

struct NavInfo { const char* id; const char* label; };
static const NavInfo kNav[] = {
    { "meters", "Meters" }, { "record", "Record" }, { "sampler", "Auto-sampler" }, { "backup", "Patch backup" },
    { "gear", "Gear check" }, { "chop", "Chop station" }, { "library", "Sample library" }, { "waves", "Wavetables" },
    { "mangle", "Mangle" }, { "riffs", "Pattern generator" }, { "books", "Patch books" }, { "video", "Video" }, { "help", "Tutorial" }, { "settings", "Settings" } };

MainComponent::MainComponent (Engine& e) : engine (e)
{
    std::unique_ptr<Page> (*makers[])(Engine&) = { makeMetersPage, makeRecordPage, makeSamplerPage, makeBackupPage, makeGearCheckPage,
                                                  makeChopPage, makeLibraryPage, makeWavesPage, makeManglePage, makeRiffsPage,
                                                  makeBooksPage, makeVideoPage, makeHelpPage, makeSettingsPage };
    for (auto* mk : makers)
    {
        auto p = mk (engine);
        p->main = this;
        addChildComponent (p.get());
        pages.add (p.release());
    }
    for (auto& n : kNav)
    {
        auto* b = nav.add (new juce::TextButton (n.label));
        juce::String id (n.id);
        b->setRadioGroupId (1);
        b->setClickingTogglesState (true);
        b->onClick = [this, id] { show (id); };
        addAndMakeVisible (b);
    }
    engine.onSourcesChanged = [this] {
        for (auto* p : pages) p->shown();
        repaint();
    };
    show (Settings::get().str ("lastPage", "meters"));
    startTimerHz (30);
}

MainComponent::~MainComponent() { engine.onSourcesChanged = nullptr; }

Page* MainComponent::page (const juce::String& id)
{
    for (auto* p : pages) if (p->id == id) return p;
    return nullptr;
}

void MainComponent::show (const juce::String& id)
{
    auto* p = page (id);
    if (p == nullptr) p = pages[0];
    if (current != nullptr) current->setVisible (false);
    if (current != p) { engine.setGenerator (nullptr); engine.stopPlayback(); }   // previews belong to the page that started them
    current = p;
    current->setVisible (true);
    current->shown();
    for (int i = 0; i < nav.size(); ++i) nav[i]->setToggleState (juce::String (kNav[i].id) == current->id, juce::dontSendNotification);
    Settings::get().set ("lastPage", current->id);
    resized();
    repaint();
}

void MainComponent::sendAudio (const juce::String& id, const juce::AudioBuffer<float>& b, double sr, const juce::String& name)
{
    if (auto* p = page (id)) { p->receiveAudio (b, sr, name); show (id); }
}

void MainComponent::timerCallback()
{
    if (current != nullptr) current->tick();
    auto* d = engine.devices.getCurrentAudioDevice();
    juce::String s = d != nullptr ? d->getName() + "  " + juce::String (d->getCurrentSampleRate() / 1000.0, 1) + " kHz" : juce::String ("no audio device");
    s << "    " << engine.sources().size() << " sources";
    int lc = engine.linkCount();
    if (lc > 0) s << "    Link: " << lc;
    s << "    jam buffer " << engine.jamMinutes() << " min";
    if (s != status) { status = s; repaint (getLocalBounds().removeFromTop (70)); }
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
    // brushed-metal-ish sidebar slab
    auto side = getLocalBounds().removeFromLeft (196).toFloat();
    drawSlab (g, side.reduced (8, 8), 14, true, 1.4f);
    drawEngraved (g, "BC", side.toNearestInt().reduced (26, 22).withHeight (22), juce::Justification::left, font (19, 2), col::text);
    drawEngraved (g, "TOOLKIT", side.toNearestInt().reduced (26, 44).withHeight (16), juce::Justification::left, font (11, 2), col::text3);
    if (current != nullptr)
    {
        auto head = getLocalBounds().withTrimmedLeft (196).removeFromTop (70).reduced (22, 12);
        g.setFont (font (26, 2)); g.setColour (col::text);
        g.drawText (current->title, head.removeFromTop (30), juce::Justification::left);
        g.setFont (font (13)); g.setColour (col::text3);
        g.drawText (current->blurb, head, juce::Justification::left);
        g.setFont (monoFont (11)); g.setColour (col::text3);
        g.drawText (status, getLocalBounds().withTrimmedLeft (196).removeFromTop (34).reduced (22, 10), juce::Justification::right);
    }
}

void MainComponent::resized()
{
    auto r = getLocalBounds();
    auto side = r.removeFromLeft (196).reduced (20, 0);
    side.removeFromTop (80);
    for (auto* b : nav) { b->setBounds (side.removeFromTop (40)); side.removeFromTop (2); }
    r.removeFromTop (70);
    if (current != nullptr) current->setBounds (r.reduced (10, 0).withTrimmedBottom (10));
}

void MainComponent::renderAll (const juce::File& dir, int w, int h)
{
    dir.createDirectory();
    setSize (w, h);
    int i = 1;
    for (auto& n : kNav)
    {
        show (n.id);
        current->demo();
        for (int t = 0; t < 40; ++t) { engine.runDemo (0.03); current->tick(); }
        auto img = createComponentSnapshot (getLocalBounds(), true, 1.0f);
        juce::File f = dir.getChildFile (juce::String (i++).paddedLeft ('0', 2) + "-" + n.id + ".png");
        f.deleteFile();
        juce::FileOutputStream os (f);
        juce::PNGImageFormat().writeImageToStream (img, os);
    }
}

static void writePng (const juce::Image& img, const juce::File& f)
{
    f.getParentDirectory().createDirectory();
    f.deleteFile();
    juce::FileOutputStream os (f);
    juce::PNGImageFormat().writeImageToStream (img, os);
}

/** the tab-like buttons on a page (radio groups), grouped */
static void findTabs (juce::Component& c, std::map<int, juce::Array<juce::Button*>>& groups)
{
    for (auto* ch : c.getChildren())
    {
        if (auto* b = dynamic_cast<juce::Button*> (ch)) { if (b->getRadioGroupId() != 0 && b->isVisible()) groups[b->getRadioGroupId()].add (b); continue; }
        findTabs (*ch, groups);
    }
}

juce::StringArray MainComponent::checkAll (const juce::File& dir)
{
    struct Size { const char* name; int w, h; bool saveAll; };
    // the window can't go below 1100 x 700 (about 1100 x 660 inside the title bar); tall = a vertical monitor
    const Size sizes[] = { { "wide", 1600, 1000, true }, { "min", 1100, 660, true }, { "tall", 1100, 1500, false } };
    juce::StringArray problems, report;
    int checks = 0;
    for (auto& sz : sizes)
    {
        setSize (sz.w, sz.h);
        int pi = 1;
        for (auto& n : kNav)
        {
            show (n.id);
            current->demo();
            for (int t = 0; t < 20; ++t) { engine.runDemo (0.03); current->tick(); }
            const juce::String pageName = juce::String (pi++).paddedLeft ('0', 2) + "-" + n.id;

            auto look = [&] (const juce::String& view)
            {
                current->resized();
                for (int t = 0; t < 4; ++t) { engine.runDemo (0.03); current->tick(); }
                auto found = findLayoutProblems (*this);
                ++checks;
                const juce::String where = juce::String (sz.name) + " " + juce::String (sz.w) + "x" + juce::String (sz.h) + "  " + n.label + (view.isNotEmpty() ? " / " + view : juce::String());
                auto controls = listControls (*current);
                report.add ((found.isEmpty() ? "PASS  " : "FAIL  ") + where + "   (" + juce::String (controls.size()) + " controls)");
                for (auto& f : found) { report.add ("        " + f); problems.add (where + ": " + f); }
                if (sz.saveAll || ! found.isEmpty())
                {
                    auto file = juce::String (sz.name) + "/" + pageName + (view.isNotEmpty() ? "-" + view.toLowerCase().retainCharacters ("abcdefghijklmnopqrstuvwxyz0123456789 ").trim().replaceCharacter (' ', '-') : juce::String()) + ".png";
                    writePng (createComponentSnapshot (getLocalBounds(), true, 1.0f), dir.getChildFile (file));
                }
            };

            std::map<int, juce::Array<juce::Button*>> groups;
            findTabs (*current, groups);
            if (groups.empty()) { look ({}); continue; }
            bool first = true;
            for (auto& [id, btns] : groups)
            {
                for (auto* b : btns)
                {
                    if (! first && b == btns.getFirst()) continue;   // the first tab was already seen with the page
                    b->setToggleState (true, juce::dontSendNotification);
                    if (b->onClick) b->onClick();
                    look (b->getButtonText());
                    first = false;
                }
                btns.getFirst()->setToggleState (true, juce::dontSendNotification);
                if (btns.getFirst()->onClick) btns.getFirst()->onClick();
            }
        }
    }
    report.insert (0, juce::String (checks) + " views checked, " + juce::String (problems.size()) + " problems\n");
    dir.createDirectory();
    dir.getChildFile ("layout-check.txt").replaceWithText (report.joinIntoString ("\n") + "\n");
    return problems;
}

// ------------------------------------------------------------ window

MainWindow::MainWindow (Engine& e) : juce::DocumentWindow ("BC Toolkit", col::bg, juce::DocumentWindow::allButtons)
{
    juce::LookAndFeel::setDefaultLookAndFeel (&look);
    setUsingNativeTitleBar (true);
    setContentOwned (new MainComponent (e), false);
    setResizable (true, true);
    setResizeLimits (1100, 700, 4000, 3000);
    centreWithSize (1480, 920);
    setVisible (true);
}

MainWindow::~MainWindow()
{
    clearContentComponent();   // pages go before the look they paint with
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
}

void MainWindow::closeButtonPressed() { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
}
