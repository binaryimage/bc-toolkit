#include "Pages.h"

namespace tk
{
namespace
{
struct BookDef { const char* id; const char* title; const char* file; };
const BookDef kBooks[] = {
    { "2600", "Behringer 2600", "patchbook-2600.json" },
    { "messenger", "Moog Messenger", "messenger-patches.json" },
    { "hydrasynth", "Hydrasynth", "hydrasynth-patches.json" },
    { "leviasynth", "Leviasynth", "leviasynth-patches.json" },
    { "minifreak", "MiniFreak", "minifreak-patches.json" },
};

juce::String resourceText (const char* original)
{
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
        if (juce::String (BinaryData::getNamedResourceOriginalFilename (BinaryData::namedResourceList[i])) == original)
        {
            int size = 0;
            auto* d = BinaryData::getNamedResource (BinaryData::namedResourceList[i], size);
            return juce::String::fromUTF8 (d, size);
        }
    return {};
}

juce::String arrow() { return juce::String (juce::CharPointer_UTF8 ("  \xe2\x86\x92  ")); }

struct Patch
{
    juce::String name, category, play, seq, adapt, switches, source, sourceName;
    std::vector<std::pair<juce::String, juce::String>> settings;   // "Section / Param" -> value
    std::vector<std::pair<juce::String, double>> faders;           // 2600 only
    juce::StringArray cables;
    bool user = false;

    static Patch fromVar (const juce::var& v, bool user)
    {
        Patch p;
        p.user = user;
        p.name = v["name"].toString(); p.category = v["category"].toString();
        p.play = v["play"].toString(); p.seq = v["seq"].toString(); p.adapt = v["adapt"].toString();
        p.switches = v["switches"].toString(); p.source = v["source"].toString(); p.sourceName = v["sourceName"].toString();
        if (auto* a = v["settings"].getArray())
            for (auto& s : *a) if (s.isArray() && s.size() >= 2) p.settings.push_back ({ s[0].toString(), s[1].toString() });
        if (auto* o = v["faders"].getDynamicObject())
            for (auto& pr : o->getProperties()) p.faders.push_back ({ pr.name.toString(), (double) pr.value });
        if (auto* a = v["cables"].getArray())
            for (auto& c : *a) p.cables.add (c["from"].toString() + arrow() + c["to"].toString());
        return p;
    }

    juce::var toVar() const
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("name", name); o->setProperty ("category", category); o->setProperty ("play", play);
        juce::Array<juce::var> s;
        for (auto& [k, v] : settings) s.add (juce::Array<juce::var> { k, v });
        o->setProperty ("settings", s);
        if (! faders.empty())
        {
            auto* f = new juce::DynamicObject();
            for (auto& [k, v] : faders) f->setProperty (k, v);
            o->setProperty ("faders", juce::var (f));
        }
        juce::Array<juce::var> c;
        for (auto& line : cables)
        {
            auto* co = new juce::DynamicObject();
            co->setProperty ("from", line.upToFirstOccurrenceOf (arrow(), false, false).trim());
            co->setProperty ("to", line.fromFirstOccurrenceOf (arrow(), false, false).trim());
            c.add (juce::var (co));
        }
        if (! c.isEmpty()) o->setProperty ("cables", c);
        if (switches.isNotEmpty()) o->setProperty ("switches", switches);
        return juce::var (o);
    }
};

juce::File userFile (const juce::String& book) { return appDataFolder().getChildFile ("Patch books").getChildFile ("my-" + book + ".json"); }

/** The patch sheet itself. Paints a 2600 front panel as 3D faders, or grouped setting cards for the others. */
class Sheet : public juce::Component
{
public:
    const Patch* patch = nullptr;
    juce::String bookTitle;
    bool forExport = false;

    int layout (int width)
    {
        setSize (width, measure (width));
        return getHeight();
    }

    int measure (int w)
    {
        if (patch == nullptr) return 200;
        juce::Graphics* none = nullptr;
        return paintInto (none, (float) w);
    }

    void paint (juce::Graphics& g) override
    {
        if (forExport) g.fillAll (col::bg);
        juce::Graphics* gp = &g;
        paintInto (gp, (float) getWidth());
    }

private:
    static std::vector<std::pair<juce::String, std::vector<int>>> groups (const std::vector<juce::String>& keys)
    {
        std::vector<std::pair<juce::String, std::vector<int>>> out;
        for (int i = 0; i < (int) keys.size(); ++i)
        {
            auto sec = keys[(size_t) i].upToFirstOccurrenceOf (" / ", false, false);
            if (out.empty() || out.back().first != sec) out.push_back ({ sec, {} });
            out.back().second.push_back (i);
        }
        return out;
    }

    float text (juce::Graphics* g, const juce::String& s, float x, float y, float w, float size, juce::Colour c, int weight = 0)
    {
        juce::AttributedString a;
        a.append (s, font (size, weight), c);
        a.setWordWrap (juce::AttributedString::byWord);
        juce::TextLayout tl; tl.createLayout (a, w);
        if (g != nullptr) tl.draw (*g, { x, y, w, tl.getHeight() });
        return tl.getHeight();
    }

    int paintInto (juce::Graphics* g, float W)
    {
        const auto& p = *patch;
        float x = 24, y = 18, w = W - 48;
        // header
        if (g) { g->setColour (col::text3); g->setFont (font (12, 1)); g->drawText (bookTitle.toUpperCase() + "   /   " + p.category.toUpperCase() + (p.user ? "   /   MINE" : ""), (int) x, (int) y, (int) w, 16, juce::Justification::left); }
        y += 22;
        y += text (g, p.name, x, y, w, 30, col::white, 2) + 12;

        if (! p.faders.empty())
        {
            std::vector<juce::String> keys;
            for (auto& f : p.faders) keys.push_back (f.first);
            auto gs = groups (keys);
            const float fw = 46, fh = 150, gap = 18;
            float cx = x, rowH = fh + 64;
            for (auto& [sec, idx] : gs)
            {
                float gw = idx.size() * fw + 16;
                if (cx + gw > x + w) { cx = x; y += rowH + 10; }
                auto box = juce::Rectangle<float> (cx, y, gw, rowH);
                if (g)
                {
                    drawSlab (*g, box, 8);
                    g->setFont (font (10.5f, 1)); g->setColour (col::text3);
                    g->drawText (sec.toUpperCase(), box.reduced (8, 6).toNearestInt(), juce::Justification::topLeft);
                    for (size_t k = 0; k < idx.size(); ++k)
                    {
                        auto [name, v] = p.faders[(size_t) idx[k]];
                        float fx = cx + 8 + k * fw, fy = y + 26;
                        auto slot = juce::Rectangle<float> (fx + fw / 2 - 4, fy, 8, fh - 30);
                        drawScreen (*g, slot, 4);
                        for (int t = 0; t <= 10; ++t)
                        {
                            float ty = slot.getBottom() - 6 - t * (slot.getHeight() - 12) / 10;
                            g->setColour (col::text3.withAlpha (t % 5 == 0 ? 0.8f : 0.4f));
                            g->drawHorizontalLine ((int) ty, slot.getX() - (t % 5 == 0 ? 9.0f : 6.0f), slot.getX() - 2);
                        }
                        float cy = slot.getBottom() - 6 - (float) juce::jlimit (0.0, 10.0, v) * (slot.getHeight() - 12) / 10;
                        auto cap = juce::Rectangle<float> (fx + 8, cy - 9, fw - 16, 18);
                        drawKey (*g, cap, v > 0, false, false, 3);
                        g->setColour (v > 0 ? juce::Colours::black : col::text3); g->fillRect (cap.withSizeKeepingCentre (cap.getWidth() - 6, 1.5f));
                        g->setFont (font (11, 3)); g->setColour (col::white);
                        g->drawText (juce::String (v, v == std::floor (v) ? 0 : 1), (int) fx, (int) (fy + fh - 28), (int) fw, 14, juce::Justification::centred);
                        g->setFont (font (9.5f)); g->setColour (col::text2);
                        g->drawFittedText (name.fromFirstOccurrenceOf (" / ", false, false), (int) fx, (int) (fy + fh - 13), (int) fw, 24, juce::Justification::centredTop, 2, 0.7f);
                    }
                }
                cx += gw + gap;
            }
            y += rowH + 16;
            if (! p.cables.isEmpty())
            {
                if (g) { g->setFont (font (12, 1)); g->setColour (col::text3); g->drawText ("CABLES", (int) x, (int) y, 200, 16, juce::Justification::left); }
                y += 22;
                for (auto& c : p.cables)
                {
                    if (g)
                    {
                        drawKey (*g, { x, y + 4, 10, 10 }, true, false, false, 5);
                    }
                    y += text (g, c, x + 20, y, w - 20, 14.5f, col::text) + 6;
                }
                y += 8;
            }
        }
        else if (! p.settings.empty())
        {
            std::vector<juce::String> keys;
            for (auto& s : p.settings) keys.push_back (s.first);
            auto gs = groups (keys);
            const int cols = juce::jmax (1, (int) (w / 300));
            const float cw = (w - (cols - 1) * 12) / cols;
            std::vector<float> colY ((size_t) cols, y);
            for (auto& [sec, idx] : gs)
            {
                size_t c = (size_t) (std::min_element (colY.begin(), colY.end()) - colY.begin());
                float bx = x + c * (cw + 12), by = colY[c];
                float h = 32;
                std::vector<float> rh;
                for (int i : idx)
                {
                    float a = text (nullptr, p.settings[(size_t) i].second, 0, 0, cw * 0.5f - 12, 14, col::white, 1);
                    rh.push_back (juce::jmax (22.0f, a + 6)); h += rh.back();
                }
                h += 8;
                if (g)
                {
                    drawSlab (*g, { bx, by, cw, h }, 8);
                    g->setFont (font (10.5f, 1)); g->setColour (col::text3);
                    g->drawText (sec.toUpperCase(), (int) bx + 12, (int) by + 9, (int) cw - 24, 14, juce::Justification::left);
                }
                float ry = by + 30;
                for (size_t k = 0; k < idx.size(); ++k)
                {
                    auto& [key, val] = p.settings[(size_t) idx[k]];
                    auto param = key.contains (" / ") ? key.fromFirstOccurrenceOf (" / ", false, false) : key;
                    bool est = val.containsIgnoreCase ("est.");
                    if (g)
                    {
                        g->setFont (font (13)); g->setColour (col::text2);
                        g->drawText (param, (int) bx + 12, (int) ry, (int) (cw * 0.5f) - 12, 20, juce::Justification::centredLeft);
                    }
                    text (g, val, bx + cw * 0.5f, ry + 2, cw * 0.5f - 12, 14, est ? col::text2 : col::white, 1);
                    ry += rh[k];
                }
                colY[c] = by + h + 12;
            }
            y = *std::max_element (colY.begin(), colY.end()) + 4;
        }

        auto para = [&] (const juce::String& head, const juce::String& body) {
            if (body.trim().isEmpty()) return;
            if (g) { g->setFont (font (12, 1)); g->setColour (col::text3); g->drawText (head, (int) x, (int) y, (int) w, 16, juce::Justification::left); }
            y += 20;
            y += text (g, body, x, y, juce::jmin (w, 980.0f), 15, col::text) + 14;
        };
        para ("SWITCHES", p.switches);
        para ("HOW TO PLAY IT", p.play);
        para ("SEQUENCER / ARP", p.seq);
        para ("ADAPTED FROM THE ORIGINAL", p.adapt);
        if (p.sourceName.isNotEmpty() || p.source.isNotEmpty())
        {
            y += text (g, p.sourceName + (p.source.isNotEmpty() ? "\n" + p.source : juce::String()), x, y, w, 12, col::text3) + 10;
        }
        y += text (g, "Values marked est. are best guesses from the source: trust your ears.", x, y, w, 11.5f, col::text3) + 16;
        return (int) y;
    }
};

class BooksPage : public Page, private juce::ListBoxModel
{
public:
    BooksPage (Engine& e) : Page (e, "books", "Patch books",
                                  "147 dark-leaning patches for the 2600, Messenger, Hydrasynth, Leviasynth and MiniFreak, plus your own.")
    {
        addAndMakeVisible (side); side.setTitle ("Books");
        for (int b = 0; b < 5; ++b)
        {
            auto btn = makeButton (kBooks[b].title, [this, b] { setBook (b); }, true);
            btn->setRadioGroupId (21);
            side.addAndMakeVisible (*btn); bookBtns.push_back (std::move (btn));
        }
        search.setTextToShowWhenEmpty ("Search names and settings", col::text3);
        search.onTextChange = [this] { filter(); };
        side.addAndMakeVisible (search);
        category.onChange = [this] { filter(); };
        styleCombo (category); side.addAndMakeVisible (category);
        list.setModel (this);
        list.setRowHeight (40);
        list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        side.addAndMakeVisible (list);
        mineBtn = makeButton ("+ My patch", [this] { editPatch (-1); });
        side.addAndMakeVisible (*mineBtn);

        addAndMakeVisible (main_); main_.setTitle ("Sheet");
        viewport.setViewedComponent (&sheet, false);
        viewport.setScrollBarsShown (true, false);
        main_.addAndMakeVisible (viewport);
        pngBtn = makeButton ("Save sheet as PNG", [this] { savePng(); });
        srcBtn = makeButton ("Open source", [this] { if (auto* p = cur()) juce::URL (p->source).launchInDefaultBrowser(); });
        editBtn = makeButton ("Edit", [this] { if (auto* p = cur(); p && p->user) editPatch (shown[(size_t) sel]); });
        copyBtn = makeButton ("Copy to mine", [this] { copyToMine(); });
        delBtn = makeButton ("Delete", [this] { deletePatch(); });
        for (auto* b : { pngBtn.get(), srcBtn.get(), editBtn.get(), copyBtn.get(), delBtn.get() }) main_.addAndMakeVisible (b);

        setBook (juce::jlimit (0, 4, Settings::get().num ("book", 0)));
    }

    void setBook (int b)
    {
        book = b;
        Settings::get().set ("book", b);
        for (int i = 0; i < 5; ++i) bookBtns[(size_t) i]->setToggleState (i == b, juce::dontSendNotification);
        patches.clear();
        auto parsed = juce::JSON::parse (resourceText (kBooks[b].file));
        if (auto* a = parsed.getArray()) for (auto& v : *a) patches.push_back (Patch::fromVar (v, false));
        auto mine = juce::JSON::parse (userFile (kBooks[b].id));
        if (auto* a = mine.getArray()) for (auto& v : *a) patches.push_back (Patch::fromVar (v, true));
        juce::StringArray cats;
        for (auto& p : patches) cats.addIfNotAlreadyThere (p.category);
        cats.sort (true);
        category.clear (juce::dontSendNotification);
        category.addItem ("Every category", 1);
        category.addItem ("Mine", 2);
        category.addItemList (cats, 3);
        category.setSelectedId (1, juce::dontSendNotification);
        sheet.bookTitle = kBooks[b].title;
        filter();
    }

    void filter()
    {
        shown.clear();
        auto q = search.getText().trim();
        auto cat = category.getSelectedId() > 2 ? category.getText() : juce::String();
        for (int i = 0; i < (int) patches.size(); ++i)
        {
            auto& p = patches[(size_t) i];
            if (category.getSelectedId() == 2 && ! p.user) continue;
            if (cat.isNotEmpty() && p.category != cat) continue;
            if (q.isNotEmpty())
            {
                bool hit = p.name.containsIgnoreCase (q) || p.play.containsIgnoreCase (q);
                for (auto& [k, v] : p.settings) hit = hit || v.containsIgnoreCase (q) || k.containsIgnoreCase (q);
                if (! hit) continue;
            }
            shown.push_back (i);
        }
        list.updateContent();
        side.setTitle (juce::String (kBooks[book].title) + "  /  " + juce::String ((int) shown.size()) + " of " + juce::String ((int) patches.size()));
        select (shown.empty() ? -1 : 0);
    }

    Patch* cur() { return sel >= 0 && sel < (int) shown.size() ? &patches[(size_t) shown[(size_t) sel]] : nullptr; }

    void select (int row)
    {
        sel = row;
        list.selectRow (row, false, true);
        sheet.patch = cur();
        sheet.layout (viewport.getMaximumVisibleWidth() > 100 ? viewport.getMaximumVisibleWidth() : 900);
        sheet.repaint();
        viewport.setViewPosition (0, 0);
        auto* p = cur();
        srcBtn->setEnabled (p && p->source.startsWith ("http"));
        editBtn->setEnabled (p && p->user);
        delBtn->setEnabled (p && p->user);
        copyBtn->setEnabled (p && ! p->user);
    }

    int getNumRows() override { return (int) shown.size(); }
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
    {
        if (row >= (int) shown.size()) return;
        auto& p = patches[(size_t) shown[(size_t) row]];
        auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (2, 2);
        drawKey (g, r, selected, false, false, 6);
        auto ink = selected ? juce::Colours::black : col::text;
        g.setColour (ink); g.setFont (font (14, 1));
        g.drawText (p.name, r.reduced (12, 0).toNearestInt(), juce::Justification::centredLeft);
        g.setColour (selected ? juce::Colour (0xff404040) : col::text3); g.setFont (font (11));
        g.drawText (p.category + (p.user ? "  *" : ""), r.reduced (12, 0).toNearestInt(), juce::Justification::centredRight);
    }
    void selectedRowsChanged (int row) override { if (row != sel && row >= 0) select (row); }

    // ------------------------------------------------------------ user patches
    void saveMine()
    {
        juce::Array<juce::var> a;
        for (auto& p : patches) if (p.user) a.add (p.toVar());
        auto f = userFile (kBooks[book].id);
        f.getParentDirectory().createDirectory();
        f.replaceWithText (juce::JSON::toString (juce::var (a)));
    }

    static juce::String toLines (const Patch& p)
    {
        juce::String s;
        for (auto& [k, v] : p.faders) s << k << " = " << juce::String (v) << "\n";
        for (auto& [k, v] : p.settings) s << k << " = " << v << "\n";
        for (auto& c : p.cables) s << "cable: " << c.replace (arrow(), " -> ") << "\n";
        return s;
    }

    void copyToMine()
    {
        auto* p = cur(); if (! p) return;
        Patch c = *p; c.user = true; c.name = p->name + " (mine)"; c.source = {}; c.sourceName = "Copied from " + p->name;
        patches.push_back (c);
        saveMine();
        int idx = (int) patches.size() - 1;
        setBookKeepSelection (idx);
        editPatch (idx);
    }

    void setBookKeepSelection (int patchIndex)
    {
        auto keepName = patches[(size_t) patchIndex].name;
        setBook (book);
        for (int r = 0; r < (int) shown.size(); ++r) if (patches[(size_t) shown[(size_t) r]].user && patches[(size_t) shown[(size_t) r]].name == keepName) { select (r); break; }
    }

    void deletePatch()
    {
        auto* p = cur(); if (! p || ! p->user) return;
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::WarningIcon, "Delete " + p->name + "?", "This only removes your own copy.", "Delete", "Keep", nullptr,
            juce::ModalCallbackFunction::create ([this, idx = shown[(size_t) sel]] (int r) {
                if (r != 1) return;
                patches.erase (patches.begin() + idx);
                saveMine(); setBook (book);
            }));
    }

    /** Small editor: name, category, one "Section / Param = value" per line, and notes. */
    void editPatch (int idx)
    {
        Patch p;
        if (idx >= 0) p = patches[(size_t) idx];
        else
        {
            p.user = true; p.name = "New patch"; p.category = "Bass";
            p.settings = { { "Osc 1 / Wave", "Saw" }, { "Filter / Cutoff", "40" }, { "Amp env / Decay", "60" } };
            if (book == 0) { p.settings.clear(); p.faders = { { "VCO 1 / Coarse tune", 3 }, { "VCF / Cutoff", 4 }, { "ADSR / Decay", 3 } }; }
        }
        auto* w = new juce::AlertWindow (idx >= 0 ? "Edit patch" : "New patch", "One setting per line: Section / Param = value.  On the 2600, numbers become faders and 'cable: A -> B' adds a cable.", juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("name", p.name, "Name");
        w->addTextEditor ("cat", p.category, "Category");
        w->addTextBlock ("Settings");
        auto* ed = new juce::TextEditor();
        ed->setMultiLine (true); ed->setReturnKeyStartsNewLine (true); ed->setSize (640, 300); ed->setText (toLines (p));
        ed->setFont (monoFont (13));
        w->addCustomComponent (ed);
        w->addTextEditor ("play", p.play, "How to play it");
        w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey, juce::ModifierKeys::commandModifier, 0));
        w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, ed, idx, p] (int r) mutable {
            std::unique_ptr<juce::TextEditor> owned (ed);
            if (r == 1)
            {
                p.name = w->getTextEditorContents ("name").trim(); p.category = w->getTextEditorContents ("cat").trim();
                p.play = w->getTextEditorContents ("play");
                p.settings.clear(); p.faders.clear(); p.cables.clear();
                for (auto line : juce::StringArray::fromLines (ed->getText()))
                {
                    line = line.trim();
                    if (line.isEmpty()) continue;
                    if (line.startsWithIgnoreCase ("cable:")) { p.cables.add (line.fromFirstOccurrenceOf (":", false, false).trim().replace ("->", arrow())); continue; }
                    auto k = line.upToFirstOccurrenceOf ("=", false, false).trim(), v = line.fromFirstOccurrenceOf ("=", false, false).trim();
                    if (book == 0 && v.containsOnly ("0123456789.")) p.faders.push_back ({ k, v.getDoubleValue() });
                    else p.settings.push_back ({ k, v });
                }
                if (idx >= 0) patches[(size_t) idx] = p; else patches.push_back (p);
                saveMine();
                setBookKeepSelection (idx >= 0 ? idx : (int) patches.size() - 1);
            }
            delete w;
        }), false);
    }

    void savePng()
    {
        auto* p = cur(); if (! p) return;
        Sheet s; s.patch = p; s.bookTitle = kBooks[book].title; s.forExport = true;
        s.layout (1100);
        auto img = s.createComponentSnapshot (s.getLocalBounds(), true, 2.0f);
        auto dir = musicFolder().getChildFile ("Toolkit Patch Sheets");
        dir.createDirectory();
        auto f = dir.getChildFile (safeName (juce::String (kBooks[book].title) + " - " + p->name) + ".png");
        f.deleteFile();
        juce::FileOutputStream os (f);
        juce::PNGImageFormat().writeImageToStream (img, os);
        os.flush();
        f.revealToUser();
    }

    void resized() override
    {
        auto r = getLocalBounds();
        side.setBounds (r.removeFromLeft (360));
        r.removeFromLeft (10);
        main_.setBounds (r);
        auto s = side.content();
        for (int i = 0; i < 5; ++i)
        {
            auto row = s.removeFromTop (40);
            bookBtns[(size_t) i]->setBounds (row);
            s.removeFromTop (2);
        }
        s.removeFromTop (8);
        search.setBounds (s.removeFromTop (32)); s.removeFromTop (6);
        category.setBounds (s.removeFromTop (32)); s.removeFromTop (8);
        mineBtn->setBounds (s.removeFromBottom (44)); s.removeFromBottom (6);
        list.setBounds (s);
        auto m = main_.content();
        rowLayout (m.removeFromTop (40), { { pngBtn.get(), 170 }, { srcBtn.get(), 130 }, { copyBtn.get(), 140 }, { editBtn.get(), 80 }, { delBtn.get(), 90 } });
        m.removeFromTop (6);
        viewport.setBounds (m);
        sheet.layout (viewport.getMaximumVisibleWidth());
    }

    void demo() override
    {
        setBook (0);
        for (int r = 0; r < (int) shown.size(); ++r)
            if (patches[(size_t) shown[(size_t) r]].name == "Iron Lung") select (r);
    }

    Slab side, main_;
    std::vector<std::unique_ptr<juce::TextButton>> bookBtns;
    std::unique_ptr<juce::TextButton> mineBtn, pngBtn, srcBtn, editBtn, copyBtn, delBtn;
    juce::TextEditor search;
    juce::ComboBox category;
    juce::ListBox list;
    juce::Viewport viewport;
    Sheet sheet;
    std::vector<Patch> patches;
    std::vector<int> shown;
    int book = 0, sel = -1;
};
}

std::unique_ptr<Page> makeBooksPage (Engine& e) { return std::make_unique<BooksPage> (e); }
}
