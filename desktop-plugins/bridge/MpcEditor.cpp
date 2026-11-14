#include "MpcEditor.h"
#include "SkinData.h"
#include <functional>
#include <map>

namespace bcp
{
// ============================================================ the skin

static juce::Colour argb (const juce::var& v, juce::Colour fallback)
{
    const auto s = v.toString().trim();
    if (s.length() != 8) return fallback;
    return juce::Colour ((juce::uint32) s.getHexValue64());
}

static juce::Rectangle<int> rect (const juce::String& s)
{
    auto t = juce::StringArray::fromTokens (s, " ", {});
    if (t.size() < 4) return {};
    return { t[0].getIntValue(), t[1].getIntValue(), t[2].getIntValue(), t[3].getIntValue() };
}

juce::Image Skin::image (const juce::String& file)
{
    if (file.isEmpty()) return {};
    for (int i = 0; i < SkinData::namedResourceListSize; ++i)
    {
        if (juce::String (SkinData::originalFilenames[i]) == file)
        {
            int size = 0;
            const char* data = SkinData::getNamedResource (SkinData::namedResourceList[i], size);
            if (data != nullptr && size > 0) return juce::ImageCache::getFromMemory (data, size);
        }
    }
    return {};
}

static juce::String skinText (const char* file)
{
    for (int i = 0; i < SkinData::namedResourceListSize; ++i)
        if (juce::String (SkinData::originalFilenames[i]) == file)
        {
            int size = 0;
            const char* data = SkinData::getNamedResource (SkinData::namedResourceList[i], size);
            return juce::String::fromUTF8 (data, size);
        }
    return {};
}

/** "Parameter 12" -> 12 */
static int paramFromHandle (const juce::String& h)
{
    if (! h.startsWith ("Parameter ")) return -1;
    return h.fromFirstOccurrenceOf ("Parameter ", false, false).getIntValue();
}

void Skin::load()
{
    tabs.clear(); problems.clear();
    const auto root = juce::JSON::parse (skinText ("TUI.json"));
    const auto pageData = root["pageData"];
    if (! pageData.isObject()) { problems.add ("TUI.json missing or unreadable"); return; }

    // local component definitions (knobUni, faderUni, toggle, radio2...), by key
    std::map<juce::String, juce::var> defs;
    if (auto* arr = pageData["componentDefinitions"]["localComponentDefinitions"].getArray())
        for (auto& d : *arr) defs[d["key"].toString()] = d["value"];

    std::map<juce::String, juce::Image> images;
    auto img = [&] (const juce::String& f) -> juce::Image
    {
        if (f.isEmpty()) return {};
        auto it = images.find (f);
        if (it != images.end()) return it->second;
        auto im = image (f);
        if (! im.isValid()) problems.addIfNotAlreadyThere ("missing image " + f);
        images[f] = im;
        return im;
    };

    bool haveColours = false;
    // one primitive (Label, Knob, Button...) at an offset, with the Data handle pointing at a parameter
    std::function<void (Tab&, const juce::var&, juce::Point<int>, int, int, const juce::String&, int)> addPrimitive;
    addPrimitive = [&] (Tab& tab, const juce::var& c, juce::Point<int> off, int param, int slot, const juce::String& defName, int visBase)
    {
        const auto cd = c["componentData"];
        const auto type = cd["type"].toString();
        const auto data = cd["data"];
        Element e;
        e.bounds = rect (c["bounds"]["bounds"].toString()) + off;
        e.param = param;
        e.slot = slot;
        if (type == "Focus")
        {
            e.kind = Kind::Focus;
            e.focusFill = argb (data["backgroundColour"], e.focusFill);
            e.focusOutline = argb (data["outlineColour"], accent);
            accent = e.focusOutline;
        }
        else if (type == "Knob")
        {
            if (data["knobType"].toString() == "FilmStrip")
            {
                e.kind = Kind::KnobStrip;
                e.strip = img (data["filmStrip"].toString());
                e.frames = juce::jmax (1, (int) data["numFrames"]);
                e.horizontal = data["dragOrientation"].toString() == "Horizontal";
                e.invert = (bool) data["invert"];
            }
            else
            {
                e.kind = Kind::KnobGeneric;
                e.bipolar = data["polarity"].toString() == "BiPolar";
            }
            e.bipolar = e.bipolar || defName.contains ("Bi");
        }
        else if (type == "Label")
        {
            e.kind = Kind::Label;
            const auto ts = data["textStyle"];
            e.colour = argb (ts["colour"], text);
            e.fontHeight = (float) (double) ts["font"]["height"];
            if (e.fontHeight <= 0) e.fontHeight = 26.0f;
            const auto j = ts["justification"].toString();
            e.just = j.contains ("horizontallyCentred") ? juce::Justification::centred
                   : j.contains ("right") ? juce::Justification::centredRight : juce::Justification::centredLeft;
            const auto lt = data["type"].toString();
            e.labelType = lt == "Name" ? LabelType::Name : lt == "Value" ? LabelType::Value : LabelType::Text;
            e.text = data["text"].toString();
            if (! haveColours && e.labelType != LabelType::Text)
            {
                if (e.labelType == LabelType::Name) muted = e.colour;
            }
            if (e.labelType == LabelType::Value) { text = e.colour; haveColours = true; }
        }
        else if (type == "Button")
        {
            e.kind = Kind::Button;
            e.onImage = img (data["onImage"].toString());
            e.offImage = img (data["offImage"].toString());
            e.buttonId = (int) data["buttonId"];
            e.groupSize = juce::jmax (1, (int) data["numButtonsInGroup"]);
        }
        else if (type == "Slider")
        {
            e.kind = Kind::Slider;
            e.menu = defName.endsWith ("Menu");
        }
        else
        {
            problems.addIfNotAlreadyThere ("unknown widget " + type);
            return;
        }
        tab.elements.push_back (e);
    };

    auto visibility = [] (const juce::var& bounds, int& vp, int& vi, int& vs)
    {
        if (auto* inv = bounds["additionalInvalidatingHandles"].getArray())
            for (auto& h : *inv)
            {
                auto s = h.toString();
                if (! s.startsWith ("IndexedEnabling/")) continue;
                auto parts = juce::StringArray::fromTokens (s.fromFirstOccurrenceOf ("IndexedEnabling/", false, false), "/", {});
                if (parts.size() >= 3)
                {
                    vi = parts[0].getIntValue(); vs = juce::jmax (2, parts[1].getIntValue());
                    vp = paramFromHandle (parts.joinIntoString ("/", 2));
                }
            }
    };

    if (auto* tabArr = pageData["tabs"].getArray())
    {
        for (auto& t : *tabArr)
        {
            Tab tab;
            tab.name = t["tabName"].toString();
            const auto cdef = t["componentDefinition"];
            const auto bg = cdef["backgroundData"]["unfocussed"];
            tab.background = argb (bg["colour"], tab.background);
            tab.backgroundImage = img (bg["image"].toString());
            if (auto* kids = cdef["componentsData"].getArray())
            {
                for (auto& c : *kids)
                {
                    const auto type = c["componentData"]["type"].toString();
                    const auto bounds = rect (c["bounds"]["bounds"].toString());
                    int vp = -1, vi = 0, vs = 2;
                    visibility (c["bounds"], vp, vi, vs);
                    int param = -1;
                    if (auto* m = c["handle remapping"]["map"].getArray())
                        for (auto& kv : *m) if (kv["key"].toString() == "Data") param = paramFromHandle (kv["value"].toString());
                    const size_t first = tab.elements.size();
                    auto d = defs.find (type);
                    if (d != defs.end())
                    {
                        const int slot = tab.numSlots++;
                        if (auto* parts = d->second["componentsData"].getArray())
                            for (auto& part : *parts) addPrimitive (tab, part, bounds.getPosition(), param, slot, type, 0);
                    }
                    else addPrimitive (tab, c, {}, param, -1, {}, -1);
                    for (size_t k = first; k < tab.elements.size(); ++k)
                    {
                        tab.elements[k].visParam = vp; tab.elements[k].visIndex = vi; tab.elements[k].visSteps = vs;
                    }
                }
            }
            tabs.push_back (std::move (tab));
        }
    }
    if (tabs.empty()) problems.add ("no tabs in TUI.json");
}

// ============================================================ one page

bool PageView::visible (const Skin::Element& e) const
{
    if (e.visParam < 0 || e.visParam >= proc.plugin().numParams()) return true;
    const int step = (int) std::lround (proc.plugin().getNormalised (e.visParam) * (float) (e.visSteps - 1));
    return step == e.visIndex;
}

static float frameNorm (const Skin::Element& e, float norm) { return e.invert ? 1.0f - norm : norm; }

void PageView::drawElement (juce::Graphics& g, const Skin::Element& e)
{
    auto& pl = proc.plugin();
    const bool hasParam = e.param >= 0 && e.param < pl.numParams();
    const float norm = hasParam ? pl.getNormalised (e.param) : 0.0f;
    const auto b = e.bounds.toFloat();
    switch (e.kind)
    {
        case Skin::Kind::Focus:
            if (e.slot >= 0 && (e.slot == hoverSlot || e.slot == dragSlot))
            {
                auto r = b.reduced (4.0f);
                g.setColour (e.focusFill); g.fillRoundedRectangle (r, 6.0f);
                g.setColour (e.focusOutline.withAlpha (e.slot == dragSlot ? 1.0f : 0.6f)); g.drawRoundedRectangle (r, 6.0f, 1.5f);
            }
            break;
        case Skin::Kind::KnobStrip:
            if (e.strip.isValid())
            {
                const int fw = e.strip.getWidth(), fh = e.strip.getHeight() / juce::jmax (1, e.frames);
                const int f = juce::jlimit (0, e.frames - 1, (int) std::lround (frameNorm (e, norm) * (float) (e.frames - 1)));
                g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
                g.drawImage (e.strip, e.bounds.getX(), e.bounds.getY(), e.bounds.getWidth(), e.bounds.getHeight(), 0, f * fh, fw, fh);
            }
            break;
        case Skin::Kind::KnobGeneric:
        {
            auto r = b.reduced (6.0f);
            const float a0 = juce::MathConstants<float>::pi * 1.2f, a1 = juce::MathConstants<float>::pi * 2.8f;
            juce::Path track; track.addCentredArc (r.getCentreX(), r.getCentreY(), r.getWidth() / 2, r.getHeight() / 2, 0, a0, a1, true);
            g.setColour (skin.muted.withAlpha (0.5f)); g.strokePath (track, juce::PathStrokeType (6.0f));
            const float from = e.bipolar ? (a0 + a1) / 2 : a0, to = a0 + (a1 - a0) * norm;
            juce::Path val; val.addCentredArc (r.getCentreX(), r.getCentreY(), r.getWidth() / 2, r.getHeight() / 2, 0, juce::jmin (from, to), juce::jmax (from, to), true);
            g.setColour (skin.accent); g.strokePath (val, juce::PathStrokeType (6.0f));
            break;
        }
        case Skin::Kind::Label:
        {
            juce::String s = e.text;
            if (hasParam && e.labelType == Skin::LabelType::Name) s = juce::String (pl.param (e.param).name);
            if (hasParam && e.labelType == Skin::LabelType::Value) s = proc.valueText (e.param);
            g.setColour (e.colour);
            g.setFont (juce::Font (juce::FontOptions (e.fontHeight * 0.92f)));
            g.drawFittedText (s, e.bounds, e.just, 1, 0.8f);
            break;
        }
        case Skin::Kind::Button:
        {
            bool on;
            if (! hasParam) on = false;
            else if (e.groupSize <= 1) on = norm >= 0.5f;
            else on = (int) std::lround (pl.get (e.param)) == e.buttonId;
            const auto& im = on ? e.onImage : e.offImage;
            if (im.isValid()) g.drawImage (im, b, juce::RectanglePlacement::centred);
            else
            {
                g.setColour (on ? skin.accent : skin.muted.withAlpha (0.4f));
                g.fillRoundedRectangle (b.reduced (2), 6.0f);
            }
            break;
        }
        case Skin::Kind::Slider:
        {
            // the MPC's stock slider: name, value and a bar; with a menu it shows the choice as a drop-down
            if (! hasParam) break;
            auto r = b;
            g.setColour (skin.muted);
            g.setFont (juce::Font (juce::FontOptions (24.0f)));
            g.drawFittedText (juce::String (pl.param (e.param).name), r.removeFromTop (34).toNearestInt(), juce::Justification::centredLeft, 1);
            auto row = r.removeFromTop (34);
            if (e.menu)
            {
                g.setColour (juce::Colours::black.withAlpha (0.35f)); g.fillRoundedRectangle (row, 6.0f);
                g.setColour (skin.accent.withAlpha (0.8f)); g.drawRoundedRectangle (row.reduced (0.5f), 6.0f, 1.2f);
                g.setColour (skin.text);
                g.setFont (juce::Font (juce::FontOptions (26.0f)));
                g.drawFittedText (proc.valueText (e.param), row.reduced (12, 0).toNearestInt(), juce::Justification::centredLeft, 1);
                juce::Path arrow;
                const float cx = row.getRight() - 22, cy = row.getCentreY();
                arrow.addTriangle (cx - 8, cy - 4, cx + 8, cy - 4, cx, cy + 6);
                g.setColour (skin.accent); g.fillPath (arrow);
            }
            else
            {
                g.setColour (skin.text);
                g.setFont (juce::Font (juce::FontOptions (26.0f)));
                g.drawFittedText (proc.valueText (e.param), row.toNearestInt(), juce::Justification::centredLeft, 1);
                auto bar = r.removeFromTop (8).withTrimmedTop (2);
                g.setColour (skin.muted.withAlpha (0.4f)); g.fillRoundedRectangle (bar, 3);
                g.setColour (skin.accent); g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * norm), 3);
            }
            break;
        }
    }
}

void PageView::paint (juce::Graphics& g)
{
    if (tab < 0 || tab >= (int) skin.tabs.size()) { g.fillAll (juce::Colours::black); return; }
    const auto& t = skin.tabs[(size_t) tab];
    g.fillAll (t.background);
    g.addTransform (juce::AffineTransform::scale (scale()));
    if (t.backgroundImage.isValid())
    {
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (t.backgroundImage, juce::Rectangle<float> (0, 0, (float) kTabW, (float) kTabH), juce::RectanglePlacement::stretchToFit);
    }
    for (auto& e : t.elements)
        if (visible (e)) drawElement (g, e);
}

const Skin::Element* PageView::hit (juce::Point<float> p) const
{
    if (tab < 0 || tab >= (int) skin.tabs.size()) return nullptr;
    const auto& els = skin.tabs[(size_t) tab].elements;
    // buttons first (a toggle or a segment), then knobs, faders and sliders, then the whole slot (its focus area)
    const Skin::Element* slotHit = nullptr;
    for (int pass = 0; pass < 3; ++pass)
        for (auto it = els.rbegin(); it != els.rend(); ++it)
        {
            const auto& e = *it;
            if (e.param < 0 || ! visible (e) || ! e.bounds.toFloat().contains (p)) continue;
            if (pass == 0 && e.kind == Skin::Kind::Button) return &e;
            if (pass == 1 && (e.kind == Skin::Kind::KnobStrip || e.kind == Skin::Kind::KnobGeneric || e.kind == Skin::Kind::Slider)) return &e;
            if (pass == 2 && e.kind == Skin::Kind::Focus && slotHit == nullptr)
            {
                // the slot's main control (a knob or fader in the same slot) does the work
                for (auto& o : els)
                    if (o.slot == e.slot && (o.kind == Skin::Kind::KnobStrip || o.kind == Skin::Kind::KnobGeneric || o.kind == Skin::Kind::Slider)) return &o;
                slotHit = &e;
            }
        }
    return nullptr;
}

void PageView::mouseMove (const juce::MouseEvent& ev)
{
    const auto* e = hit (toMpc (ev.position));
    const int s = e != nullptr ? e->slot : -1;
    if (s != hoverSlot) { hoverSlot = s; repaint(); }
    if (e != nullptr && e->param >= 0)
    {
        auto tip = juce::String (proc.plugin().param (e->param).name) + ": " + proc.valueText (e->param);
        if (e->kind == Skin::Kind::KnobStrip || e->kind == Skin::Kind::KnobGeneric)
            tip << "   (drag, Shift = fine, double-click = default)";
        setTooltip (tip);
    }
    else setTooltip ({});
    setMouseCursor (e != nullptr ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void PageView::mouseExit (const juce::MouseEvent&) { if (hoverSlot != -1) { hoverSlot = -1; repaint(); } }

void PageView::mouseDown (const juce::MouseEvent& ev)
{
    dragEl = hit (toMpc (ev.position));
    if (dragEl == nullptr) return;
    auto& pl = proc.plugin();
    const int idx = dragEl->param;
    const auto& prm = pl.param (idx);
    dragSlot = dragEl->slot;
    if (dragEl->kind == Skin::Kind::Button)
    {
        float v;
        if (dragEl->groupSize <= 1) v = pl.getNormalised (idx) >= 0.5f ? 0.0f : 1.0f;
        else v = prm.toNormalised ((float) dragEl->buttonId);
        proc.setFromUi (idx, v, true, true);
        dragEl = nullptr;
        repaint();
        return;
    }
    if (dragEl->kind == Skin::Kind::Slider && dragEl->menu && prm.kind == mpc::Param::Kind::Choice)
    {
        juce::PopupMenu m;
        const int cur = (int) std::lround (pl.get (idx));
        for (int c = 0; c < (int) prm.choices.size(); ++c) m.addItem (c + 1, prm.choices[(size_t) c], true, c == cur);
        juce::Component::SafePointer<PageView> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [safe, idx] (int r)
        {
            if (safe == nullptr || r <= 0) return;
            auto& p = safe->proc.plugin().param (idx);
            safe->proc.setFromUi (idx, p.toNormalised ((float) (r - 1)), true, true);
            safe->dragSlot = -1;
            safe->repaint();
        });
        dragEl = nullptr;
        return;
    }
    dragStart = ev.position;
    dragStartValue = pl.getNormalised (idx);
    proc.setFromUi (idx, dragStartValue, true, false);
    // a fader jumps to where it's clicked, like a slider
    if (dragEl->kind == Skin::Kind::KnobStrip && dragEl->horizontal && dragEl->bounds.toFloat().contains (toMpc (ev.position)))
    {
        const auto b = dragEl->bounds.toFloat();
        dragStartValue = juce::jlimit (0.0f, 1.0f, (toMpc (ev.position).x - b.getX()) / b.getWidth());
        proc.setFromUi (idx, dragStartValue, false, false);
    }
    repaint();
}

void PageView::mouseDrag (const juce::MouseEvent& ev)
{
    if (dragEl == nullptr) return;
    const bool fine = ev.mods.isShiftDown();
    float delta;
    if (dragEl->horizontal || (dragEl->kind == Skin::Kind::Slider))
        delta = (ev.position.x - dragStart.x) / (dragEl->bounds.getWidth() * scale());
    else
        delta = (dragStart.y - ev.position.y) / 220.0f;
    if (fine) delta *= 0.15f;
    proc.setFromUi (dragEl->param, dragStartValue + delta, false, false);
    repaint();
}

void PageView::mouseUp (const juce::MouseEvent&)
{
    if (dragEl != nullptr) proc.setFromUi (dragEl->param, proc.plugin().getNormalised (dragEl->param), false, true);
    dragEl = nullptr;
    dragSlot = -1;
    repaint();
}

void PageView::mouseDoubleClick (const juce::MouseEvent& ev)
{
    const auto* e = hit (toMpc (ev.position));
    if (e == nullptr || e->kind == Skin::Kind::Button || (e->kind == Skin::Kind::Slider && e->menu)) return;
    const auto& prm = proc.plugin().param (e->param);
    proc.setFromUi (e->param, prm.toNormalised (prm.def), true, true);
    repaint();
}

void PageView::mouseWheelMove (const juce::MouseEvent& ev, const juce::MouseWheelDetails& w)
{
    const auto* e = hit (toMpc (ev.position));
    if (e == nullptr || e->kind == Skin::Kind::Button) return;
    auto& pl = proc.plugin();
    const auto& prm = pl.param (e->param);
    const float dir = (w.deltaY != 0 ? w.deltaY : w.deltaX) > 0 ? 1.0f : -1.0f;
    float v = pl.getNormalised (e->param);
    if (prm.kind != mpc::Param::Kind::Float)
    {
        const float plain = juce::jlimit (prm.min, prm.max, std::round (pl.get (e->param)) + dir);
        v = prm.toNormalised (plain);
    }
    else v += dir * (ev.mods.isShiftDown() ? 0.002f : 0.02f);
    proc.setFromUi (e->param, v, true, true);
    repaint();
}

// ============================================================ the window

namespace
{
    struct BridgeLook : juce::LookAndFeel_V4
    {
        juce::Colour accent, text, muted, bg;
        BridgeLook (juce::Colour a, juce::Colour t, juce::Colour m, juce::Colour b) : accent (a), text (t), muted (m), bg (b)
        {
            setColour (juce::ComboBox::backgroundColourId, bg.brighter (0.08f));
            setColour (juce::ComboBox::textColourId, text);
            setColour (juce::ComboBox::outlineColourId, accent.withAlpha (0.6f));
            setColour (juce::ComboBox::arrowColourId, accent);
            setColour (juce::PopupMenu::backgroundColourId, bg.brighter (0.05f));
            setColour (juce::PopupMenu::textColourId, text);
            setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.85f));
            setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::black);
            setColour (juce::TextButton::buttonColourId, bg.brighter (0.06f));
            setColour (juce::TextButton::buttonOnColourId, accent);
            setColour (juce::TextButton::textColourOffId, muted.brighter (0.4f));
            setColour (juce::TextButton::textColourOnId, juce::Colours::black);
            setColour (juce::TooltipWindow::backgroundColourId, bg.brighter (0.15f));
            setColour (juce::TooltipWindow::textColourId, text);
            setColour (juce::TooltipWindow::outlineColourId, accent.withAlpha (0.6f));
        }
        juce::Font getTextButtonFont (juce::TextButton&, int h) override { return juce::Font (juce::FontOptions ((float) h * 0.5f, juce::Font::bold)); }
        void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
        {
            auto r = b.getLocalBounds().toFloat().reduced (1.5f);
            const bool on = b.getToggleState();
            g.setColour (on ? accent : bg.brighter (over ? 0.14f : 0.07f));
            g.fillRoundedRectangle (r, 5.0f);
            g.setColour (on ? accent.brighter (0.3f) : accent.withAlpha (down ? 0.9f : 0.35f));
            g.drawRoundedRectangle (r, 5.0f, 1.0f);
        }
    };
}

BridgeEditor::BridgeEditor (BridgeProcessor& p)
    : AudioProcessorEditor (p), proc (p), page (p, skin)
{
    skin.load();
    const auto bg = skin.tabs.empty() ? juce::Colour (0xff1e1d1e) : skin.tabs[0].background;
    lnf = std::make_unique<BridgeLook> (skin.accent, skin.text, skin.muted, bg.darker (0.35f));
    setLookAndFeel (lnf.get());

    addAndMakeVisible (page);
    for (int i = 0; i < (int) skin.tabs.size(); ++i)
    {
        auto* b = tabButtons.add (new juce::TextButton (skin.tabs[(size_t) i].name));
        b->setRadioGroupId (77);
        b->setClickingTogglesState (true);
        b->onClick = [this, i] { showTab (i); };
        addAndMakeVisible (b);
    }
    refreshPresets();
    presets.onChange = [this]
    {
        const int i = presets.getSelectedItemIndex();
        if (i >= 0 && i != proc.getCurrentProgram()) proc.setCurrentProgram (i);
    };
    prevBtn.onClick = [this] { const int n = proc.getNumPrograms(); proc.setCurrentProgram ((proc.getCurrentProgram() + n - 1) % n); };
    nextBtn.onClick = [this] { const int n = proc.getNumPrograms(); proc.setCurrentProgram ((proc.getCurrentProgram() + 1) % n); };
    prevBtn.setTooltip ("Previous preset"); nextBtn.setTooltip ("Next preset");
    for (juce::Component* c : { (juce::Component*) &presets, (juce::Component*) &prevBtn, (juce::Component*) &nextBtn }) addAndMakeVisible (c);

    constrainer.setFixedAspectRatio ((double) baseWidth() / baseHeight());
    constrainer.setSizeLimits (baseWidth() / 2, baseHeight() / 2, baseWidth() * 3 / 2, baseHeight() * 3 / 2);
    setConstrainer (&constrainer);
    setResizable (true, true);
    setSize (baseWidth() * 3 / 4, baseHeight() * 3 / 4);
    showTab (0);
    lastValues.assign ((size_t) proc.plugin().numParams(), -1.0f);
    startTimerHz (30);
}

BridgeEditor::~BridgeEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void BridgeEditor::refreshPresets()
{
    presets.clear (juce::dontSendNotification);
    for (int i = 0; i < proc.getNumPrograms(); ++i)
        presets.addItem (juce::String (i + 1).paddedLeft ('0', 3) + "  " + proc.getProgramName (i), i + 1);
    presets.setSelectedItemIndex (proc.getCurrentProgram(), juce::dontSendNotification);
}

void BridgeEditor::showTab (int t)
{
    t = juce::jlimit (0, juce::jmax (0, (int) skin.tabs.size() - 1), t);
    for (int i = 0; i < tabButtons.size(); ++i) tabButtons[i]->setToggleState (i == t, juce::dontSendNotification);
    page.setTab (t);
}

void BridgeEditor::paint (juce::Graphics& g)
{
    const float s = (float) getWidth() / (float) baseWidth();
    const auto bg = skin.tabs.empty() ? juce::Colour (0xff1e1d1e) : skin.tabs[0].background;
    g.fillAll (bg.darker (0.45f));
    auto header = getLocalBounds().removeFromTop (juce::roundToInt (kHeaderH * s));
    g.setColour (skin.accent);
    g.setFont (juce::Font (juce::FontOptions (30.0f * s, juce::Font::bold)));
    g.drawText (getAudioProcessor()->getName().toUpperCase(), header.withTrimmedLeft (juce::roundToInt (22 * s)), juce::Justification::centredLeft);
    g.setColour (skin.muted);
    g.setFont (juce::Font (juce::FontOptions (16.0f * s)));
    g.drawText ("binaryimage", header.withTrimmedRight (juce::roundToInt (22 * s)), juce::Justification::centredRight);
    g.setColour (skin.accent.withAlpha (0.35f));
    g.fillRect (0.0f, (float) header.getBottom() - 1.0f, (float) getWidth(), 1.0f);
}

void BridgeEditor::resized()
{
    const float s = (float) getWidth() / (float) baseWidth();
    auto r = getLocalBounds();
    auto header = r.removeFromTop (juce::roundToInt (kHeaderH * s));
    // presets in the middle of the header
    auto mid = header.withSizeKeepingCentre (juce::roundToInt (520 * s), juce::roundToInt (34 * s));
    prevBtn.setBounds (mid.removeFromLeft (juce::roundToInt (40 * s)));
    nextBtn.setBounds (mid.removeFromRight (juce::roundToInt (40 * s)));
    presets.setBounds (mid.reduced (juce::roundToInt (6 * s), 0));
    auto bar = r.removeFromTop (juce::roundToInt (kTabBarH * s)).reduced (juce::roundToInt (16 * s), juce::roundToInt (4 * s));
    const int n = juce::jmax (1, tabButtons.size());
    const int w = juce::jmin (juce::roundToInt (170 * s), bar.getWidth() / n);
    for (auto* b : tabButtons) { b->setBounds (bar.removeFromLeft (w).reduced (juce::roundToInt (3 * s), 0)); }
    page.setBounds (r);
}

void BridgeEditor::timerCallback()
{
    // a preset loaded (from here, the host or a state restore): show it
    if (lastProgramVersion != proc.uiProgramVersion.load())
    {
        lastProgramVersion = proc.uiProgramVersion.load();
        presets.setSelectedItemIndex (proc.getCurrentProgram(), juce::dontSendNotification);
    }
    // values moved by automation or MIDI: redraw
    bool changed = false;
    auto& pl = proc.plugin();
    for (int i = 0; i < pl.numParams() && i < (int) lastValues.size(); ++i)
    {
        const float v = pl.getNormalised (i);
        if (v != lastValues[(size_t) i]) { lastValues[(size_t) i] = v; changed = true; }
    }
    if (changed) page.repaint();
}
}
