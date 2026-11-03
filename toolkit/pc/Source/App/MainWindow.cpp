#include "MainWindow.h"
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
    setSelectedId (1, juce::dontSendNotification);
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

void rowLayout (juce::Rectangle<int> r, std::initializer_list<std::pair<juce::Component*, int>> items, int gap)
{
    for (auto& [c, w] : items)
    {
        if (c != nullptr) c->setBounds (r.removeFromLeft (w));
        r.removeFromLeft (gap);
    }
}

// ------------------------------------------------------------ main component

struct NavInfo { const char* id; const char* label; };
static const NavInfo kNav[] = {
    { "meters", "Meters" }, { "record", "Record" }, { "sampler", "Auto-sampler" }, { "backup", "Patch backup" },
    { "gear", "Gear check" }, { "chop", "Chop station" }, { "library", "Sample library" }, { "waves", "Wavetables" },
    { "mangle", "Mangle" }, { "riffs", "Pattern generator" }, { "books", "Patch books" }, { "help", "Tutorial" }, { "settings", "Settings" } };

MainComponent::MainComponent (Engine& e) : engine (e)
{
    std::unique_ptr<Page> (*makers[])(Engine&) = { makeMetersPage, makeRecordPage, makeSamplerPage, makeBackupPage, makeGearCheckPage,
                                                  makeChopPage, makeLibraryPage, makeWavesPage, makeManglePage, makeRiffsPage,
                                                  makeBooksPage, makeHelpPage, makeSettingsPage };
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
    if (current != p) engine.setGenerator (nullptr);   // preview synths belong to the page that started them
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
