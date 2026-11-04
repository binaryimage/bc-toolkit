#include "Renderer.h"
#include "Common/Look.h"

namespace bcv
{
juce::StringArray tintNames() { return { "White", "Ice", "Amber", "Red", "Phosphor green", "Violet", "Multi-colour", "Ice multi-colour" }; }
juce::StringArray backgroundNames() { return { "Black", "Cover art (dimmed)", "Paper" }; }
juce::StringArray textNames() { return { "Bottom left", "Centred", "Hidden" }; }

Palette paletteFor (const Look& l)
{
    static const juce::Colour tints[] = { juce::Colour (0xffffffff), juce::Colour (0xffcfe6ff), juce::Colour (0xffffc46e),
                                          juce::Colour (0xffff4a3d), juce::Colour (0xff86ffa6), juce::Colour (0xffcaa6ff), juce::Colour (0xffffffff), juce::Colour (0xffffffff) };
    Palette p;
    p.fg = tints[juce::jlimit (0, kIceColour, l.tint)];
    if (l.background == 2) { p.bg = juce::Colour (0xffe8e4dc); p.fg = juce::Colour (0xff141414); }
    return p;
}

void Renderer::makeBackdrop (juce::Rectangle<int> size, const juce::Image& cover)
{
    backdropSize = size; backdropSource = cover;
    backdrop = juce::Image (juce::Image::RGB, size.getWidth(), size.getHeight(), true);
    juce::Graphics g (backdrop);
    g.fillAll (juce::Colours::black);
    if (! cover.isValid()) return;
    // cheap blur: shrink the cover to a few dozen pixels, then stretch it back up smoothly
    int sw = 32, sh = juce::jmax (1, (int) std::round (32.0 * size.getHeight() / size.getWidth()));
    juce::Image small (juce::Image::RGB, sw, sh, true);
    {
        juce::Graphics gs (small);
        gs.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        // centre-crop the cover to the frame's shape
        float ca = (float) cover.getWidth() / cover.getHeight(), fa = (float) size.getWidth() / size.getHeight();
        juce::Rectangle<int> src = cover.getBounds();
        if (ca > fa) src = src.withSizeKeepingCentre ((int) (cover.getHeight() * fa), cover.getHeight());
        else src = src.withSizeKeepingCentre (cover.getWidth(), (int) (cover.getWidth() / fa));
        gs.drawImage (cover, 0, 0, sw, sh, src.getX(), src.getY(), src.getWidth(), src.getHeight());
    }
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (small, size.withZeroOrigin().toFloat());
    g.fillAll (juce::Colours::black.withAlpha (0.72f));
}

void Renderer::render (juce::Graphics& g, juce::Rectangle<float> r, const Frame& f, Viz& viz, const Look& look)
{
    const auto p = paletteFor (look);
    const float s = r.getHeight() / 1080.0f;

    // background
    if (look.background == 1 && look.cover.isValid())
    {
        auto size = r.toNearestInt().withZeroOrigin();
        if (! backdrop.isValid() || size != backdropSize || look.cover != backdropSource) makeBackdrop (size, look.cover);
        g.drawImageAt (backdrop, (int) r.getX(), (int) r.getY());
    }
    else
    {
        g.setColour (p.bg); g.fillRect (r);
    }

    if (look.tint == kMultiColour || look.tint == kIceColour)
    {
        auto size = r.toNearestInt().withZeroOrigin();
        if (! layer.isValid() || layer.getBounds() != size) layer = juce::Image (juce::Image::ARGB, juce::jmax (1, size.getWidth()), juce::jmax (1, size.getHeight()), true);
        {
            juce::Graphics gl (layer);
            gl.fillAll (juce::Colours::black);
            Palette vp;   // white on black; the colour comes afterwards
            viz.draw (gl, size.toFloat(), f, vp, look.intensity);
        }
        colourise (f, look.background == 2, look.tint == kIceColour);
        g.drawImageAt (layer, (int) r.getX(), (int) r.getY());
    }
    else
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (r.toNearestInt());
        auto vp = p;
        if (look.background == 1) vp.bg = juce::Colours::black;   // trails fade toward black over the cover
        viz.draw (g, r, f, vp, look.intensity);
    }

    if (look.vignette)
    {
        auto c = r.getCentre();
        juce::ColourGradient vg (p.bg.withAlpha (0.0f), c.x, c.y, p.bg.withAlpha (0.65f), r.getX(), r.getY(), true);
        vg.addColour (0.55, p.bg.withAlpha (0.0f));
        g.setGradientFill (vg); g.fillRect (r);
        juce::Random gr (f.index * 104729 + 1);
        for (int i = 0; i < 900; ++i)
        {
            g.setColour (p.fg.withAlpha (gr.nextFloat() * 0.05f));
            g.fillRect (r.getX() + gr.nextFloat() * r.getWidth(), r.getY() + gr.nextFloat() * r.getHeight(), 2 * s, 2 * s);
        }
    }

    // titles, easing in after the first second
    if (look.text != 2 && (look.title.isNotEmpty() || look.artist.isNotEmpty()))
    {
        float a = juce::jlimit (0.0f, 1.0f, (float) (f.t - 0.6) / 1.2f);
        if (! look.fades) a = 1;
        auto ink = p.fg.withMultipliedAlpha (a);
        const float titleH = 64 * s, artistH = 30 * s;
        float badge = look.cover.isValid() && look.coverBadge ? 150 * s : 0;
        if (look.text == 0)
        {
            float margin = 80 * s;
            auto block = juce::Rectangle<float> (r.getX() + margin, r.getBottom() - margin - juce::jmax (badge, titleH + artistH + 14 * s),
                                                 r.getWidth() - margin * 2, juce::jmax (badge, titleH + artistH + 14 * s));
            if (badge > 0)
            {
                auto b = block.removeFromLeft (badge).withSizeKeepingCentre (badge, badge);
                g.setOpacity (a);
                g.drawImage (look.cover, b, juce::RectanglePlacement::centred | juce::RectanglePlacement::fillDestination);
                g.setOpacity (1);
                g.setColour (ink.withMultipliedAlpha (0.35f)); g.drawRect (b, 1.5f * s);
                block.removeFromLeft (32 * s);
            }
            auto textArea = block.withSizeKeepingCentre (block.getWidth(), titleH + artistH + 14 * s);
            g.setColour (ink); g.setFont (tk::font (titleH, 2));
            g.drawText (look.title, textArea.removeFromTop (titleH), juce::Justification::bottomLeft, true);
            textArea.removeFromTop (14 * s);
            g.setColour (ink.withMultipliedAlpha (0.7f)); g.setFont (tk::font (artistH, 1).withExtraKerningFactor (0.18f));
            g.drawText (look.artist.toUpperCase(), textArea, juce::Justification::topLeft, true);
        }
        else
        {
            auto area = r.withSizeKeepingCentre (r.getWidth() * 0.9f, titleH + artistH + 20 * s);
            g.setColour (p.bg.withAlpha (0.45f * a));
            g.fillRect (area.withSizeKeepingCentre (r.getWidth(), area.getHeight() + 60 * s));
            g.setColour (ink); g.setFont (tk::font (titleH, 2));
            g.drawText (look.title, area.removeFromTop (titleH), juce::Justification::centred, true);
            area.removeFromTop (20 * s);
            g.setColour (ink.withMultipliedAlpha (0.7f)); g.setFont (tk::font (artistH, 1).withExtraKerningFactor (0.18f));
            g.drawText (look.artist.toUpperCase(), area, juce::Justification::centred, true);
        }
    }

    if (look.progress && f.duration > 0)
    {
        float u = (float) juce::jlimit (0.0, 1.0, f.t / f.duration);
        g.setColour (p.fg.withAlpha (0.15f)); g.fillRect (r.getX(), r.getBottom() - 6 * s, r.getWidth(), 6 * s);
        g.setColour (p.fg.withAlpha (0.8f)); g.fillRect (r.getX(), r.getBottom() - 6 * s, r.getWidth() * u, 6 * s);
    }

    if (look.fades)
    {
        double fade = juce::jmin (1.0, f.t / 1.5, (f.duration - f.t) / 2.0);
        if (fade < 1) { g.setColour (juce::Colours::black.withAlpha ((float) juce::jlimit (0.0, 1.0, 1 - fade))); g.fillRect (r); }
    }
}

void Renderer::colourise (const Frame& f, bool paper, bool icy)
{
    const int w = layer.getWidth(), h = layer.getHeight();
    if (fieldSize != layer.getBounds())
    {
        // hue = angle round the centre + distance out from it: a rainbow wheel with a spiral twist
        fieldSize = layer.getBounds();
        hueField.assign ((size_t) w * (size_t) h, 0.0f);
        const float cx = w * 0.5f, cy = h * 0.5f, norm = 1.0f / (float) juce::jmax (1, h);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                float dx = x - cx, dy = y - cy;
                hueField[(size_t) y * (size_t) w + (size_t) x] = std::atan2 (dy, dx) / juce::MathConstants<float>::twoPi
                                                                 + std::sqrt (dx * dx + dy * dy) * norm * 0.9f;
            }
    }
    constexpr int kLut = 1024;
    juce::uint8 lr[kLut], lg[kLut], lb[kLut];
    for (int i = 0; i < kLut; ++i)
    {
        const float x = (float) i / kLut;
        juce::Colour c;
        if (icy)
        {
            // cyan -> blue -> violet and back (no seam), with frosty near-white streaks running through it
            float hue = 0.48f + 0.27f * (0.5f - 0.5f * std::cos (x * juce::MathConstants<float>::twoPi));
            float sat = 0.5f + 0.35f * std::sin (x * juce::MathConstants<float>::twoPi * 3.0f);
            c = juce::Colour::fromHSV (hue, paper ? juce::jmin (1.0f, sat + 0.3f) : sat, paper ? 0.6f : 1.0f, 1.0f);
        }
        else c = juce::Colour::fromHSV (x, paper ? 1.0f : 0.85f, paper ? 0.72f : 1.0f, 1.0f);
        lr[i] = c.getRed(); lg[i] = c.getGreen(); lb[i] = c.getBlue();
    }
    // the wheel turns slowly, kicks forward on beats, and drifts faster when the track is loud
    const float offset = (float) std::fmod (f.t * (0.04 + 0.05 * f.energy) + f.beat * 0.07, 1.0);
    juce::Image::BitmapData bd (layer, juce::Image::BitmapData::readWrite);
    for (int y = 0; y < h; ++y)
    {
        auto* line = bd.getLinePointer (y);
        const float* hf = hueField.data() + (size_t) y * (size_t) w;
        for (int x = 0; x < w; ++x)
        {
            auto* px = reinterpret_cast<juce::PixelARGB*> (line + (size_t) x * (size_t) bd.pixelStride);
            const juce::uint32 lum = juce::jmax (px->getRed(), px->getGreen(), px->getBlue());
            float hv = hf[x] + offset;
            const int i = (int) ((hv - std::floor (hv)) * kLut) & (kLut - 1);
            px->setARGB ((juce::uint8) lum, (juce::uint8) (lr[i] * lum / 255), (juce::uint8) (lg[i] * lum / 255), (juce::uint8) (lb[i] * lum / 255));
        }
    }
}

juce::AudioBuffer<float> makeDemoTrack (double sr, double seconds)
{
    const int n = (int) (sr * seconds);
    juce::AudioBuffer<float> b (2, n);
    b.clear();
    juce::Random rnd (42);
    const double bpm = 124, beat = 60.0 / bpm;
    const double roots[] = { 55.0, 43.65, 49.0, 41.2 };     // A1, F1, G1, E1
    double kickPh = 0, bassPh = 0, padPh[6] {};
    float hpState = 0, prevNoise = 0;
    for (int i = 0; i < n; ++i)
    {
        double t = i / sr, inBeat = std::fmod (t, beat), inBar = std::fmod (t, beat * 4);
        int bar = (int) (t / (beat * 4)) % 4;
        double root = roots[bar];
        // kick: pitch sweep with a fast decay
        double kf = 45 + 110 * std::exp (-inBeat * 40);
        kickPh += kf / sr;
        float kick = (float) (std::sin (kickPh * juce::MathConstants<double>::twoPi) * std::exp (-inBeat * 7));
        // snare on 2 and 4
        double sn = std::fmod (inBar - beat, beat * 2);
        float noise = rnd.nextFloat() * 2 - 1;
        float snare = (inBar >= beat && sn < beat) ? noise * (float) std::exp (-sn * 18) * 0.5f : 0.0f;
        // hats: highpassed noise on the off eighths
        double h8 = std::fmod (t, beat / 2);
        hpState = 0.6f * (hpState + noise - prevNoise); prevNoise = noise;
        float hat = hpState * (float) std::exp (-h8 * 60) * 0.25f;
        // bass: square-ish, sidechained by the kick
        bassPh += root / sr;
        float bass = (float) (std::tanh (std::sin (bassPh * juce::MathConstants<double>::twoPi) * 3) * 0.25 * (1 - std::exp (-inBeat * 12)));
        // pad: minor triad two octaves up, slightly detuned per side
        float padL = 0, padR = 0;
        const double ratios[] = { 4.0, 4.0 * 1.1892, 4.0 * 1.4983 };
        for (int v = 0; v < 3; ++v)
        {
            padPh[v * 2] += root * ratios[v] * 1.002 / sr; padPh[v * 2 + 1] += root * ratios[v] * 0.998 / sr;
            padL += (float) std::sin (padPh[v * 2] * juce::MathConstants<double>::twoPi);
            padR += (float) std::sin (padPh[v * 2 + 1] * juce::MathConstants<double>::twoPi);
        }
        float swell = 0.05f + 0.04f * (float) std::sin (t * 0.7);
        float mono = kick * 0.8f + snare + bass;
        b.setSample (0, i, juce::jlimit (-1.0f, 1.0f, mono + hat * 0.8f + padL * swell));
        b.setSample (1, i, juce::jlimit (-1.0f, 1.0f, mono + hat * 1.2f + padR * swell));
    }
    return b;
}
}
