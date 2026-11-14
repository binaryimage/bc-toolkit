#include "Renderer.h"
#include <cstring>
#include "Common/Look.h"

namespace bcv
{

namespace
{
    /** A mixed palette: colour stops round a loop, read by where a pixel is (field) and how bright it is. */
    struct Mix { const char* name; std::vector<juce::uint32> stops; int field; float spatial, bright; };
    // fields: 0 spiral wheel, 1 rings out from the centre, 2 top to bottom, 3 diagonal, 4 angle round the centre
    const std::vector<Mix>& mixes()
    {
        static const std::vector<Mix> m {
            { "Toxic Bloom",     { 0xff0b3d0b, 0xff7cff2e, 0xfff2ff3d, 0xffff2ed1 }, 1, 0.7f, 0.5f },
            { "Blood Moon",      { 0xff3a0006, 0xffc4001d, 0xffff6a1a, 0xfff3e6d0 }, 1, 0.5f, 0.7f },
            { "Acid Rain",       { 0xffb6ff00, 0xff00ffd5, 0xff0047ff, 0xff00ff6a }, 2, 0.8f, 0.3f },
            { "Ultraviolet",     { 0xff2a00ff, 0xff8f00ff, 0xffff2bd6, 0xfff5e6ff }, 0, 0.7f, 0.4f },
            { "Ember Rot",       { 0xff4a0500, 0xffd43a00, 0xffffb300, 0xff7a1c00 }, 1, 0.4f, 0.8f },
            { "Deep Sea",        { 0xff001a4d, 0xff006d77, 0xff00e5ff, 0xff7affd4 }, 2, 0.6f, 0.5f },
            { "Bruise",          { 0xff3b0a57, 0xff8a2b8a, 0xffc7d34a, 0xff2b4bd6 }, 4, 0.8f, 0.3f },
            { "Witch Light",     { 0xff00ff7b, 0xff00b3a6, 0xff7a3dff, 0xff2bff00 }, 0, 0.8f, 0.3f },
            { "Synth Dusk",      { 0xff3d0f6b, 0xffb5179e, 0xffff5f3d, 0xffffb347 }, 2, 0.9f, 0.2f },
            { "Oil Slick",       { 0xff5a189a, 0xff00a6a6, 0xffd4a017, 0xffd6246e, 0xff2ec22e }, 3, 1.2f, 0.4f },
            { "Thermal",         { 0xff0a0a40, 0xff6a00a8, 0xffe0003c, 0xffff8c00, 0xffffef5c, 0xffffffff }, 1, 0.1f, 1.0f },
            { "Night Vision",    { 0xff031a03, 0xff19c419, 0xff9dff9d, 0xfff0fff0 }, 1, 0.1f, 0.9f },
            { "Cyanotype",       { 0xff0b2a6f, 0xff2a6fdb, 0xffbfe3ff, 0xffffffff }, 1, 0.2f, 0.9f },
            { "Sepia Ghost",     { 0xff3b2412, 0xff8a6239, 0xffd9c19a, 0xfffff4e0 }, 2, 0.2f, 0.9f },
            { "Absinthe",        { 0xff2f6b00, 0xff8fd400, 0xffd9ff7a, 0xff00d48f }, 4, 0.7f, 0.4f },
            { "Venom",           { 0xff00330d, 0xff7aff00, 0xff6a00d4, 0xffc4ff4d }, 0, 0.8f, 0.4f },
            { "Magma",           { 0xff1a0000, 0xff9e0000, 0xffff4d00, 0xffffc400, 0xfffff4b0 }, 1, 0.2f, 1.0f },
            { "Nebula",          { 0xff0b0b5c, 0xff6a2bd6, 0xffff4fa8, 0xff3dd6ff }, 0, 0.9f, 0.3f },
            { "Fungal",          { 0xff4a2f1a, 0xffc49a3d, 0xffd8b8ff, 0xff3da69e }, 3, 0.8f, 0.4f },
            { "Bioluminescent",  { 0xff001233, 0xff00b4d8, 0xff38ff8a, 0xff00f5d4 }, 1, 0.6f, 0.6f },
            { "Static Storm",    { 0xff5c6b80, 0xffffffff, 0xff1f8fff, 0xffb3d9ff }, 2, 0.8f, 0.6f },
            { "Haunted VHS",     { 0xffff00c8, 0xff00e5ff, 0xfffff200, 0xff7a00ff }, 2, 1.4f, 0.2f },
            { "Graveyard Mist",  { 0xff4d5c4d, 0xff9fb3a6, 0xffc9dcf0, 0xfff4f8f6 }, 2, 0.4f, 0.7f },
            { "Lava Lamp",       { 0xffe0001b, 0xffff7b00, 0xffff4fd8, 0xff8a00d4 }, 1, 0.9f, 0.4f },
            { "Ectoplasm",       { 0xff7affc4, 0xffc8ff3d, 0xfff4fff0, 0xff3dffa8 }, 4, 0.6f, 0.5f },
            { "Chrome",          { 0xff2b2f36, 0xff9aa5b1, 0xffffffff, 0xff5c7a99 }, 2, 0.5f, 0.9f },
            { "Moth Wing",       { 0xff4a3220, 0xffa8763e, 0xffeedcb3, 0xffb5521c }, 4, 0.6f, 0.6f },
            { "Electric Eel",    { 0xfffff200, 0xff00f0ff, 0xff0040ff, 0xff9dff00 }, 3, 1.0f, 0.3f },
            { "Infrared",        { 0xff1a0010, 0xffff0066, 0xffff7aa8, 0xffffffff }, 2, 0.5f, 0.8f },
            { "Sulphur",         { 0xff6b6b00, 0xffd4c400, 0xfffff36b, 0xff8f7a00 }, 1, 0.6f, 0.5f },
            { "Cobalt Flame",    { 0xff00137a, 0xff0057ff, 0xff4dc4ff, 0xffe6f7ff }, 1, 0.3f, 0.9f },
            { "Rust and Patina", { 0xff8a3300, 0xffc26a2b, 0xff3d8f7a, 0xff5c2a12 }, 3, 0.8f, 0.4f },
            { "Poison Ivy",      { 0xff0f4d12, 0xff2bd43d, 0xffd40f2b, 0xff7aff8a }, 0, 0.8f, 0.4f },
            { "Plasma Arc",      { 0xff4b00a8, 0xff1f6bff, 0xffa8e6ff, 0xffffffff }, 4, 0.6f, 0.7f },
            { "Hellfire",        { 0xff200000, 0xffb30000, 0xffff5500, 0xffffd000 }, 2, 0.4f, 0.9f },
            { "Glacier",         { 0xff005f73, 0xff94d2bd, 0xfff0fbff, 0xff3f5f7a }, 3, 0.7f, 0.5f },
            { "Gasoline",        { 0xff3d0066, 0xff007a5c, 0xff1f3dff, 0xffa8742b }, 0, 1.2f, 0.3f },
            { "Ritual",          { 0xff5c0010, 0xffd4a017, 0xff1f0029, 0xffff3d3d }, 4, 0.7f, 0.5f },
            { "Mercury",         { 0xff3d4247, 0xffc0c8d0, 0xfff7fbff, 0xff8fa8c4 }, 1, 0.4f, 0.8f },
            { "Neon Grave",      { 0xffff1fa8, 0xff7a1fff, 0xffa8ff1f, 0xff1fd4ff }, 0, 1.0f, 0.3f },
           #if BCV_EXTRAS
            // long palettes (6-8 colours): made for fractals, where every depth gets its own colour
            { "Hellscape",        { 0xff0a0000, 0xff3d0000, 0xff8f0a00, 0xffd43a00, 0xffff8c1a, 0xffffd24d, 0xfffff6c7, 0xff5c0a2b }, 1, 0.3f, 0.9f },
            { "Psilocybin",       { 0xff1a0b2e, 0xff5b2a86, 0xffb83dba, 0xffff6f91, 0xffffc75f, 0xfff9f871, 0xff4fd1c5 }, 0, 0.6f, 0.7f },
            { "Opal",             { 0xff0d1b2a, 0xff3a86ff, 0xff8ecae6, 0xfff1faee, 0xffffafcc, 0xffcdb4db, 0xff80ffdb }, 3, 0.5f, 0.7f },
            { "Black Light",      { 0xff0b0014, 0xff3c00a8, 0xff7a00ff, 0xffff00e5, 0xff00f0ff, 0xffb6ff00 }, 4, 0.5f, 0.8f },
            { "Carrion",          { 0xff140f05, 0xff4a3b12, 0xff7d6b2a, 0xffa3a85a, 0xff6b1f1f, 0xffd9c9a3 }, 2, 0.4f, 0.8f },
            { "Abyss",            { 0xff00040d, 0xff001f3d, 0xff004e7a, 0xff00a3b8, 0xff7affe6, 0xffe0fff9, 0xff2a0f5c }, 1, 0.3f, 0.9f },
            { "Cathedral Glass",  { 0xff1a0033, 0xff0047ab, 0xff00a86b, 0xffffd700, 0xffe0115f, 0xff6a0dad, 0xffff7f00, 0xff00ced1 }, 4, 0.7f, 0.6f },
            { "Toxic Sunset",     { 0xff1b0d33, 0xff7a1e5c, 0xffe8344e, 0xffff8a3d, 0xfffff04d, 0xff8aff4d, 0xff1fd1a8 }, 2, 0.5f, 0.8f },
            { "Infernal Rainbow", { 0xff200000, 0xffff1f1f, 0xffff9500, 0xfffff200, 0xff1fff5a, 0xff1fb4ff, 0xff8a1fff, 0xffff1fcf }, 0, 0.6f, 0.8f },
            { "Ghost Orchid",     { 0xff120d1a, 0xff4b3a66, 0xff9b7fb8, 0xffe6d9f2, 0xffffffff, 0xffb8e0d2 }, 3, 0.4f, 0.8f },
            { "Deep Fry",         { 0xff2b0f00, 0xffff6a00, 0xffffe600, 0xffff00a8, 0xff00ffea, 0xffff2600 }, 1, 0.7f, 0.6f },
            { "Witch Spectrum",   { 0xff001a0d, 0xff00ff6a, 0xff00a3ff, 0xff6a00ff, 0xffff00d4, 0xffffe100, 0xff00330f }, 0, 0.6f, 0.8f },
           #endif
        };
        return m;
    }

    /** colour stops round a loop, blended smoothly, into a lookup table */
    void lutFromStops (const std::vector<juce::uint32>& st, bool paper, juce::uint8* lr, juce::uint8* lg, juce::uint8* lb, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            const float pos = (float) i / (float) n * (float) st.size();
            const int a = (int) pos % (int) st.size(), b = (a + 1) % (int) st.size();
            const float u = pos - std::floor (pos), su = u * u * (3 - 2 * u);
            auto c = juce::Colour (st[(size_t) a]).interpolatedWith (juce::Colour (st[(size_t) b]), su);
            if (paper) c = c.withMultipliedBrightness (0.7f).withMultipliedSaturation (1.2f);
            lr[i] = c.getRed(); lg[i] = c.getGreen(); lb[i] = c.getBlue();
        }
    }

    void buildLut (int tint, bool paper, juce::uint8* lr, juce::uint8* lg, juce::uint8* lb, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            const float x = (float) i / n;
            juce::Colour c;
            if (tint == kMultiColour) c = juce::Colour::fromHSV (x, paper ? 1.0f : 0.85f, paper ? 0.72f : 1.0f, 1.0f);
            else if (tint == kIceColour)
            {
                // cyan -> blue -> violet and back (no seam), with frosty near-white streaks running through it
                float hue = 0.48f + 0.27f * (0.5f - 0.5f * std::cos (x * juce::MathConstants<float>::twoPi));
                float sat = 0.5f + 0.35f * std::sin (x * juce::MathConstants<float>::twoPi * 3.0f);
                c = juce::Colour::fromHSV (hue, paper ? juce::jmin (1.0f, sat + 0.3f) : sat, paper ? 0.6f : 1.0f, 1.0f);
            }
            else
            {
                const auto& st = mixes()[(size_t) juce::jlimit (0, (int) mixes().size() - 1, tint - kFirstMix)].stops;
                const float pos = x * (float) st.size();
                const int a = (int) pos % (int) st.size(), b = (a + 1) % (int) st.size();
                const float u = pos - std::floor (pos), su = u * u * (3 - 2 * u);
                c = juce::Colour (st[(size_t) a]).interpolatedWith (juce::Colour (st[(size_t) b]), su);
                if (paper) c = c.withMultipliedBrightness (0.7f).withMultipliedSaturation (1.2f);
            }
            lr[i] = c.getRed(); lg[i] = c.getGreen(); lb[i] = c.getBlue();
        }
    }
}

juce::StringArray tintNames()
{
    juce::StringArray s { "White", "Ice", "Amber", "Red", "Phosphor green", "Violet", "Multi-colour", "Ice multi-colour" };
    for (auto& m : mixes()) s.add (m.name);
    return s;
}
juce::StringArray reactionNames() { return { "Off", "Subtle", "Wild" }; }
juce::StringArray effectNames() { return { "Motion", "Sparkles", "Glitch", "Shimmer", "Echoes", "Ghosts", "Trails", "Static", "Flash", "Colour motion" }; }
juce::StringArray effectHints()
{
    return { "Zoom punches on bass, sway on low mids, lean with panning, shake on hits",
             "Sparkles on the highs",
             "Sideways glitch slices on presence (2-6 kHz) hits",
             "Ripple wobble on the air (very top end)",
             "Zoomed copies when the mix gets busy",
             "Side-by-side ghost copies when the mix is wide",
             "Trails and slow dissolve on tails, gaps and silence",
             "TV static on noisy sounds",
             "White flash on drops",
             "Colours turning, kicking on beats and jumping on new sections (off: the palette holds still)" };
}
juce::StringArray mirrorNames() { return { "Mirror: follows the song", "Mirror: off", "Mirror: left-right", "Mirror: top-bottom", "Mirror: four-way", "Mirror: flipped" }; }
juce::StringArray backgroundNames() { return { "Black", "Cover art (dimmed)", "Paper" }; }
juce::StringArray textNames() { return { "Bottom left", "Centred", "Hidden" }; }
juce::StringArray flowNames() { return { "Gentle", "Flowing", "Racing" }; }

RolledPalette rollPalette (juce::uint32 seed)
{
    juce::Random r ((juce::int64) seed * 2862933555777941757LL + 3037000493LL);
    for (int i = 0; i < 3; ++i) r.nextInt();
    RolledPalette p;
    const int n = 3 + r.nextInt (6);                                     // 3 to 8 colours
    float hue = r.nextFloat();
    const float step = 0.04f + r.nextFloat() * 0.32f;                   // small steps = colours that go together, big = wild
    const int dark = r.nextInt (n), hot = (dark + 1 + r.nextInt (n - 1)) % n;
    for (int i = 0; i < n; ++i)
    {
        float sat = 0.72f + 0.28f * r.nextFloat(), val = 0.62f + 0.38f * r.nextFloat();
        if (i == dark) { val = 0.16f + 0.16f * r.nextFloat(); sat = 0.7f + 0.3f * r.nextFloat(); }
        if (i == hot)  { val = 1.0f; sat = 0.1f + 0.45f * r.nextFloat(); }
        p.stops.push_back (juce::Colour::fromHSV (hue - std::floor (hue), sat, val, 1.0f).getARGB());
        hue += (r.nextBool() ? 1.0f : -1.0f) * step * (0.5f + r.nextFloat());
    }
    p.field = r.nextInt (5);
    p.spatial = 0.2f + r.nextFloat() * 0.9f;
    p.bright = 0.35f + r.nextFloat() * 0.6f;
    return p;
}

juce::Colour rolledColour (juce::uint32 seed, float x)
{
    const auto p = rollPalette (seed);
    const int n = (int) p.stops.size();
    const int i = juce::jlimit (0, n - 1, (int) ((x - std::floor (x)) * (float) n));
    return juce::Colour (p.stops[(size_t) i]);
}

Palette paletteFor (const Look& l)
{
    static const juce::Colour tints[] = { juce::Colour (0xffffffff), juce::Colour (0xffcfe6ff), juce::Colour (0xffffc46e),
                                          juce::Colour (0xffff4a3d), juce::Colour (0xff86ffa6), juce::Colour (0xffcaa6ff) };
    Palette p;
    p.fg = l.tint < kMultiColour ? tints[juce::jmax (0, l.tint)] : juce::Colour (0xffffffff);
    if (l.background == 2) { p.bg = juce::Colour (0xffe8e4dc); p.fg = l.tint < kMultiColour && l.tint > 0 ? tints[l.tint].withMultipliedBrightness (0.45f) : juce::Colour (0xff141414); }
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
    render (g, r, f, viz, look, nullptr, 0.0f);
}

void Renderer::render (juce::Graphics& g, juce::Rectangle<float> r, const Frame& f, Viz& viz, const Look& look, Viz* next, float mix)
{
    const bool mixing = next != nullptr && next != &viz && mix > 0.0005f;
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

    const float react = look.reactions <= 0 ? 0.0f : look.reactions == 1 ? 0.5f : 1.0f;
    const bool fixedMirror = look.mirror >= mirrorLeftRight;
    pixelThreads() = workers != nullptr ? workers->getActive() : 0;   // styles that split their own pixel work use this many
    if (look.tint >= kMultiColour || look.rollA != 0 || react > 0 || fixedMirror || mixing)
    {
        auto size = r.toNearestInt().withZeroOrigin();
        if (! layer.isValid() || layer.getBounds() != size)
        {
            layer = juce::Image (juce::Image::ARGB, juce::jmax (1, size.getWidth()), juce::jmax (1, size.getHeight()), true);
            prevLum.assign ((size_t) layer.getWidth() * (size_t) layer.getHeight(), 0);
        }
        {
            // the motion is worked out once and shared, so both styles of a crossfade move together
            const bool motion = react > 0 && look.effectOn (fxMotion);
            const auto xf = motion ? reactTransform (f, size.toFloat(), react) : juce::AffineTransform();
            const float kk = look.intensity * (1 + react * (0.8f * f.swell + 0.6f * f.drop));
            auto drawInto = [&] (juce::Image& img, Viz& v)
            {
                juce::Graphics gl (img);
                gl.fillAll (juce::Colours::black);
                Palette vp;   // white on black; the colour comes afterwards
                if (motion) gl.addTransform (xf);
                v.draw (gl, size.toFloat(), f, vp, kk);
            };
            if (mixing)
            {
                if (! layer2.isValid() || layer2.getBounds() != layer.getBounds())
                    layer2 = juce::Image (juce::Image::ARGB, layer.getWidth(), layer.getHeight(), true);
                // the two styles draw at the same time on two cores
                if (workers != nullptr) workers->both ([&] { drawInto (layer2, *next); }, [&] { drawInto (layer, viz); });
                else { drawInto (layer, viz); drawInto (layer2, *next); }
            }
            else drawInto (layer, viz);
        }
        extractLum (mixing ? mix : 0.0f);
        if (react > 0 || fixedMirror) postFx (f, react, look);
        colourise (f, look, react);
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

void Renderer::reset()
{
    std::fill (prevLum.begin(), prevLum.end(), (juce::uint8) 0);
    sway = slide = offset = sectionShift = flowPhase = 0; zoomS = 1; lastSection = 0; mirrorMode = 0;
}

static float zoneAvg (const Frame& f, int a, int z, bool hits)
{
    float s = 0;
    for (int i = a; i <= z; ++i) s += hits ? f.zoneHit[(size_t) i] : f.zones[(size_t) i];
    return s / (float) (z - a + 1);
}
static float zoneMax (const Frame& f, int a, int z) { float s = 0; for (int i = a; i <= z; ++i) s = juce::jmax (s, f.zoneHit[(size_t) i]); return s; }

// Ranges used below (of 32): 0-2 sub, 3-6 bass, 7-11 low mids, 12-17 mids, 18-21 upper mids, 22-25 presence, 26-28 highs, 29-31 air
juce::AffineTransform Renderer::reactTransform (const Frame& f, juce::Rectangle<float> r, float react)
{
    const float dt = (float) f.dt;
    const auto c = r.getCentre();
    // sub breathes the picture, bass hits punch it, rising pitch pulls in, falling pitch pushes out, drops slam
    const float target = 1 + react * (0.07f * zoneAvg (f, 0, 2, false) + 0.06f * zoneMax (f, 3, 6)
                                      + 0.10f * juce::jmax (0.0f, f.pitchMove) - 0.07f * juce::jmax (0.0f, -f.pitchMove) + 0.08f * f.drop);
    zoomS += (target - zoomS) * juce::jmin (1.0f, dt * (6 + 14 * f.attack));    // sharp sounds snap, swells glide
    // low mids and drones sway it round; mid hits flick it
    sway += dt * (0.4f + 1.6f * zoneAvg (f, 7, 11, false) + 1.2f * f.drone);
    const float rot = react * (0.10f * std::sin (sway) * (0.3f + zoneAvg (f, 7, 11, false) + f.drone) + 0.035f * zoneMax (f, 12, 17) * (f.section % 2 ? 1 : -1));
    // panning slides and leans it; bass hits shake it
    slide += (f.pan - slide) * juce::jmin (1.0f, dt * 4);
    const float shake = react * zoneMax (f, 3, 6) * (0.5f + f.attack) * 0.012f * r.getHeight();
    const float sx = (rnd.nextFloat() - 0.5f) * shake, sy = (rnd.nextFloat() - 0.5f) * shake;
    const float z = zoomS * (1 + std::abs (rot) * 0.6f);                      // never show the corners
    return juce::AffineTransform::translation (-c.x, -c.y)
        .scaled (z)
        .sheared (react * slide * 0.08f, 0)
        .rotated (rot)
        .translated (c.x + react * slide * 0.05f * r.getWidth() + sx, c.y + sy);
}

void Renderer::postFx (const Frame& f, float react, const Look& look)
{
    // Everything here works on a one-byte-per-pixel brightness buffer (the layer is white on black at this point),
    // so it's a handful of cheap passes instead of full image draws. The result stays in lum for colourise().
    const int w = layer.getWidth(), h = layer.getHeight();
    const size_t N = (size_t) w * (size_t) h;
    const float t = (float) f.t;
    if (lum.size() != N) lum.assign (N, 0);
    if (prevLum.size() != N) prevLum.assign (N, 0);
    if (cpy.size() != N) cpy.assign (N, 0);

    // 1. brightness out of the layer: already in lum (extractLum)

    // 2. mirror modes: a new section flips to the next one, mid hits flicker a kaleidoscope
    if (f.section != lastSection) { lastSection = f.section; mirrorMode = (mirrorMode + 1 + rnd.nextInt (3)) % 5; }
    int mode = react >= 1 ? mirrorMode : 0;
    if (zoneMax (f, 12, 17) > 0.75f && react >= 0.5f) mode = mode == 0 ? (f.section % 2 ? 1 : 3) : mode;
    if (look.mirror != mirrorAuto) mode = look.mirror == mirrorOff ? 0 : look.mirror - 1;   // your pick: 1 L-R, 2 T-B, 3 four-way, 4 flipped
    if (mode == 1 || mode == 3)
        forRows (workers, h, [&] (int y0, int y1) { for (int y = y0; y < y1; ++y) { juce::uint8* row = lum.data() + (size_t) y * (size_t) w; for (int x = 0; x < w / 2; ++x) row[w - 1 - x] = row[x]; } });
    if (mode == 2 || mode == 3)
        forRows (workers, h / 2, [&] (int y0, int y1) { for (int y = y0; y < y1; ++y) std::memcpy (lum.data() + (size_t) (h - 1 - y) * (size_t) w, lum.data() + (size_t) y * (size_t) w, (size_t) w); });
    if (mode == 4)
        forRows (workers, h, [&] (int y0, int y1) { for (int y = y0; y < y1; ++y) { juce::uint8* row = lum.data() + (size_t) y * (size_t) w; std::reverse (row, row + w); } });

    // 3. sparkles on the highs: soft dots stamped in
    const float highs = zoneAvg (f, 26, 28, true);
    const int sparks = look.effectOn (fxSparkles) ? (int) (react * highs * 70) : 0;
    for (int i = 0; i < sparks; ++i)
    {
        const int cx = rnd.nextInt (w), cy = rnd.nextInt (h), rad = juce::jmax (1, (int) ((1.5f + rnd.nextFloat() * 2.5f) * h / 1080.0f * 2.5f));
        for (int dy = -rad; dy <= rad; ++dy)
            for (int dx = -rad; dx <= rad; ++dx)
            {
                const int x = cx + dx, y = cy + dy;
                if (x < 0 || y < 0 || x >= w || y >= h) continue;
                const float d = std::sqrt ((float) (dx * dx + dy * dy)) / rad;
                if (d > 1) continue;
                juce::uint8& v = lum[(size_t) y * (size_t) w + (size_t) x];
                v = (juce::uint8) juce::jmax ((int) v, (int) (d < 0.25f ? 255 : 40 * (1 - d)));
            }
    }

    // 4. one pass for the rest: glitch slices (presence), shimmer (air), echoes (busy), ghosts (wide),
    //    trails / reverb / dissolve (tails, gaps, silence), static (noise), flash (drops)
    std::memcpy (cpy.data(), lum.data(), N);
    const float presence = zoneMax (f, 22, 25), air = zoneAvg (f, 29, 31, false);
    shift.assign ((size_t) h, 0);
    if (presence > 0.5f && look.effectOn (fxGlitch))
    {
        int slices = 1 + (int) (presence * 4);
        for (int s = 0; s < slices; ++s)
        {
            int y0 = rnd.nextInt (h), hh = 2 + rnd.nextInt (juce::jmax (3, h / 12));
            int dx = (int) ((rnd.nextFloat() - 0.5f) * 0.1f * w * presence * react);
            for (int y = y0; y < juce::jmin (h, y0 + hh); ++y) shift[(size_t) y] += dx;
        }
    }
    if (air > 0.25f && look.effectOn (fxShimmer)) for (int y = 0; y < h; ++y) shift[(size_t) y] += (int) std::lround (react * (air - 0.25f) * 4 * h / 1080.0f * std::sin (y * 0.15f + t * 30));

    const int echoA = f.density > 0.15f && look.effectOn (fxEchoes) ? (int) (react * f.density * 0.35f * 256) : 0;
    const int ghostA = f.width > 0.1f && look.effectOn (fxGhosts) ? (int) (react * f.width * 0.4f * 256) : 0;
    const int ghostOff = (int) (f.width * 0.04f * w * react);
    if (echoA > 0)
    {
        // where each pixel of the 105% and 95% copies comes from
        const float cxf = w * 0.5f, cyf = h * 0.5f;
        ex1.resize ((size_t) w); ex2.resize ((size_t) w); ey1.resize ((size_t) h); ey2.resize ((size_t) h);
        for (int x = 0; x < w; ++x) { ex1[(size_t) x] = juce::jlimit (0, w - 1, (int) (cxf + (x - cxf) / 1.05f)); ex2[(size_t) x] = (int) (cxf + (x - cxf) / 0.95f); }
        for (int y = 0; y < h; ++y) { ey1[(size_t) y] = juce::jlimit (0, h - 1, (int) (cyf + (y - cyf) / 1.05f)); ey2[(size_t) y] = (int) (cyf + (y - cyf) / 0.95f); }
    }
    const int decay = look.effectOn (fxTrails) ? (int) (juce::jlimit (0.0f, 0.93f, react * (0.55f * f.tail + 0.3f * f.sparse + 0.5f * f.silence)) * 256) : 0;
    const juce::uint32 dissolve = (juce::uint32) (f.silence * react * 90);
    const int drift = f.silence > 0.2f ? 1 : 0;
    const int noiseAmp = look.effectOn (fxStatic) ? (int) (react * f.noisiness * 70) : 0;
    const int flash = look.effectOn (fxFlash) ? (int) (react * f.drop * f.drop * 90) : 0;
    const juce::uint32 frameSeed = (juce::uint32) (f.index * 2654435761u + 1);
    forRows (workers, h, [&] (int y0, int y1) {
    for (int y = y0; y < y1; ++y)
    {
        // each row gets its own noise seed, so the rows can run on different cores
        juce::uint32 seed = frameSeed ^ ((juce::uint32) y * 2246822519u);
        seed = seed * 1664525u + 1013904223u;
        const int sh = shift[(size_t) y];
        const juce::uint8* src = cpy.data() + (size_t) y * (size_t) w;
        const juce::uint8* prv = prevLum.data() + (size_t) juce::jmax (0, y - drift) * (size_t) w;
        const juce::uint8* e1 = echoA > 0 ? cpy.data() + (size_t) ey1[(size_t) y] * (size_t) w : nullptr;
        const int y2 = echoA > 0 ? ey2[(size_t) y] : -1;
        const juce::uint8* e2 = (y2 >= 0 && y2 < h) ? cpy.data() + (size_t) y2 * (size_t) w : nullptr;
        juce::uint8* out = lum.data() + (size_t) y * (size_t) w;
        for (int x = 0; x < w; ++x)
        {
            const int sx = x - sh;
            int v = (sx >= 0 && sx < w) ? src[sx] : 0;
            if (echoA > 0)
            {
                const int a1 = e1[ex1[(size_t) x]];
                const int x2 = ex2[(size_t) x];
                const int a2 = (e2 != nullptr && x2 >= 0 && x2 < w) ? e2[x2] : 0;
                v += ((a1 + a2) * echoA) >> 9;
            }
            if (ghostA > 0)
            {
                const int l = x - ghostOff >= 0 ? src[x - ghostOff] : 0, r = x + ghostOff < w ? src[x + ghostOff] : 0;
                v += ((l + r) * ghostA) >> 9;
            }
            if (decay > 0)
            {
                seed = seed * 1664525u + 1013904223u;
                const int p = (dissolve > 0 && ((seed >> 24) & 255) < dissolve) ? 0 : (prv[x] * decay) >> 8;
                if (p > v) v = p;
            }
            if (noiseAmp > 0) { seed = seed * 1664525u + 1013904223u; v += (int) ((seed >> 25) % (juce::uint32) (noiseAmp + 1)) - noiseAmp / 3; }
            v += flash;
            out[x] = (juce::uint8) (v < 0 ? 0 : v > 255 ? 255 : v);
        }
    }
    });
    std::memcpy (prevLum.data(), lum.data(), N);   // for next frame's trails
    lumReady = true;
}

const std::vector<float>& Renderer::field (int kind)
{
    const int w = layer.getWidth(), h = layer.getHeight();
    if (fieldSize != layer.getBounds()) { fields.clear(); fieldSize = layer.getBounds(); }
    auto it = fields.find (kind);
    if (it != fields.end()) return it->second;
    std::vector<float> v ((size_t) w * (size_t) h, 0.0f);
    const float cx = w * 0.5f, cy = h * 0.5f, norm = 1.0f / (float) juce::jmax (1, h);
    const float tau = juce::MathConstants<float>::twoPi;
    forRows (workers, h, [&] (int y0, int y1) {
    for (int y = y0; y < y1; ++y)
        for (int x = 0; x < w; ++x)
        {
            float dx = x - cx, dy = y - cy, d = std::sqrt (dx * dx + dy * dy) * norm, a = std::atan2 (dy, dx) / tau;
            float val = kind == 0 ? a + d * 0.9f : kind == 1 ? d * 1.4f : kind == 2 ? y * norm : kind == 3 ? (x + y) * norm * 0.6f : a;
            v[(size_t) y * (size_t) w + (size_t) x] = val;
        }
    });
    return fields.emplace (kind, std::move (v)).first->second;
}

void Renderer::colourise (const Frame& f, const Look& look, float react)
{
    const int w = layer.getWidth(), h = layer.getHeight();
    const bool paper = look.background == 2;
    constexpr int kLut = 1024;
    // one palette, or (random colours, mid-fade) two that blend
    struct Lut { juce::uint8 r[kLut], g[kLut], b[kLut]; int kind = 0; float spatial = 1, bright = 0; };
    Lut A, B;
    const bool rolled = look.rollA != 0;
    const bool twoLuts = rolled && look.rollB != 0 && look.rollB != look.rollA && look.rollMix > 0.002f;
    auto fromRoll = [&] (Lut& L, juce::uint32 seed)
    {
        const auto rp = rollPalette (seed);
        lutFromStops (rp.stops, paper, L.r, L.g, L.b, kLut);
        L.kind = rp.field; L.spatial = rp.spatial; L.bright = rp.bright;
    };
    if (rolled)
    {
        fromRoll (A, look.rollA);
        if (twoLuts) fromRoll (B, look.rollB);
    }
    else if (look.tint < kMultiColour)
    {
        auto c = paletteFor (look).fg;
        for (int i = 0; i < kLut; ++i) { A.r[i] = c.getRed(); A.g[i] = c.getGreen(); A.b[i] = c.getBlue(); }
        A.spatial = 0;
    }
    else
    {
        buildLut (look.tint, paper, A.r, A.g, A.b, kLut);
        if (look.tint >= kFirstMix)
        {
            const auto& m = mixes()[(size_t) juce::jlimit (0, (int) mixes().size() - 1, look.tint - kFirstMix)];
            A.kind = m.field; A.spatial = m.spatial; A.bright = m.bright;
        }
    }
    // the palette turns slowly, kicks on beats, slides with brightness, and jumps a third when the section changes;
    // Colour flow adds a steady run through it on top (faster when it's loud)
    const float target = (float) f.section * 0.33f;
    sectionShift += (target - sectionShift) * juce::jmin (1.0f, (float) f.dt * 6);
    static const float flowRates[] = { 0.0f, 0.22f, 0.7f };
    flowPhase = (float) std::fmod (flowPhase + (float) f.dt * flowRates[juce::jlimit (0, 2, look.flow)] * (0.6f + 0.8f * f.energy), 1000.0);
    offset = (float) std::fmod (f.t * (0.04 + 0.05 * f.energy) + f.beat * 0.07 + f.brightness * 0.3f * (react > 0 ? 1 : 0) + sectionShift * (react > 0 ? 1 : 0) + flowPhase, 1000.0);
    if (! look.effectOn (fxColourMotion)) offset = 0;   // the palette holds still
    const std::vector<float>* fv = A.spatial > 0 ? &field (A.kind) : nullptr;
    const std::vector<float>* fv2 = twoLuts && B.spatial > 0 ? &field (B.kind) : nullptr;
    const int mixB = twoLuts ? juce::jlimit (0, 256, (int) std::lround (look.rollMix * 256.0f)) : 0, mixA = 256 - mixB;

    juce::Image::BitmapData bd (layer, juce::Image::BitmapData::readWrite);
    if (! lumReady) extractLum (0.0f);
    lumReady = false;
    // crunch splits the colour channels apart
    const int split = (int) std::lround (react * juce::jmax (0.0f, f.crunch - 0.3f) / 0.7f * 0.01f * w);
    forRows (workers, h, [&] (int y0, int y1) {
    std::vector<juce::uint8> cr ((size_t) w), cg ((size_t) w), cb ((size_t) w), ca ((size_t) w);
    for (int y = y0; y < y1; ++y)
    {
        const juce::uint8* L = lum.data() + (size_t) y * (size_t) w;
        const float* F = fv != nullptr ? fv->data() + (size_t) y * (size_t) w : nullptr;
        const float* F2 = fv2 != nullptr ? fv2->data() + (size_t) y * (size_t) w : nullptr;
        for (int x = 0; x < w; ++x)
        {
            const juce::uint32 v = L[x];
            float idx = offset + (F != nullptr ? F[x] * A.spatial : 0.0f) + A.bright * (float) v / 255.0f;
            const int i = (int) ((idx - std::floor (idx)) * kLut) & (kLut - 1);
            juce::uint32 pr = A.r[i], pg = A.g[i], pb = A.b[i];
            if (mixB > 0)
            {
                float idx2 = offset + (F2 != nullptr ? F2[x] * B.spatial : 0.0f) + B.bright * (float) v / 255.0f;
                const int j = (int) ((idx2 - std::floor (idx2)) * kLut) & (kLut - 1);
                pr = (pr * (juce::uint32) mixA + B.r[j] * (juce::uint32) mixB) >> 8;
                pg = (pg * (juce::uint32) mixA + B.g[j] * (juce::uint32) mixB) >> 8;
                pb = (pb * (juce::uint32) mixA + B.b[j] * (juce::uint32) mixB) >> 8;
            }
            const juce::uint32 m = v * 257u;   // x * v / 255 without the divisions
            cr[(size_t) x] = (juce::uint8) ((pr * m + 32768u) >> 16); cg[(size_t) x] = (juce::uint8) ((pg * m + 32768u) >> 16); cb[(size_t) x] = (juce::uint8) ((pb * m + 32768u) >> 16);
            ca[(size_t) x] = (juce::uint8) v;
        }
        auto* line = bd.getLinePointer (y);
        for (int x = 0; x < w; ++x)
        {
            const int xr = juce::jlimit (0, w - 1, x - split), xb = juce::jlimit (0, w - 1, x + split);
            juce::uint8 r = cr[(size_t) xr], g = cg[(size_t) x], b = cb[(size_t) xb];
            juce::uint8 a = juce::jmax (ca[(size_t) xr], ca[(size_t) x], ca[(size_t) xb]);
            r = juce::jmin (r, a); g = juce::jmin (g, a); b = juce::jmin (b, a);
            reinterpret_cast<juce::PixelARGB*> (line + (size_t) x * (size_t) bd.pixelStride)->setARGB (a, r, g, b);
        }
    }
    });
}

void Renderer::extractLum (float mix)
{
    const int w = layer.getWidth(), h = layer.getHeight();
    const size_t N = (size_t) w * (size_t) h;
    if (lum.size() != N) lum.assign (N, 0);
    const bool two = mix > 0 && layer2.isValid() && layer2.getBounds() == layer.getBounds();
    const int mb = juce::jlimit (0, 256, (int) std::lround (mix * 256.0f)), ma = 256 - mb;
    juce::Image::BitmapData bd (layer, juce::Image::BitmapData::readOnly);
    std::unique_ptr<juce::Image::BitmapData> bd2;
    if (two) bd2 = std::make_unique<juce::Image::BitmapData> (layer2, juce::Image::BitmapData::readOnly);
    forRows (workers, h, [&] (int y0, int y1)
    {
        for (int y = y0; y < y1; ++y)
        {
            const auto* line = bd.getLinePointer (y);
            const auto* line2 = two ? bd2->getLinePointer (y) : nullptr;
            juce::uint8* out = lum.data() + (size_t) y * (size_t) w;
            for (int x = 0; x < w; ++x)
            {
                const auto* px = reinterpret_cast<const juce::PixelARGB*> (line + (size_t) x * (size_t) bd.pixelStride);
                int v = juce::jmax (px->getRed(), px->getGreen(), px->getBlue());
                if (two)
                {
                    const auto* q = reinterpret_cast<const juce::PixelARGB*> (line2 + (size_t) x * (size_t) bd2->pixelStride);
                    v = (v * ma + juce::jmax (q->getRed(), q->getGreen(), q->getBlue()) * mb) >> 8;
                }
                out[x] = (juce::uint8) v;
            }
        }
    });
    lumReady = true;
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
