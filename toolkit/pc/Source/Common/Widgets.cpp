#include "Widgets.h"
#include "Dsp.h"
#include <cmath>

namespace tk
{
// ------------------------------------------------------------ slab

juce::Rectangle<int> Slab::content() const
{
    auto r = getLocalBounds().reduced (14, 12);
    if (title.isNotEmpty()) r.removeFromTop (26);
    return r;
}

void Slab::paint (juce::Graphics& g)
{
    drawSlab (g, getLocalBounds().toFloat().reduced (4, 3), 12);
    if (title.isNotEmpty())
        drawEngraved (g, title.toUpperCase(), getLocalBounds().reduced (18, 12).withHeight (18), juce::Justification::centredLeft,
                      font (11.5f, 2), col::text3);
}

// ------------------------------------------------------------ waterfall

void Waterfall3D::push (const std::vector<float>& m, double binHz)
{
    const int bands = 160;
    std::vector<float> line ((size_t) bands);
    for (int b = 0; b < bands; ++b)
    {
        double f0 = 25.0 * std::pow (16000.0 / 25.0, b / (double) bands), f1 = 25.0 * std::pow (16000.0 / 25.0, (b + 1) / (double) bands);
        int i0 = juce::jlimit (1, (int) m.size() - 1, (int) (f0 / binHz)), i1 = juce::jlimit (i0 + 1, (int) m.size(), (int) (f1 / binHz) + 1);
        float mx = -200;
        for (int i = i0; i < i1; ++i) mx = std::max (mx, m[(size_t) i]);
        line[(size_t) b] = juce::jlimit (0.0f, 1.0f, (mx - floorDb) / (ceilDb - floorDb));
    }
    lines.push_front (std::move (line));
    while ((int) lines.size() > depth) lines.pop_back();
    repaint();
}

void Waterfall3D::paintScreen (juce::Graphics& g, juce::Rectangle<float> r)
{
    if (lines.empty())
    {
        g.setFont (font (13)); g.setColour (col::text3);
        g.drawText ("waiting for sound", r.toNearestInt(), juce::Justification::centred);
        return;
    }
    // back to front, each line filled black below it so nearer lines hide farther ones
    const int n = (int) lines.size();
    const float rowH = r.getHeight() * 0.7f / depth, amp = r.getHeight() * 0.26f;
    for (int k = n - 1; k >= 0; --k)
    {
        const auto& L = lines[(size_t) k];
        float d = k / (float) depth;                       // 0 front .. 1 back
        float inset = d * r.getWidth() * 0.16f;            // perspective narrowing
        float base = r.getBottom() - 14 - k * rowH;
        float x0 = r.getX() + inset, x1 = r.getRight() - inset;
        float scale = 1.0f - d * 0.45f;
        juce::Path p;
        p.startNewSubPath (x0, base);
        for (int b = 0; b < (int) L.size(); ++b)
        {
            float x = x0 + (x1 - x0) * b / (float) (L.size() - 1);
            p.lineTo (x, base - L[(size_t) b] * amp * scale);
        }
        p.lineTo (x1, base);
        juce::Path fill (p); fill.closeSubPath();
        g.setColour (col::screen); g.fillPath (fill);
        float a = juce::jmap (d, 1.0f, 0.12f);
        g.setColour (juce::Colours::white.withAlpha (a));
        g.strokePath (p, juce::PathStrokeType (k == 0 ? 1.8f : 1.1f));
    }
    // frequency labels along the front line
    g.setFont (font (10)); g.setColour (col::text3);
    for (double f : { 50.0, 100.0, 500.0, 1000.0, 5000.0, 10000.0 })
    {
        float t = (float) (std::log (f / 25.0) / std::log (16000.0 / 25.0));
        float x = r.getX() + r.getWidth() * t;
        g.drawText (f >= 1000 ? juce::String ((int) (f / 1000)) + "k" : juce::String ((int) f), (int) x - 20, (int) r.getBottom() - 2, 40, 12, juce::Justification::centred);
    }
}

// ------------------------------------------------------------ scope

void Scope::setData (const float* x, int n, double per) { data.assign (x, x + n); period = per; repaint(); }

void Scope::paintScreen (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setColour (col::grid);
    for (int i = 1; i < 8; ++i) g.drawVerticalLine ((int) (r.getX() + r.getWidth() * i / 8), r.getY(), r.getBottom());
    g.drawHorizontalLine ((int) r.getCentreY(), r.getX(), r.getRight());
    if (data.size() < 16) return;
    int n = (int) data.size(), len = n / 2, start = n - len;
    if (period > 8 && period * 2 < n)
    {
        len = (int) (period * 2);
        // find a rising zero crossing near the end for a stable trace
        start = n - len - 1;
        for (int i = n - len - 1; i > n - len - (int) period - 1 && i > 1; --i)
            if (data[(size_t) i - 1] < 0 && data[(size_t) i] >= 0) { start = i; break; }
    }
    float mx = 1e-4f;
    for (int i = start; i < start + len; ++i) mx = std::max (mx, std::abs (data[(size_t) i]));
    float gain = juce::jmin (1.0f / mx, 40.0f) * 0.9f;
    juce::Path p;
    for (int i = 0; i < len; ++i)
    {
        float x = r.getX() + r.getWidth() * i / (float) (len - 1);
        float y = r.getCentreY() - data[(size_t) (start + i)] * gain * r.getHeight() * 0.48f;
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    // glow + line for a bit of depth
    g.setColour (juce::Colours::white.withAlpha (0.12f)); g.strokePath (p, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved));
    g.setColour (col::white); g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved));
}

// ------------------------------------------------------------ xy

void XYView::setData (const float* l, const float* r, int n)
{
    L.assign (l, l + n); R.assign (r, r + n);
    double lr = 0, ll = 0, rr = 0;
    for (int i = 0; i < n; ++i) { lr += (double) l[i] * r[i]; ll += (double) l[i] * l[i]; rr += (double) r[i] * r[i]; }
    correlation = (ll > 1e-9 && rr > 1e-9) ? lr / std::sqrt (ll * rr) : 1.0;
    repaint();
}

void XYView::paintScreen (juce::Graphics& g, juce::Rectangle<float> r)
{
    auto c = r.getCentre();
    float rad = juce::jmin (r.getWidth(), r.getHeight()) * 0.45f;
    g.setColour (col::grid);
    g.drawLine (c.x - rad, c.y + rad, c.x + rad, c.y - rad);
    g.drawLine (c.x - rad, c.y - rad, c.x + rad, c.y + rad);
    g.drawVerticalLine ((int) c.x, c.y - rad, c.y + rad);
    float mx = 1e-4f;
    for (size_t i = 0; i < L.size(); ++i) mx = std::max ({ mx, std::abs (L[i]), std::abs (R[i]) });
    float gain = juce::jmin (1.0f / mx, 30.0f) * 0.7f;
    juce::Path p;
    for (size_t i = 0; i < L.size(); i += 2)
    {
        float m = (L[i] + R[i]) * 0.7071f, s = (L[i] - R[i]) * 0.7071f;
        float x = c.x + s * gain * rad, y = c.y - m * gain * rad;
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    g.setColour (juce::Colours::white.withAlpha (0.7f)); g.strokePath (p, juce::PathStrokeType (1.0f));
    g.setFont (font (11)); g.setColour (col::text3);
    g.drawText ("correlation " + juce::String (correlation, 2), r.toNearestInt().removeFromBottom (16), juce::Justification::centred);
}

// ------------------------------------------------------------ tuner

void TunerDial::setPitch (double h, double a)
{
    a4 = a;
    if (h > 0)
    {
        double m = midiFromHz (h, a4), cents = (m - std::round (m)) * 100;
        smooth = has ? smooth * 0.7 + cents * 0.3 : cents;
        hz = h; has = true;
    }
    else has = false;
    repaint();
}

void TunerDial::paintScreen (juce::Graphics& g, juce::Rectangle<float> r)
{
    auto dial = r.withTrimmedBottom (24);   // the readout line lives under the pivot
    auto c = juce::Point<float> (dial.getCentreX(), dial.getBottom() - 8);
    float rad = juce::jmin (r.getWidth() * 0.46f, dial.getHeight() * 0.9f);
    // tick arc: -50..+50 cents across 100 degrees
    for (int t = -50; t <= 50; t += 5)
    {
        float a = juce::degreesToRadians (t * 1.0f);
        float r0 = rad * (t % 25 == 0 ? 0.86f : 0.92f);
        g.setColour (t == 0 ? col::white : col::text3);
        g.drawLine (c.x + std::sin (a) * r0, c.y - std::cos (a) * r0, c.x + std::sin (a) * rad, c.y - std::cos (a) * rad, t == 0 ? 2.5f : 1.0f);
    }
    if (! has)
    {
        g.setFont (font (r.getHeight() * 0.18f, 0)); g.setColour (col::text3);
        g.drawText ("-", dial.toNearestInt().withTrimmedBottom ((int) (dial.getHeight() * 0.3f)), juce::Justification::centred);
        return;
    }
    double m = midiFromHz (hz, a4);
    int note = (int) std::lround (m);
    float a = juce::degreesToRadians ((float) juce::jlimit (-50.0, 50.0, smooth));
    // needle with a shadow for depth
    juce::Line<float> needle (c.x, c.y, c.x + std::sin (a) * rad * 0.97f, c.y - std::cos (a) * rad * 0.97f);
    g.setColour (juce::Colours::black); g.drawLine (needle.withShortenedStart (0).toFloat(), 6.0f);
    g.setColour (col::white); g.drawLine (needle, 2.2f);
    g.fillEllipse (juce::Rectangle<float> (14, 14).withCentre (c));
    bool inTune = std::abs (smooth) < 3;
    g.setFont (font (dial.getHeight() * 0.3f, inTune ? 2 : 0));
    g.setColour (inTune ? col::white : col::text);
    g.drawText (pitchClass (note), dial.toNearestInt().withHeight ((int) (dial.getHeight() * 0.5f)).translated (0, (int) (dial.getHeight() * 0.1f)), juce::Justification::centred);
    g.setFont (font (13)); g.setColour (col::text2);
    g.drawText (noteName (note) + "   " + juce::String (hz, hz < 100 ? 2 : 1) + " Hz   " + (smooth >= 0 ? "+" : "") + juce::String (smooth, 1) + " cents",
                r.toNearestInt().removeFromBottom (18), juce::Justification::centred);
}

// ------------------------------------------------------------ led meter

void LedMeter::setLevels (float p, float r)
{
    peak = p; rms = r;
    auto now = juce::Time::getMillisecondCounter();
    if (p >= hold || now - holdTime > 1500) { hold = p; holdTime = now; }
    repaint();
}

void LedMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    drawScreen (g, r, 4);
    auto in = r.reduced (3);
    const int leds = juce::jmax (8, (int) (in.getHeight() / 5));
    float lh = in.getHeight() / leds;
    auto level = [] (float db) { return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f); };
    float pk = level (peak), rm = level (rms), hd = level (hold);
    for (int i = 0; i < leds; ++i)
    {
        float t = (i + 0.5f) / leds;
        auto led = juce::Rectangle<float> (in.getX(), in.getBottom() - (i + 1) * lh + 1, in.getWidth(), lh - 1.5f);
        float a = t <= rm ? 0.95f : t <= pk ? 0.45f : 0.07f;
        if (std::abs (t - hd) < 0.5f / leds) a = 1.0f;
        // the top (clip) section is drawn hollow so it reads differently in monochrome
        if (t > 0.95f && t <= pk) { g.setColour (col::white); g.drawRect (led, 1.2f); }
        g.setColour (juce::Colours::white.withAlpha (a));
        g.fillRect (led.reduced (t > 0.95f ? 2.5f : 0, 0));
    }
}

// ------------------------------------------------------------ waveform

void WaveView::setView (int start, int length)
{
    if (audio == nullptr) return;
    const int n = audio->getNumSamples();
    viewLen = juce::jlimit (juce::jmin (n, 256), juce::jmax (1, n), length);
    viewStart = juce::jlimit (0, juce::jmax (0, n - viewLen), start);
    rebuild();
}

void WaveView::rebuild()
{
    mins.clear(); maxs.clear();
    int w = juce::jmax (1, (int) screenArea().reduced (6).getWidth());
    if (audio == nullptr || audio->getNumSamples() == 0) { repaint(); return; }
    const int n = audio->getNumSamples();
    if (viewLen <= 0 || viewStart + viewLen > n) { viewStart = 0; viewLen = n; }
    mins.assign ((size_t) w, 0); maxs.assign ((size_t) w, 0);
    for (int x = 0; x < w; ++x)
    {
        int a = viewStart + (int) ((juce::int64) viewLen * x / w);
        int b = juce::jmin (n, juce::jmax (a + 1, viewStart + (int) ((juce::int64) viewLen * (x + 1) / w)));
        if (a >= n) break;
        float lo = 0, hi = 0;
        for (int c = 0; c < audio->getNumChannels(); ++c)
        {
            auto mm = juce::FloatVectorOperations::findMinAndMax (audio->getReadPointer (c, a), b - a);
            lo = juce::jmin (lo, mm.getStart()); hi = juce::jmax (hi, mm.getEnd());
        }
        mins[(size_t) x] = lo; maxs[(size_t) x] = hi;
    }
    repaint();
}

float WaveView::xAtSample (int s) const
{
    auto r = screenArea().reduced (6);
    return r.getX() + r.getWidth() * (float) (s - viewStart) / (float) juce::jmax (1, viewLen);
}

int WaveView::sampleAtX (float x) const
{
    auto r = screenArea().reduced (6);
    int n = audio != nullptr ? audio->getNumSamples() : 0;
    return juce::jlimit (0, juce::jmax (0, n - 1), viewStart + (int) ((x - r.getX()) / r.getWidth() * viewLen));
}

void WaveView::paintScreen (juce::Graphics& g, juce::Rectangle<float> r)
{
    if (mins.empty())
    {
        g.setFont (font (14)); g.setColour (col::text3);
        g.drawText ("drop an audio file here, or record one", r.toNearestInt(), juce::Justification::centred);
        return;
    }
    g.saveState();
    g.reduceClipRegion (r.toNearestInt());
    // selected slice glow
    if (selected >= 0 && selected < (int) slices.size())
    {
        float x0 = xAtSample (slices[(size_t) selected]);
        float x1 = selected + 1 < (int) slices.size() ? xAtSample (slices[(size_t) selected + 1]) : xAtSample (audio->getNumSamples());
        g.setColour (juce::Colours::white.withAlpha (0.1f));
        g.fillRect (juce::Rectangle<float> (x0, r.getY(), x1 - x0, r.getHeight()));
    }
    float mid = r.getCentreY();
    g.setColour (juce::Colours::white.withAlpha (0.85f));
    for (int x = 0; x < (int) mins.size(); ++x)
        g.drawVerticalLine ((int) (r.getX() + (float) x), mid - maxs[(size_t) x] * r.getHeight() * 0.46f, mid - mins[(size_t) x] * r.getHeight() * 0.46f + 1);
    g.setColour (col::grid); g.drawHorizontalLine ((int) mid, r.getX(), r.getRight());
    // slice markers: numbered tabs (plus a tag such as K / S / H)
    g.setFont (font (10, 2));
    for (size_t i = 0; i < slices.size(); ++i)
    {
        float x = xAtSample (slices[i]);
        if (x < r.getX() - 40 || x > r.getRight()) continue;
        g.setColour (juce::Colours::white.withAlpha ((int) i == selected ? 1.0f : 0.55f));
        g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
        juce::String t (juce::String ((int) i + 1));
        if (i < tags.size() && tags[i].isNotEmpty()) t << " " << tags[i];
        auto tab = juce::Rectangle<float> (x, r.getY(), juce::jmax (20.0f, 9.0f + 6.5f * t.length()), 13);
        g.fillRect (tab);
        g.setColour (col::black);
        g.drawText (t, tab.toNearestInt(), juce::Justification::centred);
    }
    if (playhead >= 0)
    {
        g.setColour (col::white);
        g.drawVerticalLine ((int) xAtSample (playhead), r.getY(), r.getBottom());
    }
    g.restoreState();
    // overview strip when zoomed in
    if (audio != nullptr && viewLen < audio->getNumSamples())
    {
        const float n = (float) audio->getNumSamples();
        auto strip = r.withTop (r.getBottom() - 4);
        g.setColour (juce::Colours::white.withAlpha (0.15f)); g.fillRect (strip);
        g.setColour (juce::Colours::white.withAlpha (0.7f));
        g.fillRect (strip.withX (r.getX() + r.getWidth() * viewStart / n).withWidth (juce::jmax (3.0f, r.getWidth() * viewLen / n)));
    }
}

void WaveView::addMarker (float x)
{
    if (onBeforeEdit) onBeforeEdit();
    slices.push_back (sampleAtX (x));
    std::sort (slices.begin(), slices.end());
    slices.erase (std::unique (slices.begin(), slices.end()), slices.end());
    if (onSlicesEdited) onSlicesEdited();
    repaint();
}

void WaveView::mouseDown (const juce::MouseEvent& e)
{
    if (audio == nullptr) return;
    // grab a marker if close to one
    dragging = -1;
    for (size_t i = 0; i < slices.size(); ++i)
        if (std::abs (xAtSample (slices[i]) - e.position.x) < 5) dragging = (int) i;
    if (editable && dragging < 0 && e.mods.isAltDown()) { addMarker (e.position.x); return; }
    if (editable && dragging >= 0 && e.mods.isPopupMenu())
    {
        if (onBeforeEdit) onBeforeEdit();
        slices.erase (slices.begin() + dragging);
        dragging = -1;
        if (onSlicesEdited) onSlicesEdited();
        repaint();
        return;
    }
    if (editable && dragging >= 0 && onBeforeEdit) onBeforeEdit();
    if (dragging < 0)
    {
        int s = sampleAtX (e.position.x), idx = -1;
        for (size_t i = 0; i < slices.size(); ++i) if (slices[i] <= s) idx = (int) i;
        selected = idx;
        repaint();
        if (onSliceClicked && idx >= 0) onSliceClicked (idx);
    }
}

void WaveView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (audio == nullptr || ! editable || e.mods.isPopupMenu()) return;
    for (auto s : slices) if (std::abs (xAtSample (s) - e.position.x) < 5) return;
    addMarker (e.position.x);
}

void WaveView::mouseDrag (const juce::MouseEvent& e)
{
    if (! editable || dragging < 0 || dragging >= (int) slices.size()) return;
    // a marker can't pass its neighbours, so the slices always stay in order
    int lo = dragging > 0 ? slices[(size_t) dragging - 1] + 1 : 0;
    int hi = dragging + 1 < (int) slices.size() ? slices[(size_t) dragging + 1] - 1 : (audio != nullptr ? audio->getNumSamples() - 2 : 0);
    slices[(size_t) dragging] = juce::jlimit (lo, juce::jmax (lo, hi), sampleAtX (e.position.x));
    repaint();
}

void WaveView::mouseUp (const juce::MouseEvent&)
{
    if (dragging >= 0)
    {
        std::sort (slices.begin(), slices.end());
        slices.erase (std::unique (slices.begin(), slices.end()), slices.end());
        dragging = -1;
        if (onSlicesEdited) onSlicesEdited();
    }
}

void WaveView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (! zoomable || audio == nullptr) { Screen::mouseWheelMove (e, w); return; }
    const float dy = w.isReversed ? -w.deltaY : w.deltaY;
    if (e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY))
    {
        float d = std::abs (w.deltaX) > std::abs (w.deltaY) ? -w.deltaX : -dy;
        setView (viewStart + (int) (d * viewLen * 0.5f), viewLen);
        return;
    }
    const int anchor = sampleAtX (e.position.x);
    const double zoom = std::pow (0.8, dy * 4.0);
    const int newLen = (int) (viewLen * zoom);
    const double frac = (anchor - viewStart) / (double) juce::jmax (1, viewLen);
    setView (anchor - (int) (frac * newLen), newLen);
}

// ------------------------------------------------------------ wavetable stack

void StackView::paintScreen (juce::Graphics& g, juce::Rectangle<float> r)
{
    if (frames.empty()) return;
    const int F = (int) frames.size();
    int hi = juce::roundToInt (pos * (F - 1));
    float dx = r.getWidth() * 0.22f, dy = r.getHeight() * 0.42f, w = r.getWidth() - dx - 12, amp = r.getHeight() * 0.17f;
    for (int f = 0; f < F; ++f)   // back (first frame, top right) to front (last frame, bottom left)
    {
        float d = F == 1 ? 0 : f / (float) (F - 1);
        float ox = r.getX() + 6 + dx * (1 - d), oy = r.getY() + r.getHeight() * 0.3f + dy * d;
        const auto& w0 = frames[(size_t) f];
        juce::Path p;
        int pts = 128;
        for (int i = 0; i <= pts; ++i)
        {
            float x = ox + w * i / pts, y = oy - w0[(size_t) juce::jmin ((int) w0.size() - 1, (int) ((juce::int64) i * (juce::int64) w0.size() / pts))] * amp;
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        juce::Path fill (p); fill.lineTo (ox + w, oy + amp + 4); fill.lineTo (ox, oy + amp + 4); fill.closeSubPath();
        g.setColour (col::screen.withAlpha (0.85f)); g.fillPath (fill);
        g.setColour (f == hi ? col::white : juce::Colours::white.withAlpha (F > 24 ? 0.16f : 0.3f));
        g.strokePath (p, juce::PathStrokeType (f == hi ? 2.0f : 1.0f));
    }
    g.setFont (font (11)); g.setColour (col::text3);
    g.drawText ("frame " + juce::String (hi + 1) + " / " + juce::String (F), r.toNearestInt().removeFromTop (14), juce::Justification::left);
}

void StackView::mouseDrag (const juce::MouseEvent& e)
{
    pos = juce::jlimit (0.0f, 1.0f, e.position.x / (float) getWidth());
    if (onScrub) onScrub (pos);
    repaint();
}

// ------------------------------------------------------------ 3D bars

void BarChart3D::setBarsRange (std::vector<Bar> b, double l, double h, juce::String u)
{
    bars = std::move (b); lo = l; hi = h; uni = true; unit = std::move (u); repaint();
}

void BarChart3D::setBars (std::vector<Bar> b, double rng, juce::String u) { bars = std::move (b); range = rng; uni = false; unit = std::move (u); repaint(); }

void BarChart3D::paintScreen (juce::Graphics& g, juce::Rectangle<float> r)
{
    r.removeFromBottom (18);
    r.removeFromTop (8);
    // maps a value to a y position, and gives the baseline bars grow from
    auto yOf = [&] (double v) {
        if (uni) return r.getBottom() - (float) ((juce::jlimit (lo, hi, v) - lo) / (hi - lo)) * r.getHeight() * 0.92f;
        return r.getCentreY() - (float) (juce::jlimit (-range, range, v) / range) * r.getHeight() * 0.45f;
    };
    const float base = uni ? r.getBottom() : r.getCentreY();
    std::vector<double> grid;
    if (uni) for (int k = 0; k <= 4; ++k) grid.push_back (lo + (hi - lo) * k / 4);
    else grid = { -range, -range / 2, 0.0, range / 2, range };
    for (double v : grid)
    {
        float y = yOf (v);
        g.setColour (col::grid);
        g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
        g.setFont (font (10)); g.setColour (col::text3);
        g.drawText ((v > 0 && ! uni ? "+" : "") + juce::String ((int) std::round (v)) + unit, (int) r.getX() + 4, (int) y - 12, 60, 11, juce::Justification::left);
    }
    if (bars.empty()) return;
    float bw = (r.getWidth() - 60) / bars.size();
    float depth = juce::jmin (10.0f, bw * 0.25f);
    for (size_t i = 0; i < bars.size(); ++i)
    {
        float x = r.getX() + 54 + bw * i + bw * 0.18f, w = bw * 0.56f;
        if (! bars[i].valid)
        {
            g.setColour (col::text3); g.setFont (font (10));
            g.drawText ("?", (int) x, (int) base - 16, (int) w, 16, juce::Justification::centred);
        }
        else
        {
            float y = yOf (bars[i].value);
            auto front = juce::Rectangle<float> (x, juce::jmin (y, base), w, std::abs (base - y));
            juce::Path side; side.addQuadrilateral (front.getRight(), front.getY(), front.getRight() + depth, front.getY() - depth,
                                                    front.getRight() + depth, front.getBottom() - depth, front.getRight(), front.getBottom());
            juce::Path top; top.addQuadrilateral (front.getX(), front.getY(), front.getX() + depth, front.getY() - depth,
                                                  front.getRight() + depth, front.getY() - depth, front.getRight(), front.getY());
            g.setColour (juce::Colour (0xff6a6a6a)); g.fillPath (side);
            g.setColour (juce::Colour (0xffd0d0d0)); g.fillPath (top);
            g.setGradientFill (juce::ColourGradient (col::white, x, front.getY(), juce::Colour (0xff9a9a9a), x, front.getBottom(), false));
            g.fillRect (front);
        }
        g.setFont (font (10.5f)); g.setColour (col::text2);
        g.drawText (bars[i].label, (int) (x - bw * 0.2f), (int) r.getBottom() + 2, (int) (bw), 14, juce::Justification::centred);
    }
}

// ------------------------------------------------------------ helpers

std::unique_ptr<juce::TextButton> makeButton (const juce::String& text, std::function<void()> onClick, bool toggle)
{
    auto b = std::make_unique<juce::TextButton> (text);
    b->onClick = std::move (onClick);
    if (toggle) b->setClickingTogglesState (true);
    return b;
}

std::unique_ptr<juce::Label> makeLabel (const juce::String& text, float size, juce::Colour c, int weight)
{
    auto l = std::make_unique<juce::Label> (juce::String(), text);
    l->setFont (font (size, weight));
    l->setColour (juce::Label::textColourId, c);
    l->setMinimumHorizontalScale (0.8f);
    return l;
}

void styleCombo (juce::ComboBox& c) { c.setJustificationType (juce::Justification::centredLeft); }

juce::String fmtDb (double db) { return db <= -119 ? juce::String ("-inf") : juce::String (db, 1); }

juce::String fmtTime (double s)
{
    int m = (int) (s / 60); double sec = s - m * 60;
    return juce::String (m) + ":" + juce::String (sec, 1).paddedLeft ('0', 4);
}
}
