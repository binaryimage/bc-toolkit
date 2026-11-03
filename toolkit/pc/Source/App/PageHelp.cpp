#include "Pages.h"
#include "MainWindow.h"

namespace tk
{
namespace
{
/** Renders Resources/tutorial.md: # / ## / ### headings, "- " bullets, **bold**. */
class Doc : public juce::Component
{
public:
    struct Block { int kind; juce::String text; std::vector<juce::StringArray> rows; };   // 1..3 heading, 4 bullet, 5 numbered, 6 table, 0 paragraph
    std::vector<Block> blocks;
    std::vector<std::pair<juce::String, int>> anchors;   // section title -> y

    void parse (const juce::String& md)
    {
        blocks.clear();
        juce::String para;
        auto flush = [&] { if (para.trim().isNotEmpty()) blocks.push_back ({ 0, para.trim() }); para.clear(); };
        for (auto line : juce::StringArray::fromLines (md))
        {
            if (line.startsWith ("|"))
            {
                flush();
                juce::StringArray cells;
                cells.addTokens (line.trim().substring (1, line.trim().length() - 1), "|", "");
                for (auto& c : cells) c = c.trim();
                if (cells[0].containsOnly ("-: ")) continue;          // the |---|---| line under the header
                if (blocks.empty() || blocks.back().kind != 6) blocks.push_back ({ 6, {}, {} });
                blocks.back().rows.push_back (cells);
                continue;
            }
            const int dot = line.indexOf (". ");
            if (dot > 0 && dot <= 2 && line.substring (0, dot).containsOnly ("0123456789")) { flush(); blocks.push_back ({ 5, line }); continue; }
            if (line.startsWith ("### ")) { flush(); blocks.push_back ({ 3, line.substring (4) }); }
            else if (line.startsWith ("## ")) { flush(); blocks.push_back ({ 2, line.substring (3) }); }
            else if (line.startsWith ("# ")) { flush(); blocks.push_back ({ 1, line.substring (2) }); }
            else if (line.startsWith ("- ")) { flush(); blocks.push_back ({ 4, line.substring (2) }); }
            else if (line.trim().isEmpty()) flush();
            else para << (para.isEmpty() ? "" : " ") << line.trim();
        }
        flush();
    }

    static juce::AttributedString rich (const juce::String& t, float size, juce::Colour c)
    {
        juce::AttributedString a;
        bool bold = false;
        juce::String rest = t;
        while (rest.isNotEmpty())
        {
            int i = rest.indexOf ("**");
            auto chunk = i < 0 ? rest : rest.substring (0, i);
            if (chunk.isNotEmpty()) a.append (chunk, font (size, bold ? 2 : 0), bold ? col::white : c);
            if (i < 0) break;
            bold = ! bold;
            rest = rest.substring (i + 2);
        }
        a.setWordWrap (juce::AttributedString::byWord);
        a.setLineSpacing (size * 0.35f);
        return a;
    }

    int layout (int w)
    {
        anchors.clear();
        int y = paintAll (nullptr, (float) w);
        setSize (w, y);
        return y;
    }

    void paint (juce::Graphics& g) override { paintAll (&g, (float) getWidth()); }

    int paintAll (juce::Graphics* g, float W)
    {
        const float x = 28, w = juce::jmin (W - 56, 920.0f);
        float y = 20;
        for (auto& b : blocks)
        {
            if (b.kind == 6)
            {
                y += 4;
                const int cols = b.rows.empty() ? 0 : b.rows[0].size();
                if (cols == 0) continue;
                // first column narrower when there are 3+ columns
                std::vector<float> cw ((size_t) cols, w / cols);
                if (cols >= 3) { cw[0] = w * 0.22f; for (int c = 1; c < cols; ++c) cw[(size_t) c] = (w - cw[0]) / (cols - 1); }
                for (size_t r = 0; r < b.rows.size(); ++r)
                {
                    float rh = 0, cx = x;
                    std::vector<juce::TextLayout> cells;
                    for (int c = 0; c < cols; ++c)
                    {
                        auto t = rich (b.rows[r][c], 13.5f, r == 0 ? col::white : col::text);
                        if (r == 0) t.setFont (font (13, 2));
                        juce::TextLayout tl; tl.createLayout (t, cw[(size_t) c] - 16);
                        rh = juce::jmax (rh, tl.getHeight());
                        cells.push_back (std::move (tl));
                    }
                    rh += 14;
                    if (g != nullptr)
                    {
                        if (r == 0) { g->setColour (juce::Colours::white.withAlpha (0.07f)); g->fillRect (x, y, w, rh); }
                        g->setColour (col::grid); g->fillRect (x, y + rh - 1, w, 1.0f);
                        for (int c = 0; c < cols; ++c) { cells[(size_t) c].draw (*g, { cx + 8, y + 7, cw[(size_t) c] - 16, rh }); cx += cw[(size_t) c]; }
                    }
                    y += rh;
                }
                y += 14;
                continue;
            }
            juce::AttributedString a;
            float before = 0, after = 8, indent = 0;
            switch (b.kind)
            {
                case 1: a = rich (b.text, 30, col::white); a.setFont (font (30, 2)); after = 10; break;
                case 2: a = rich (b.text, 21, col::white); a.setFont (font (21, 2)); before = 22; after = 8; break;
                case 3: a = rich (b.text, 16, col::text); a.setFont (font (16, 1)); before = 10; after = 4; break;
                case 4: a = rich (b.text, 15, col::text); indent = 22; after = 6; break;
                case 5: a = rich (b.text.fromFirstOccurrenceOf (". ", false, false), 15, col::text); indent = 26; after = 6; break;
                default: a = rich (b.text, 15, col::text); after = 12; break;
            }
            y += before;
            if (b.kind == 2) anchors.push_back ({ b.text, (int) y - 10 });
            juce::TextLayout tl; tl.createLayout (a, w - indent);
            if (g != nullptr)
            {
                if (b.kind == 2)
                {
                    g->setColour (col::grid); g->fillRect (x, y - 10, w, 1.0f);
                }
                if (b.kind == 4) drawKey (*g, { x + 4, y + 6, 8, 8 }, true, false, false, 4);
                if (b.kind == 5) { g->setColour (col::text2); g->setFont (font (15, 2)); g->drawText (b.text.upToFirstOccurrenceOf (".", false, false) + ".", (int) x, (int) y, 22, 20, juce::Justification::left); }
                tl.draw (*g, { x + indent, y, w - indent, tl.getHeight() });
            }
            y += tl.getHeight() + after;
        }
        return (int) y + 30;
    }
};

class HelpPage : public Page
{
public:
    HelpPage (Engine& e) : Page (e, "help", "Tutorial", "How to install, set up and use each part of Toolkit.")
    {
        addAndMakeVisible (index); index.setTitle ("Contents");
        addAndMakeVisible (body);
        int size = 0;
        const char* data = nullptr;
        for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
            if (juce::String (BinaryData::getNamedResourceOriginalFilename (BinaryData::namedResourceList[i])) == "tutorial.md")
                data = BinaryData::getNamedResource (BinaryData::namedResourceList[i], size);
        doc.parse (data != nullptr ? juce::String::fromUTF8 (data, size) : juce::String ("# Tutorial missing"));
        view.setViewedComponent (&doc, false);
        view.setScrollBarsShown (true, false);
        body.addAndMakeVisible (view);
        doc.layout (900);
        for (auto& [t, y] : doc.anchors)
        {
            auto b = makeButton (t, [this, title = t] { jump (title); });
            index.addAndMakeVisible (*b);
            links.push_back (std::move (b));
        }
        pdfNote = makeLabel ("This guide is also installed as Toolkit Tutorial.md.", 11.5f, col::text3);
        index.addAndMakeVisible (*pdfNote);
    }

    void jump (const juce::String& title)
    {
        for (auto& [t, y] : doc.anchors) if (t == title) view.setViewPosition (0, y);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        index.setBounds (r.removeFromLeft (290));
        r.removeFromLeft (10);
        body.setBounds (r);
        auto c = index.content();
        pdfNote->setBounds (c.removeFromBottom (36));
        int h = juce::jmin (38, c.getHeight() / juce::jmax (1, (int) links.size()));
        for (auto& b : links) b->setBounds (c.removeFromTop (h));
        view.setBounds (body.content());
        doc.layout (view.getMaximumVisibleWidth());
        // anchors move with the width; rebuild the jump targets
    }

    Slab index, body;
    juce::Viewport view;
    Doc doc;
    std::vector<std::unique_ptr<juce::TextButton>> links;
    std::unique_ptr<juce::Label> pdfNote;
};
}

std::unique_ptr<Page> makeHelpPage (Engine& e) { return std::make_unique<HelpPage> (e); }
}
