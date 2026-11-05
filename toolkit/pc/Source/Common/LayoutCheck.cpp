#include "LayoutCheck.h"

namespace tk
{
namespace
{
bool isControl (juce::Component* c)
{
    return dynamic_cast<juce::Button*> (c) != nullptr || dynamic_cast<juce::ComboBox*> (c) != nullptr
        || dynamic_cast<juce::Slider*> (c) != nullptr || dynamic_cast<juce::TextEditor*> (c) != nullptr;
}

juce::String describe (juce::Component* c)
{
    auto quoted = [] (const juce::String& s) { return "\"" + s.substring (0, 40) + "\""; };
    if (auto* b = dynamic_cast<juce::Button*> (c))
    {
        if (dynamic_cast<juce::ToggleButton*> (c) != nullptr) return "switch " + quoted (b->getButtonText());
        if (b->getButtonText().isNotEmpty()) return "button " + quoted (b->getButtonText());
        return "button " + quoted (b->getName().isNotEmpty() ? b->getName() : b->getTooltip());
    }
    if (auto* cb = dynamic_cast<juce::ComboBox*> (c)) return "dropdown " + quoted (cb->getText().isNotEmpty() ? cb->getText() : cb->getTooltip());
    if (auto* s = dynamic_cast<juce::Slider*> (c)) return "slider " + quoted (s->getTextFromValue (s->getValue()));
    if (auto* t = dynamic_cast<juce::TextEditor*> (c)) return "text box " + quoted (t->getText().isNotEmpty() ? t->getText() : t->getTextToShowWhenEmpty());
    return c->getName();
}

/** text that can't fit, even squeezed onto the lines the look allows */
juce::String crushedText (juce::Component* c)
{
    auto& lf = c->getLookAndFeel();
    const int w = c->getWidth(), h = c->getHeight();
    if (auto* tb = dynamic_cast<juce::TextButton*> (c))
    {
        auto text = tb->getButtonText();
        if (text.isEmpty()) return {};
        auto f = lf.getTextButtonFont (*tb, h);
        const float need = (float) juce::GlyphArrangement::getStringWidth (f, text);
        const int lines = juce::jlimit (1, 2, (int) ((h - 6) / juce::jmax (1.0f, f.getHeight())));
        // drawFittedText may squeeze text to 70% of its width and wrap onto `lines`, but never splits a word
        float word = 0;
        for (auto& wd : juce::StringArray::fromTokens (text, " ", {})) word = juce::jmax (word, (float) juce::GlyphArrangement::getStringWidth (f, wd));
        if (word > (float) (w - 12) / 0.7f) return "\"" + text + "\" gets cut short (" + juce::String ((int) word) + " px word in " + juce::String (w) + ")";
        if (need > (float) (w - 12) * (float) lines / 0.7f) return "text doesn't fit (" + juce::String ((int) need) + " px of text in " + juce::String (w) + ")";
        return {};
    }
    if (auto* t = dynamic_cast<juce::ToggleButton*> (c))
    {
        auto text = t->getButtonText();
        if (text.isEmpty()) return {};
        const float need = (float) juce::GlyphArrangement::getStringWidth (juce::Font (juce::FontOptions (14.0f)), text);
        if (need > (float) (w - 52) * 1.12f + 2) return "label cut short (" + juce::String ((int) need) + " px of text in " + juce::String (juce::jmax (0, w - 52)) + ")";
    }
    return {};
}

struct Walker
{
    juce::Component& root;
    juce::StringArray problems, seen;

    static constexpr int big = 1 << 28;

    void walk (juce::Component& parent, juce::Rectangle<int> clip)
    {
        juce::Array<std::pair<juce::Component*, juce::Rectangle<int>>> controls;
        for (auto* c : parent.getChildren())
        {
            if (! c->isVisible()) continue;
            const auto b = root.getLocalArea (c, c->getLocalBounds());
            if (isControl (c))
            {
                check (c, b, clip);
                controls.add ({ c, b });
                continue;   // a control's own insides (a dropdown's label, a slider's text box) are its business
            }
            if (dynamic_cast<juce::ListBox*> (c) != nullptr) continue;   // rows come and go as it scrolls
            if (auto* vp = dynamic_cast<juce::Viewport*> (c))
            {
                // inside a scrolling area, running past the edge in the direction it scrolls is fine
                auto inner = b.getIntersection (clip);
                if (vp->isHorizontalScrollBarShown()) inner = inner.withX (-big / 2).withWidth (big);
                if (vp->isVerticalScrollBarShown()) inner = inner.withY (-big / 2).withHeight (big);
                if (auto* viewed = vp->getViewedComponent()) walkViewed (*viewed, inner);
                continue;
            }
            walk (*c, b.getIntersection (clip));
        }
        // controls on the same panel sitting on top of each other
        for (int i = 0; i < controls.size(); ++i)
            for (int j = i + 1; j < controls.size(); ++j)
            {
                auto o = controls[i].second.getIntersection (controls[j].second);
                if (o.getWidth() * o.getHeight() > 16)
                    problems.add (describe (controls[i].first) + " overlaps " + describe (controls[j].first));
            }
    }

    void walkViewed (juce::Component& viewed, juce::Rectangle<int> clip)
    {
        // the viewed component itself can be any size; its children are judged against the scrolling area
        walk (viewed, clip);
    }

    void check (juce::Component* c, juce::Rectangle<int> b, juce::Rectangle<int> clip)
    {
        const auto name = describe (c);
        seen.add (name);
        if (b.getWidth() < 12 || b.getHeight() < 12)
        {
            problems.add (name + ": squashed to " + juce::String (b.getWidth()) + " x " + juce::String (b.getHeight()));
            return;
        }
        const auto vis = b.getIntersection (clip);
        const double frac = (double) vis.getWidth() * vis.getHeight() / ((double) b.getWidth() * b.getHeight());
        if (frac <= 0.0) { problems.add (name + ": off-screen (at " + juce::String (b.getX()) + ", " + juce::String (b.getY()) + ")"); return; }
        if (frac < 0.97) { problems.add (name + ": cut off (" + juce::String ((int) (frac * 100)) + "% visible)"); return; }
        if (auto* s = dynamic_cast<juce::Slider*> (c))
            if (s->isHorizontal() && b.getWidth() - (s->getTextBoxPosition() == juce::Slider::NoTextBox ? 0 : s->getTextBoxWidth()) < 40)
                problems.add (name + ": slider too short to use (" + juce::String (b.getWidth()) + " px)");
        if (dynamic_cast<juce::ComboBox*> (c) != nullptr && b.getWidth() < 60)
            problems.add (name + ": dropdown too narrow (" + juce::String (b.getWidth()) + " px)");
        if (dynamic_cast<juce::TextEditor*> (c) != nullptr && b.getWidth() < 80)
            problems.add (name + ": text box too narrow to type in (" + juce::String (b.getWidth()) + " px)");
        auto t = crushedText (c);
        if (t.isNotEmpty()) problems.add (name + ": " + t);
    }
};
}

juce::StringArray findLayoutProblems (juce::Component& root)
{
    Walker w { root };
    w.walk (root, root.getLocalBounds());
    w.problems.removeDuplicates (false);
    return w.problems;
}

juce::StringArray listControls (juce::Component& root)
{
    Walker w { root };
    w.walk (root, root.getLocalBounds());
    return w.seen;
}
}
