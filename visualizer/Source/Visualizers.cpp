// The BC Visualizer styles. Each one is a small class: state it needs between frames, and draw().
#include "Visualizers.h"

namespace bcv
{
namespace
{
using juce::Graphics; using juce::Rectangle; using juce::Path; using juce::PathStrokeType; using juce::Point; using juce::Colour;
constexpr float kPi = juce::MathConstants<float>::pi;
constexpr float kTau = juce::MathConstants<float>::twoPi;

// ------------------------------------------------------------ helpers
inline float hash (int x, int y, int z = 0)
{
    uint32_t h = (uint32_t) x * 374761393u + (uint32_t) y * 668265263u + (uint32_t) z * 2147483647u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (float) ((h ^ (h >> 16)) & 0xffffff) / (float) 0xffffff;
}
inline float smoothstep (float t) { return t * t * (3 - 2 * t); }
inline float noise (float x, float y, float z = 0)
{
    int xi = (int) std::floor (x), yi = (int) std::floor (y), zi = (int) std::floor (z);
    float xf = x - xi, yf = y - yi, zf = z - zi;
    float u = smoothstep (xf), v = smoothstep (yf), w = smoothstep (zf);
    auto L = [] (float a, float b, float t) { return a + (b - a) * t; };
    float a = L (L (hash (xi, yi, zi), hash (xi + 1, yi, zi), u), L (hash (xi, yi + 1, zi), hash (xi + 1, yi + 1, zi), u), v);
    float b = L (L (hash (xi, yi, zi + 1), hash (xi + 1, yi, zi + 1), u), L (hash (xi, yi + 1, zi + 1), hash (xi + 1, yi + 1, zi + 1), u), v);
    return L (a, b, w);
}
inline float band (const Frame& f, float pos) { return f.smooth[(size_t) juce::jlimit (0, kBands - 1, (int) (pos * (kBands - 1)))]; }

struct P3 { float x, y, z; };
inline P3 rotY (P3 p, float a) { float c = std::cos (a), s = std::sin (a); return { p.x * c + p.z * s, p.y, -p.x * s + p.z * c }; }
inline P3 rotX (P3 p, float a) { float c = std::cos (a), s = std::sin (a); return { p.x, p.y * c - p.z * s, p.y * s + p.z * c }; }
inline P3 rotZ (P3 p, float a) { float c = std::cos (a), s = std::sin (a); return { p.x * c - p.y * s, p.x * s + p.y * c, p.z }; }
/** perspective: camera at z = -dist looking +z; returns screen point and depth scale */
inline Point<float> project (P3 p, Rectangle<float> r, float dist, float& scale)
{
    float z = p.z + dist;
    scale = z > 0.05f ? dist / z : 0;
    float s = r.getHeight() * 0.5f;
    return { r.getCentreX() + p.x * scale * s, r.getCentreY() - p.y * scale * s };
}

/** A persistent canvas for styles that leave trails (phosphor, smoke). */
struct Trails
{
    juce::Image img;
    void prepare (Rectangle<float> r, const Palette& p, float fade)
    {
        int w = juce::jmax (1, (int) r.getWidth()), h = juce::jmax (1, (int) r.getHeight());
        if (img.getWidth() != w || img.getHeight() != h) { img = juce::Image (juce::Image::ARGB, w, h, true); Graphics gi (img); gi.fillAll (p.bg); }
        Graphics gi (img);
        gi.setColour (p.bg.withAlpha (fade));
        gi.fillAll();
    }
    void blit (Graphics& g, Rectangle<float> r) { g.drawImageAt (img, (int) r.getX(), (int) r.getY()); }
    void clear() { img = {}; }
};

// ============================================================ 1-10

struct Ridges : Viz   // Unknown Pleasures
{
    std::deque<std::array<float, kBands>> rows;
    juce::String name() const override { return "Ridge Lines"; }
    juce::String blurb() const override { return "Stacked spectrum ridges in perspective, newest at the front."; }
    void reset() override { rows.clear(); }
    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        rows.push_front (f.bands);
        const int depth = 56;
        while ((int) rows.size() > depth) rows.pop_back();
        const float w = r.getWidth() * 0.62f, x0 = r.getCentreX() - w / 2;
        for (int i = (int) rows.size() - 1; i >= 0; --i)
        {
            float d = (float) i / depth;
            float base = r.getY() + r.getHeight() * (0.80f - d * 0.60f);
            float amp = r.getHeight() * 0.22f * (1 - d * 0.55f) * k;
            Path line;
            for (int b = 0; b < kBands; ++b)
            {
                float u = (float) b / (kBands - 1);
                float env = std::pow (std::sin (u * kPi), 2.0f);   // calm edges, busy middle like the record sleeve
                float y = base - rows[(size_t) i][(size_t) b] * amp * env;
                float x = x0 + u * w;
                if (b == 0) line.startNewSubPath (x, y); else line.lineTo (x, y);
            }
            Path fill (line); fill.lineTo (x0 + w, base + 2); fill.lineTo (x0, base + 2); fill.closeSubPath();
            g.setColour (p.bg); g.fillPath (fill);
            g.setColour (p.fgA (1.0f - d * 0.75f)); g.strokePath (line, PathStrokeType (px (r, i == 0 ? 2.4f : 1.6f)));
        }
    }
};

struct Scope : Viz
{
    juce::String name() const override { return "Oscilloscope"; }
    juce::String blurb() const override { return "A clean triggered trace with a soft glow."; }
    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        int start = 0;   // trigger on a rising zero crossing in the first half
        for (int i = 1; i < kWave / 2; ++i) if (f.L[(size_t) i - 1] < 0 && f.L[(size_t) i] >= 0) { start = i; break; }
        Path tr;
        const int n = kWave - kWave / 2;
        for (int i = 0; i < n; ++i)
        {
            float x = r.getX() + r.getWidth() * (0.08f + 0.84f * i / (n - 1));
            float y = r.getCentreY() - juce::jlimit (-1.2f, 1.2f, f.L[(size_t) (start + i)] * 1.6f) * r.getHeight() * 0.3f * k;
            if (i == 0) tr.startNewSubPath (x, y); else tr.lineTo (x, y);
        }
        for (int glow = 4; glow >= 1; --glow) { g.setColour (p.fgA (0.06f * (5 - glow))); g.strokePath (tr, PathStrokeType (px (r, 2.0f + glow * 5.0f), PathStrokeType::curved, PathStrokeType::rounded)); }
        g.setColour (p.fg); g.strokePath (tr, PathStrokeType (px (r, 2.2f), PathStrokeType::curved, PathStrokeType::rounded));
    }
};

struct Lissajous : Viz
{
    Trails tr;
    juce::String name() const override { return "Phosphor X/Y"; }
    juce::String blurb() const override { return "Left against right on a slow-fading phosphor screen."; }
    void reset() override { tr.clear(); }
    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        tr.prepare (r, p, 0.18f);
        Graphics gi (tr.img);
        auto lr = r.withPosition (0, 0);
        float s = lr.getHeight() * 0.42f * k;
        Path pth;
        for (int i = 0; i < kWave; i += 2)
        {
            float m = (f.L[(size_t) i] + f.R[(size_t) i]) * 0.7071f, sd = (f.L[(size_t) i] - f.R[(size_t) i]) * 0.7071f;
            float x = lr.getCentreX() + sd * s, y = lr.getCentreY() - m * s;
            if (i == 0) pth.startNewSubPath (x, y); else pth.lineTo (x, y);
        }
        gi.setColour (p.fgA (0.85f)); gi.strokePath (pth, PathStrokeType (px (r, 1.6f)));
        tr.blit (g, r);
        g.setColour (p.fgA (0.12f));
        g.drawLine (r.getCentreX(), r.getY() + r.getHeight() * 0.1f, r.getCentreX(), r.getBottom() - r.getHeight() * 0.1f, px (r, 1));
        g.drawLine (r.getCentreX() - r.getHeight() * 0.4f, r.getCentreY(), r.getCentreX() + r.getHeight() * 0.4f, r.getCentreY(), px (r, 1));
    }
};

struct Bars3D : Viz
{
    juce::String name() const override { return "3D Bars"; }
    juce::String blurb() const override { return "Extruded spectrum bars lit from above."; }
    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        const int n = 48;
        float area = r.getWidth() * 0.8f, bw = area / n, x0 = r.getCentreX() - area / 2, base = r.getY() + r.getHeight() * 0.78f;
        float depth = bw * 0.45f;
        for (int i = 0; i < n; ++i)
        {
            float v = juce::jlimit (0.0f, 1.0f, band (f, (float) i / n) * k);
            float h = v * r.getHeight() * 0.55f + px (r, 4);
            auto front = Rectangle<float> (x0 + i * bw + bw * 0.12f, base - h, bw * 0.7f, h);
            Path side; side.addQuadrilateral (front.getRight(), front.getY(), front.getRight() + depth, front.getY() - depth, front.getRight() + depth, front.getBottom() - depth, front.getRight(), front.getBottom());
            Path top; top.addQuadrilateral (front.getX(), front.getY(), front.getX() + depth, front.getY() - depth, front.getRight() + depth, front.getY() - depth, front.getRight(), front.getY());
            g.setColour (p.ink (0.35f)); g.fillPath (side);
            g.setColour (p.ink (0.85f)); g.fillPath (top);
            g.setGradientFill (juce::ColourGradient (p.ink (1.0f), front.getX(), front.getY(), p.ink (0.45f), front.getX(), front.getBottom(), false));
            g.fillRect (front);
            // reflection
            g.setGradientFill (juce::ColourGradient (p.fgA (0.18f), front.getX(), base, p.fgA (0.0f), front.getX(), base + h * 0.5f, false));
            g.fillRect (front.withY (base + px (r, 3)).withHeight (h * 0.5f));
        }
    }
};

struct Radial : Viz
{
    float spin = 0;
    juce::String name() const override { return "Radial Spectrum"; }
    juce::String blurb() const override { return "Spectrum bars around a circle that breathes with the bass."; }
    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        spin += (float) f.dt * (0.1f + f.energy * 0.6f);
        auto c = r.getCentre();
        float r0 = r.getHeight() * (0.2f + f.bass * 0.06f * k + f.beat * 0.02f);
        const int n = 120;
        for (int i = 0; i < n; ++i)
        {
            float u = (float) i / n, a = u * kTau + spin;
            float mirror = u < 0.5f ? u * 2 : (1 - u) * 2;
            float len = r.getHeight() * 0.24f * band (f, mirror) * k + px (r, 3);
            g.setColour (p.fgA (0.4f + band (f, mirror) * 0.6f));
            g.drawLine (c.x + std::cos (a) * r0, c.y + std::sin (a) * r0, c.x + std::cos (a) * (r0 + len), c.y + std::sin (a) * (r0 + len), px (r, 4));
        }
        g.setColour (p.fg); g.drawEllipse (Rectangle<float> (r0 * 2, r0 * 2).withCentre (c), px (r, 2.5f));
    }
};

struct PulseRings : Viz
{
    struct Ring { float r, a; };
    std::vector<Ring> rings;
    juce::String name() const override { return "Pulse Rings"; }
    juce::String blurb() const override { return "Every beat throws a ring outward."; }
    void reset() override { rings.clear(); }
    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        if (f.onset) rings.push_back ({ 0.02f, 1.0f });
        for (auto& x : rings) { x.r += (float) f.dt * (0.35f + f.energy * 0.6f) * k; x.a *= (float) std::exp (-f.dt * 1.2); }
        rings.erase (std::remove_if (rings.begin(), rings.end(), [] (const Ring& x) { return x.a < 0.02f || x.r > 1.2f; }), rings.end());
        auto c = r.getCentre(); float s = r.getHeight() * 0.5f;
        for (auto& x : rings) { g.setColour (p.fgA (x.a)); g.drawEllipse (Rectangle<float> (x.r * s * 2, x.r * s * 2).withCentre (c), px (r, 3 + x.a * 6)); }
        float core = s * (0.06f + f.rms * 0.18f * k);
        g.setColour (p.fg); g.fillEllipse (Rectangle<float> (core * 2, core * 2).withCentre (c));
    }
};

struct Starfield : Viz
{
    struct Star { float x, y, z; };
    std::vector<Star> stars;
    juce::Random rnd { 7 };
    juce::String name() const override { return "Warp Field"; }
    juce::String blurb() const override { return "Stars streak past faster as the track gets louder."; }
    void reset() override { stars.clear(); rnd.setSeed (7); }
    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        if (stars.empty()) for (int i = 0; i < 700; ++i) stars.push_back ({ rnd.nextFloat() * 2 - 1, rnd.nextFloat() * 2 - 1, rnd.nextFloat() });
        float speed = (float) f.dt * (0.15f + f.energy * 1.2f * k + f.beat * 0.8f);
        auto c = r.getCentre(); float s = r.getWidth() * 0.5f;
        for (auto& st : stars)
        {
            float z0 = st.z;
            st.z -= speed;
            if (st.z <= 0.02f) { st = { rnd.nextFloat() * 2 - 1, rnd.nextFloat() * 2 - 1, 1.0f }; continue; }
            Point<float> a (c.x + st.x / z0 * s * 0.3f, c.y + st.y / z0 * s * 0.3f), b (c.x + st.x / st.z * s * 0.3f, c.y + st.y / st.z * s * 0.3f);
            g.setColour (p.fgA (1.0f - st.z));
            g.drawLine ({ a, b }, px (r, 1.0f + (1 - st.z) * 2.5f));
        }
    }
};

struct Burst : Viz
{
    struct Pt { Point<float> pos, vel; float life; };
    std::vector<Pt> pts;
    juce::Random rnd { 3 };
    juce::String name() const override { return "Particle Burst"; }
    juce::String blurb() const override { return "Beats explode into particles that fall away."; }
    void reset() override { pts.clear(); rnd.setSeed (3); }
    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        auto c = r.getCentre();
        int spawn = f.onset ? (int) (160 * k) : (int) (f.rms * 6 * k);
        for (int i = 0; i < spawn; ++i)
        {
            float a = rnd.nextFloat() * kTau, sp = (0.2f + rnd.nextFloat() * (f.onset ? 0.9f : 0.3f)) * r.getHeight();
            pts.push_back ({ c, { std::cos (a) * sp, std::sin (a) * sp }, 1.0f });
        }
        float dt = (float) f.dt;
        for (auto& q : pts) { q.vel.y += r.getHeight() * 0.5f * dt; q.vel *= 0.985f; q.pos += q.vel * dt; q.life -= dt * 0.6f; }
        pts.erase (std::remove_if (pts.begin(), pts.end(), [&] (const Pt& q) { return q.life <= 0 || ! r.expanded (50).contains (q.pos); }), pts.end());
        for (auto& q : pts) { float sz = px (r, 2 + q.life * 4); g.setColour (p.fgA (q.life)); g.fillEllipse (q.pos.x - sz / 2, q.pos.y - sz / 2, sz, sz); }
    }
};

struct Tunnel : Viz
{
    float z = 0, rot = 0;
    juce::String name() const override { return "Tunnel"; }
    juce::String blurb() const override { return "Square frames rush toward you and twist on the beat."; }
    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        z += (float) f.dt * (0.4f + f.energy * 2.0f * k);
        rot += (float) f.dt * (0.1f + f.beat * 1.5f);
        auto c = r.getCentre();
        const int n = 22;
        for (int i = n; i >= 1; --i)
        {
            float d = (float) i - std::fmod (z, 1.0f);
            float s = r.getHeight() * 1.6f / d;
            float a = rot + d * 0.12f;
            Path sq; sq.addRectangle (-s / 2, -s / 2, s, s);
            sq.applyTransform (juce::AffineTransform::rotation (a).translated (c));
            g.setColour (p.fgA (juce::jlimit (0.0f, 1.0f, 1.2f - d / n) * (0.5f + band (f, (float) i / n))));
            g.strokePath (sq, PathStrokeType (px (r, 2.0f + 6.0f / d)));
        }
    }
};

struct Terrain : Viz
{
    float scroll = 0;
    std::deque<std::array<float, 40>> rows;
    juce::String name() const override { return "Terrain"; }
    juce::String blurb() const override { return "A wireframe landscape scrolls toward you, raised by the spectrum."; }
    void reset() override { rows.clear(); }
    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        scroll += (float) f.dt * (2 + f.energy * 6);
        std::array<float, 40> row {};
        for (int i = 0; i < 40; ++i) { float u = std::abs (i - 19.5f) / 19.5f; row[(size_t) i] = band (f, u * 0.9f) * (0.3f + u) * k + noise (i * 0.4f, scroll * 0.3f) * 0.15f; }
        rows.push_front (row);
        while (rows.size() > 30) rows.pop_back();
        std::vector<std::vector<Point<float>>> pts;
        for (size_t j = 0; j < rows.size(); ++j)
        {
            std::vector<Point<float>> line;
            for (int i = 0; i < 40; ++i)
            {
                P3 q { (i - 19.5f) * 0.12f, -0.5f + rows[j][(size_t) i] * 0.9f, 0.3f + j * 0.18f };
                float sc; line.push_back (project (q, r, 1.4f, sc));
            }
            pts.push_back (line);
        }
        for (size_t j = 0; j < pts.size(); ++j)
        {
            float a = 1.0f - (float) j / pts.size();
            g.setColour (p.fgA (a));
            Path pl; pl.startNewSubPath (pts[j][0]); for (int i = 1; i < 40; ++i) pl.lineTo (pts[j][(size_t) i]);
            g.strokePath (pl, PathStrokeType (px (r, 1.4f)));
            if (j + 1 < pts.size()) for (int i = 0; i < 40; i += 2) g.drawLine ({ pts[j][(size_t) i], pts[j + 1][(size_t) i] }, px (r, 1.0f));
        }
    }
};

// ============================================================ 11-20

struct Sphere : Viz
{
    float a = 0;
    juce::String name() const override { return "Wire Sphere"; }
    juce::String blurb() const override { return "A rotating sphere of points pushed out by the spectrum."; }
    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        a += (float) f.dt * (0.25f + f.energy);
        const int lat = 24, lon = 48;
        for (int i = 1; i < lat; ++i)
            for (int j = 0; j < lon; ++j)
            {
                float th = kPi * i / lat, ph = kTau * j / lon;
                float push = 0.55f + band (f, (float) i / lat) * 0.35f * k + f.beat * 0.05f;
                P3 q { std::sin (th) * std::cos (ph) * push, std::cos (th) * push, std::sin (th) * std::sin (ph) * push };
                q = rotX (rotY (q, a), 0.35f);
                float sc; auto pt = project (q, r, 2.2f, sc);
                float sz = px (r, 1.5f + sc * 2.5f);
                g.setColour (p.fgA (juce::jlimit (0.15f, 1.0f, 0.55f - q.z * 0.8f)));
                g.fillEllipse (pt.x - sz / 2, pt.y - sz / 2, sz, sz);
            }
    }
};

struct Cube : Viz
{
    float a = 0, b = 0;
    juce::String name() const override { return "Cube Lattice"; }
    juce::String blurb() const override { return "Nested wireframe cubes spin and swell with the bass."; }
    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        a += (float) f.dt * (0.3f + f.mid); b += (float) f.dt * (0.2f + f.treble * 0.8f);
        const int edges[12][2] = { {0,1},{1,3},{3,2},{2,0},{4,5},{5,7},{7,6},{6,4},{0,4},{1,5},{2,6},{3,7} };
        for (int layer = 0; layer < 4; ++layer)
        {
            float s = (0.25f + layer * 0.12f) * (1 + f.bass * 0.4f * k + (layer == 0 ? f.beat * 0.2f : 0));
            Point<float> v[8];
            for (int i = 0; i < 8; ++i)
            {
                P3 q { (i & 1 ? s : -s), (i & 2 ? s : -s), (i & 4 ? s : -s) };
                q = rotX (rotY (q, a + layer * 0.3f), b - layer * 0.2f);
                float sc; v[i] = project (q, r, 2.5f, sc);
            }
            g.setColour (p.fgA (1.0f - layer * 0.2f));
            for (auto& e : edges) g.drawLine ({ v[e[0]], v[e[1]] }, px (r, 2.5f - layer * 0.4f));
        }
    }
};

struct PolarWave : Viz
{
    juce::String name() const override { return "Polar Wave"; }
    juce::String blurb() const override { return "The waveform wrapped into a ring."; }
    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        auto c = r.getCentre();
        for (int pass = 0; pass < 2; ++pass)
        {
            Path pth;
            const auto& w = pass == 0 ? f.L : f.R;
            for (int i = 0; i <= 360; ++i)
            {
                int idx = (i % 360) * (kWave - 1) / 360;
                float rad = r.getHeight() * (0.26f + w[(size_t) idx] * 0.4f * k + f.bass * 0.06f);
                float a = kTau * i / 360 - kPi / 2;
                Point<float> q (c.x + std::cos (a) * rad, c.y + std::sin (a) * rad);
                if (i == 0) pth.startNewSubPath (q); else pth.lineTo (q);
            }
            pth.closeSubPath();
            g.setColour (p.fgA (pass == 0 ? 1.0f : 0.45f));
            g.strokePath (pth, PathStrokeType (px (r, pass == 0 ? 2.5f : 1.5f)));
        }
    }
};

struct Spectrogram : Viz
{
    juce::Image img;
    juce::String name() const override { return "Spectrogram"; }
    juce::String blurb() const override { return "Frequency over time, scrolling right to left."; }
    void reset() override { img = {}; }
    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        const int W = 320, H = kBands;
        if (! img.isValid()) { img = juce::Image (juce::Image::RGB, W, H, true); Graphics gi (img); gi.fillAll (p.bg); }
        img.moveImageSection (0, 0, 1, 0, W - 1, H);
        for (int b = 0; b < H; ++b) img.setPixelAt (W - 1, H - 1 - b, p.ink (std::pow (juce::jlimit (0.0f, 1.0f, f.bands[(size_t) b] * k), 1.6f)));
        auto area = r.reduced (r.getWidth() * 0.06f, r.getHeight() * 0.12f);
        g.setImageResamplingQuality (Graphics::mediumResamplingQuality);
        g.drawImage (img, area);
        g.setColour (p.fgA (0.3f)); g.drawRect (area, px (r, 1.5f));
    }
};

struct CRT : Viz
{
    Trails tr;
    juce::String name() const override { return "CRT Scope"; }
    juce::String blurb() const override { return "An old tube oscilloscope: persistence, bloom and scanlines."; }
    void reset() override { tr.clear(); }
    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        tr.prepare (r, p, 0.35f);
        {
            Graphics gi (tr.img);
            auto lr = r.withPosition (0, 0);
            Path pth;
            for (int i = 0; i < kWave; ++i)
            {
                float x = lr.getX() + lr.getWidth() * (0.1f + 0.8f * i / (kWave - 1));
                float y = lr.getCentreY() - f.L[(size_t) i] * lr.getHeight() * 0.35f * k;
                if (i == 0) pth.startNewSubPath (x, y); else pth.lineTo (x, y);
            }
            gi.setColour (p.fgA (0.25f)); gi.strokePath (pth, PathStrokeType (px (r, 9)));
            gi.setColour (p.fg); gi.strokePath (pth, PathStrokeType (px (r, 2)));
        }
        tr.blit (g, r);
        g.setColour (p.bg.withAlpha (0.35f));
        for (float y = r.getY(); y < r.getBottom(); y += px (r, 4)) g.fillRect (r.getX(), y, r.getWidth(), px (r, 1.5f));
        g.setColour (p.fgA (0.08f));
        for (int i = 1; i < 10; ++i) g.drawVerticalLine ((int) (r.getX() + r.getWidth() * i / 10), r.getY(), r.getBottom());
        for (int i = 1; i < 8; ++i) g.drawHorizontalLine ((int) (r.getY() + r.getHeight() * i / 8), r.getX(), r.getRight());
    }
};

struct MirrorBars : Viz
{
    juce::String name() const override { return "Mirror Bars"; }
    juce::String blurb() const override { return "Thin bars mirrored up and down from a centre line."; }
    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        const int n = 96;
        float bw = r.getWidth() * 0.9f / n, x0 = r.getX() + r.getWidth() * 0.05f;
        for (int i = 0; i < n; ++i)
        {
            float u = (float) i / n, sym = u < 0.5f ? 1 - u * 2 : (u - 0.5f) * 2;
            float h = band (f, sym) * r.getHeight() * 0.4f * k + px (r, 2);
            g.setColour (p.fgA (0.35f + sym * 0.65f));
            g.fillRect (x0 + i * bw + bw * 0.2f, r.getCentreY() - h, bw * 0.6f, h * 2);
        }
    }
};

struct DotMatrix : Viz
{
    juce::String name() const override { return "Dot Matrix"; }
    juce::String blurb() const override { return "An LED board: each column is a band, each row a level."; }
    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        const int cols = 48, rows = 24;
        float cw = r.getWidth() * 0.86f / cols, ch = r.getHeight() * 0.7f / rows;
        float x0 = r.getCentreX() - cw * cols / 2, y0 = r.getCentreY() + ch * rows / 2;
        float d = juce::jmin (cw, ch) * 0.62f;
        for (int c = 0; c < cols; ++c)
        {
            int lit = (int) (band (f, (float) c / cols) * k * rows);
            for (int y = 0; y < rows; ++y)
            {
                bool on = y < lit;
                g.setColour (on ? p.fgA (0.55f + 0.45f * y / rows) : p.fgA (0.06f));
                g.fillEllipse (x0 + c * cw + (cw - d) / 2, y0 - (y + 1) * ch + (ch - d) / 2, d, d);
            }
        }
    }
};

struct Rain : Viz
{
    struct Drop { float x, y, sp, len; };
    std::vector<Drop> drops;
    juce::Random rnd { 11 };
    juce::String name() const override { return "Signal Rain"; }
    juce::String blurb() const override { return "Falling streaks that thicken with the high end."; }
    void reset() override { drops.clear(); rnd.setSeed (11); }
    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        int spawn = (int) ((2 + f.treble * 40 * k) * f.dt * 30 + (f.onset ? 30 : 0));
        for (int i = 0; i < spawn; ++i) drops.push_back ({ rnd.nextFloat(), -0.1f, 0.6f + rnd.nextFloat() * 1.4f, 0.03f + rnd.nextFloat() * 0.12f });
        for (auto& d : drops) d.y += d.sp * (float) f.dt * (0.6f + f.energy * 1.5f);
        drops.erase (std::remove_if (drops.begin(), drops.end(), [] (const Drop& d) { return d.y > 1.2f; }), drops.end());
        for (auto& d : drops)
        {
            float x = r.getX() + d.x * r.getWidth(), y = r.getY() + d.y * r.getHeight();
            g.setGradientFill (juce::ColourGradient (p.fgA (0), x, y - d.len * r.getHeight(), p.fgA (0.9f), x, y, false));
            g.fillRect (x, y - d.len * r.getHeight(), px (r, 2), d.len * r.getHeight());
        }
    }
};

struct Flow : Viz
{
    struct Q { Point<float> p; float life; };
    std::vector<Q> qs;
    Trails tr;
    juce::Random rnd { 5 };
    float t = 0;
    juce::String name() const override { return "Flow Field"; }
    juce::String blurb() const override { return "Particles drift through a moving noise field, faster when it's loud."; }
    void reset() override { qs.clear(); tr.clear(); rnd.setSeed (5); }
    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        tr.prepare (r, p, 0.06f);
        if (qs.empty()) for (int i = 0; i < 1500; ++i) qs.push_back ({ { rnd.nextFloat() * r.getWidth(), rnd.nextFloat() * r.getHeight() }, rnd.nextFloat() });
        t += (float) f.dt * (0.1f + f.mid * 0.6f);
        Graphics gi (tr.img);
        float speed = r.getHeight() * (0.05f + f.energy * 0.4f * k + f.beat * 0.2f) * (float) f.dt;
        for (auto& q : qs)
        {
            float a = noise (q.p.x / r.getHeight() * 2.5f, q.p.y / r.getHeight() * 2.5f, t) * kTau * 2;
            auto np = q.p + Point<float> (std::cos (a), std::sin (a)) * speed * 3;
            gi.setColour (p.fgA (0.5f));
            gi.drawLine ({ q.p, np }, px (r, 1.2f));
            q.p = np; q.life -= (float) f.dt * 0.2f;
            if (q.life <= 0 || ! r.withPosition (0, 0).contains (q.p)) q = { { rnd.nextFloat() * r.getWidth(), rnd.nextFloat() * r.getHeight() }, 1.0f };
        }
        tr.blit (g, r);
    }
};

struct Kaleido : Viz
{
    float a = 0;
    juce::String name() const override { return "Kaleidoscope"; }
    juce::String blurb() const override { return "The waveform mirrored into an eight-fold star."; }
    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        a += (float) f.dt * (0.15f + f.mid * 0.8f);
        auto c = r.getCentre();
        Path seg;
        for (int i = 0; i < 256; ++i)
        {
            float u = (float) i / 255;
            float rad = r.getHeight() * 0.45f * u;
            float off = f.L[(size_t) (i * 4)] * r.getHeight() * 0.12f * k;
            if (i == 0) seg.startNewSubPath (rad, off); else seg.lineTo (rad, off);
        }
        for (int s = 0; s < 8; ++s)
            for (int m = 0; m < 2; ++m)
            {
                Path q (seg);
                auto t = juce::AffineTransform::scale (1, m ? -1.0f : 1.0f).rotated (a + s * kTau / 8).translated (c);
                q.applyTransform (t);
                g.setColour (p.fgA (m ? 0.5f : 0.9f));
                g.strokePath (q, PathStrokeType (px (r, 1.8f)));
            }
        float core = r.getHeight() * (0.03f + f.beat * 0.05f);
        g.setColour (p.fg); g.fillEllipse (Rectangle<float> (core * 2, core * 2).withCentre (c));
    }
};

// ============================================================ 21-30

struct Orbits : Viz
{
    float t = 0;
    juce::String name() const override { return "Orbits"; }
    juce::String blurb() const override { return "Bodies circle a core; each orbit is a slice of the spectrum."; }
    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        t += (float) f.dt;
        auto c = r.getCentre();
        const int n = 12;
        for (int i = 0; i < n; ++i)
        {
            float u = (float) i / n, rad = r.getHeight() * (0.08f + u * 0.38f);
            g.setColour (p.fgA (0.2f)); g.drawEllipse (Rectangle<float> (rad * 2, rad * 2 * 0.42f).withCentre (c), px (r, 1.5f));
            float a = t * (1.5f - u) * (0.4f + f.energy) + i * 1.7f;
            float sz = px (r, 6 + band (f, u) * 28 * k);
            Point<float> q (c.x + std::cos (a) * rad, c.y + std::sin (a) * rad * 0.42f);
            g.setColour (p.fgA (0.6f + band (f, u) * 0.4f)); g.fillEllipse (q.x - sz / 2, q.y - sz / 2, sz, sz);
        }
        float core = r.getHeight() * (0.04f + f.bass * 0.05f * k);
        g.setColour (p.fg); g.fillEllipse (Rectangle<float> (core * 2, core * 2).withCentre (c));
    }
};

struct Strings : Viz
{
    float t = 0;
    juce::String name() const override { return "Strings"; }
    juce::String blurb() const override { return "Twelve strings, each set ringing by its own band."; }
    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        t += (float) f.dt;
        const int n = 12;
        for (int i = 0; i < n; ++i)
        {
            float u = (float) i / (n - 1), amp = band (f, u) * band (f, u) * r.getHeight() * 0.11f * k;
            float y0 = r.getY() + r.getHeight() * (0.15f + u * 0.7f);
            int mode = 1 + i % 4;
            Path s;
            for (int x = 0; x <= 200; ++x)
            {
                float v = (float) x / 200;
                float y = y0 + std::sin (v * kPi * mode) * std::sin (t * (8 + i * 3)) * amp;
                if (x == 0) s.startNewSubPath (r.getX() + r.getWidth() * (0.08f + 0.84f * v), y); else s.lineTo (r.getX() + r.getWidth() * (0.08f + 0.84f * v), y);
            }
            g.setColour (p.fgA (0.4f + band (f, u) * 0.6f));
            g.strokePath (s, PathStrokeType (px (r, 1.2f + (n - i) * 0.15f)));
        }
    }
};

struct Seismo : Viz
{
    std::array<std::deque<float>, 6> hist;
    juce::String name() const override { return "Seismograph"; }
    juce::String blurb() const override { return "Six pens on scrolling paper, one per frequency range."; }
    void reset() override { for (auto& h : hist) h.clear(); }
    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        const int len = 240;
        for (int i = 0; i < 6; ++i)
        {
            float v = 0; for (int b = i * kBands / 6; b < (i + 1) * kBands / 6; ++b) v += f.bands[(size_t) b] - f.smooth[(size_t) b];
            v = v / (kBands / 6) * 4.0f + (hash ((int) f.index, i) - 0.5f) * (0.1f + f.flux) * band (f, i / 6.0f);
            v = juce::jlimit (-1.0f, 1.0f, v);
            hist[(size_t) i].push_front (v);
            while ((int) hist[(size_t) i].size() > len) hist[(size_t) i].pop_back();
            float y0 = r.getY() + r.getHeight() * (0.15f + i * 0.14f);
            g.setColour (p.fgA (0.12f)); g.drawHorizontalLine ((int) y0, r.getX() + r.getWidth() * 0.06f, r.getRight() - r.getWidth() * 0.06f);
            Path s;
            for (int j = 0; j < (int) hist[(size_t) i].size(); ++j)
            {
                float x = r.getRight() - r.getWidth() * 0.06f - r.getWidth() * 0.88f * j / len;
                float y = y0 - hist[(size_t) i][(size_t) j] * r.getHeight() * 0.06f * k;
                if (j == 0) s.startNewSubPath (x, y); else s.lineTo (x, y);
            }
            g.setColour (p.fg); g.strokePath (s, PathStrokeType (px (r, 1.6f)));
        }
    }
};

struct Halftone : Viz
{
    juce::String name() const override { return "Halftone"; }
    juce::String blurb() const override { return "A field of print dots that swell from the centre outward."; }
    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        float step = r.getHeight() / 30;
        auto c = r.getCentre();
        float maxD = std::hypot (r.getWidth(), r.getHeight()) * 0.5f;
        for (float y = r.getY() + step / 2; y < r.getBottom(); y += step)
            for (float x = r.getX() + step / 2; x < r.getRight(); x += step)
            {
                float d = std::hypot (x - c.x, y - c.y) / maxD;
                float v = band (f, d) * k * (1.1f - d) + f.beat * 0.3f * (1 - d);
                float sz = juce::jlimit (0.0f, step * 0.95f, step * v);
                if (sz < 0.5f) continue;
                g.setColour (p.fg); g.fillEllipse (x - sz / 2, y - sz / 2, sz, sz);
            }
    }
};

struct Glitch : Viz
{
    juce::Random rnd { 13 };
    juce::String name() const override { return "Glitch"; }
    juce::String blurb() const override { return "A waveform that tears into displaced slices on every hit."; }
    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        rnd.setSeed ((int64_t) f.index / 2);
        const int slices = 18;
        float sh = r.getHeight() / slices;
        for (int s = 0; s < slices; ++s)
        {
            float off = (rnd.nextFloat() - 0.5f) * r.getWidth() * 0.25f * (f.beat * f.beat) * k;
            Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (Rectangle<float> (r.getX(), r.getY() + s * sh, r.getWidth(), sh).toNearestInt());
            Path pth;
            for (int i = 0; i < kWave; i += 2)
            {
                float x = r.getX() + off + r.getWidth() * i / kWave;
                float y = r.getCentreY() - f.L[(size_t) i] * r.getHeight() * 0.4f * k;
                if (i == 0) pth.startNewSubPath (x, y); else pth.lineTo (x, y);
            }
            g.setColour (s % 3 == 0 && f.beat > 0.5f ? p.fgA (0.5f) : p.fg);
            g.strokePath (pth, PathStrokeType (px (r, 3)));
            if (f.beat > 0.4f && rnd.nextFloat() < 0.25f) { g.setColour (p.fgA (0.15f + rnd.nextFloat() * 0.3f)); g.fillRect (r.getX() + rnd.nextFloat() * r.getWidth(), r.getY() + s * sh, r.getWidth() * rnd.nextFloat() * 0.3f, sh); }
        }
    }
};

struct VHS : Viz
{
    juce::Random rnd { 17 };
    juce::String name() const override { return "VHS"; }
    juce::String blurb() const override { return "A tracking-damaged tape: wobbling trace, noise bands and roll."; }
    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        rnd.setSeed ((int64_t) f.index);
        float roll = std::fmod ((float) f.t * 0.07f, 1.0f);
        Path pth;
        for (int i = 0; i < kWave; ++i)
        {
            float y0 = (float) i / kWave;
            float wob = std::sin (y0 * 40 + (float) f.t * 6) * r.getWidth() * 0.004f;
            float x = r.getCentreX() + wob + f.L[(size_t) i] * r.getWidth() * 0.3f * k;
            float y = r.getY() + std::fmod (y0 + roll, 1.0f) * r.getHeight();
            if (i == 0 || std::fmod (y0 + roll, 1.0f) < std::fmod ((float) (i - 1) / kWave + roll, 1.0f)) pth.startNewSubPath (x, y); else pth.lineTo (x, y);
        }
        g.setColour (p.fgA (0.85f)); g.strokePath (pth, PathStrokeType (px (r, 2.5f)));
        for (int n = 0; n < 3; ++n)
        {
            float y = r.getY() + rnd.nextFloat() * r.getHeight(), h = px (r, 4 + rnd.nextFloat() * 30);
            for (float x = r.getX(); x < r.getRight(); x += px (r, 6)) { g.setColour (p.fgA (rnd.nextFloat() * 0.25f * (0.3f + f.flux))); g.fillRect (x, y, px (r, 6), h); }
        }
        g.setColour (p.fgA (0.12f)); g.fillRect (r.getX(), r.getY() + std::fmod (roll * 3, 1.0f) * r.getHeight(), r.getWidth(), px (r, 18));
    }
};

struct Constellation : Viz
{
    std::vector<Point<float>> home;
    juce::Random rnd { 21 };
    float t = 0;
    juce::String name() const override { return "Constellation"; }
    juce::String blurb() const override { return "Stars wander and link up when the music pulls them close."; }
    void reset() override { home.clear(); rnd.setSeed (21); }
    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        if (home.empty()) for (int i = 0; i < 90; ++i) home.push_back ({ rnd.nextFloat(), rnd.nextFloat() });
        t += (float) f.dt * (0.2f + f.energy);
        std::vector<Point<float>> pts;
        for (size_t i = 0; i < home.size(); ++i)
        {
            float u = (float) i / home.size();
            float dx = (noise (u * 9, t) - 0.5f) * 0.12f * (1 + band (f, u) * 3 * k), dy = (noise (u * 9 + 50, t) - 0.5f) * 0.12f * (1 + band (f, u) * 3 * k);
            pts.push_back ({ r.getX() + (home[i].x + dx) * r.getWidth(), r.getY() + (home[i].y + dy) * r.getHeight() });
        }
        float link = r.getHeight() * (0.12f + f.energy * 0.12f + f.beat * 0.05f);
        for (size_t i = 0; i < pts.size(); ++i)
            for (size_t j = i + 1; j < pts.size(); ++j)
            {
                float d = pts[i].getDistanceFrom (pts[j]);
                if (d < link) { g.setColour (p.fgA ((1 - d / link) * 0.7f)); g.drawLine ({ pts[i], pts[j] }, px (r, 1.2f)); }
            }
        for (size_t i = 0; i < pts.size(); ++i) { float sz = px (r, 3 + band (f, (float) i / pts.size()) * 7); g.setColour (p.fg); g.fillEllipse (pts[i].x - sz / 2, pts[i].y - sz / 2, sz, sz); }
    }
};

struct Ripples : Viz
{
    struct Rp { Point<float> c; float r, a; };
    std::vector<Rp> rs;
    juce::Random rnd { 23 };
    juce::String name() const override { return "Ripple Pond"; }
    juce::String blurb() const override { return "Each hit drops a stone somewhere on still water."; }
    void reset() override { rs.clear(); rnd.setSeed (23); }
    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        if (f.onset || (f.rms > 0.3f && rnd.nextFloat() < 0.05f)) rs.push_back ({ { r.getX() + rnd.nextFloat() * r.getWidth(), r.getY() + rnd.nextFloat() * r.getHeight() }, 0, 0.9f });
        for (auto& x : rs) { x.r += r.getHeight() * 0.25f * (float) f.dt * k; x.a *= (float) std::exp (-f.dt * 0.7); }
        rs.erase (std::remove_if (rs.begin(), rs.end(), [] (const Rp& x) { return x.a < 0.02f; }), rs.end());
        for (auto& x : rs)
            for (int ring = 0; ring < 3; ++ring)
            {
                float rr = x.r - ring * px (r, 22);
                if (rr <= 0) continue;
                g.setColour (p.fgA (x.a * (1 - ring * 0.3f)));
                g.drawEllipse (Rectangle<float> (rr * 2, rr * 0.8f).withCentre (x.c), px (r, 2));
            }
    }
};

struct Helix : Viz
{
    float a = 0;
    juce::String name() const override { return "Double Helix"; }
    juce::String blurb() const override { return "Two strands twist across the screen, rungs lit by the spectrum."; }
    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        a += (float) f.dt * (1 + f.energy * 2);
        const int n = 60;
        for (int i = 0; i < n; ++i)
        {
            float u = (float) i / (n - 1), x = r.getX() + r.getWidth() * (0.05f + 0.9f * u);
            float ph = u * kTau * 2.5f + a;
            float amp = r.getHeight() * (0.18f + band (f, u) * 0.12f * k);
            float y1 = r.getCentreY() + std::sin (ph) * amp, y2 = r.getCentreY() - std::sin (ph) * amp;
            float depth = std::cos (ph);
            g.setColour (p.fgA (0.15f + band (f, u) * 0.6f)); g.drawLine (x, y1, x, y2, px (r, 1.5f));
            float s1 = px (r, 5 + depth * 3), s2 = px (r, 5 - depth * 3);
            g.setColour (p.fgA (0.6f + depth * 0.4f)); g.fillEllipse (x - s1 / 2, y1 - s1 / 2, s1, s1);
            g.setColour (p.fgA (0.6f - depth * 0.4f)); g.fillEllipse (x - s2 / 2, y2 - s2 / 2, s2, s2);
        }
    }
};

struct Mandala : Viz
{
    float a = 0;
    juce::String name() const override { return "Mandala"; }
    juce::String blurb() const override { return "Layered rose curves; petals grow with each band group."; }
    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        a += (float) f.dt * 0.2f;
        auto c = r.getCentre();
        const int layers = 6;
        for (int l = 0; l < layers; ++l)
        {
            int petals = 3 + l * 2;
            float v = band (f, (float) l / layers) * k;
            float rad = r.getHeight() * (0.1f + l * 0.06f) * (0.7f + v * 0.6f);
            Path pth;
            for (int i = 0; i <= 720; ++i)
            {
                float th = kTau * i / 720;
                float rr = rad * (0.6f + 0.4f * std::abs (std::cos (petals * th / 2)));
                Point<float> q (c.x + std::cos (th + a * (l % 2 ? 1 : -1)) * rr, c.y + std::sin (th + a * (l % 2 ? 1 : -1)) * rr);
                if (i == 0) pth.startNewSubPath (q); else pth.lineTo (q);
            }
            g.setColour (p.fgA (0.3f + v * 0.7f)); g.strokePath (pth, PathStrokeType (px (r, 1.6f)));
        }
    }
};

// ============================================================ 31-40

struct Lightning : Viz
{
    struct Bolt { Path path; float a; };
    std::vector<Bolt> bolts;
    juce::Random rnd { 29 };
    juce::String name() const override { return "Lightning"; }
    juce::String blurb() const override { return "Forked bolts strike on the big hits."; }
    void reset() override { bolts.clear(); rnd.setSeed (29); }
    void addBolt (Path& p, Point<float> a, Point<float> b, float jitter, int depth)
    {
        if (depth == 0) { p.startNewSubPath (a); p.lineTo (b); return; }
        Point<float> m ((a.x + b.x) / 2 + (rnd.nextFloat() - 0.5f) * jitter, (a.y + b.y) / 2 + (rnd.nextFloat() - 0.5f) * jitter * 0.3f);
        addBolt (p, a, m, jitter / 2, depth - 1);
        addBolt (p, m, b, jitter / 2, depth - 1);
        if (depth > 3 && rnd.nextFloat() < 0.3f) addBolt (p, m, m + Point<float> ((rnd.nextFloat() - 0.5f) * jitter * 2, jitter * 1.5f), jitter / 2, depth - 2);
    }
    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        if (f.onset && f.rms > 0.05f)
        {
            Bolt b; b.a = 1;
            float x = r.getX() + rnd.nextFloat() * r.getWidth();
            addBolt (b.path, { x, r.getY() }, { x + (rnd.nextFloat() - 0.5f) * r.getWidth() * 0.4f, r.getBottom() }, r.getHeight() * 0.5f * k, 7);
            bolts.push_back (b);
            g.setColour (p.fgA (0.25f * f.rms)); g.fillRect (r);
        }
        for (auto& b : bolts)
        {
            g.setColour (p.fgA (b.a * 0.3f)); g.strokePath (b.path, PathStrokeType (px (r, 10)));
            g.setColour (p.fgA (b.a)); g.strokePath (b.path, PathStrokeType (px (r, 2.5f)));
            b.a *= (float) std::exp (-f.dt * 6);
        }
        bolts.erase (std::remove_if (bolts.begin(), bolts.end(), [] (const Bolt& b) { return b.a < 0.03f; }), bolts.end());
        g.setColour (p.fgA (0.15f + f.bass * 0.3f));
        g.fillRect (r.getX(), r.getBottom() - px (r, 3), r.getWidth(), px (r, 3));
    }
};

struct Vortex : Viz
{
    float a = 0;
    juce::String name() const override { return "Vortex"; }
    juce::String blurb() const override { return "Spiral arms wind tighter and faster with the energy."; }
    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        a += (float) f.dt * (0.4f + f.energy * 3 * k);
        auto c = r.getCentre();
        const int arms = 7;
        for (int arm = 0; arm < arms; ++arm)
        {
            Path s;
            for (int i = 0; i < 160; ++i)
            {
                float u = (float) i / 159;
                float rad = r.getHeight() * 0.55f * u;
                float th = a + arm * kTau / arms + u * (3.5f + f.bass * 3);
                float wob = band (f, u) * r.getHeight() * 0.03f * k;
                Point<float> q (c.x + std::cos (th) * (rad + wob), c.y + std::sin (th) * (rad + wob));
                if (i == 0) s.startNewSubPath (q); else s.lineTo (q);
            }
            g.setColour (p.fgA (0.4f + 0.6f * arm / arms)); g.strokePath (s, PathStrokeType (px (r, 2.2f)));
        }
    }
};

struct LedStack : Viz
{
    std::array<float, 20> peaks {};
    juce::String name() const override { return "LED Stack"; }
    juce::String blurb() const override { return "A classic segmented equalizer with falling peak caps."; }
    void reset() override { peaks.fill (0); }
    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        const int n = 20, segs = 18;
        float area = r.getWidth() * 0.7f, cw = area / n, x0 = r.getCentreX() - area / 2;
        float sh = r.getHeight() * 0.6f / segs, base = r.getY() + r.getHeight() * 0.8f;
        for (int i = 0; i < n; ++i)
        {
            float v = juce::jlimit (0.0f, 1.0f, band (f, (float) i / n) * k);
            peaks[(size_t) i] = juce::jmax (v, peaks[(size_t) i] - (float) f.dt * 0.5f);
            int lit = (int) (v * segs), pk = (int) (peaks[(size_t) i] * segs);
            for (int s = 0; s < segs; ++s)
            {
                auto seg = Rectangle<float> (x0 + i * cw + cw * 0.1f, base - (s + 1) * sh + sh * 0.15f, cw * 0.8f, sh * 0.7f);
                g.setColour (s < lit ? p.ink (0.55f + 0.45f * s / segs) : (s == pk ? p.fg : p.fgA (0.06f)));
                g.fillRoundedRectangle (seg, px (r, 2));
            }
        }
    }
};

struct Topo : Viz
{
    float t = 0;
    juce::String name() const override { return "Contours"; }
    juce::String blurb() const override { return "Topographic lines over a shifting landscape shaped by the bass."; }
    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        t += (float) f.dt * (0.08f + f.energy * 0.4f);
        const int gx = 64, gy = 36;
        std::vector<float> h ((size_t) ((gx + 1) * (gy + 1)));
        for (int y = 0; y <= gy; ++y)
            for (int x = 0; x <= gx; ++x)
            {
                float d = std::hypot (x - gx / 2.0f, (y - gy / 2.0f) * 1.6f) / gx;
                h[(size_t) (y * (gx + 1) + x)] = noise (x * 0.08f, y * 0.08f, t) + f.bass * k * 0.6f * std::exp (-d * 6) + f.beat * 0.15f * std::exp (-d * 3);
            }
        float cw = r.getWidth() / gx, ch = r.getHeight() / gy;
        for (int level = 1; level < 12; ++level)
        {
            float iso = level / 12.0f * 1.2f;
            g.setColour (p.fgA (0.25f + 0.06f * level));
            for (int y = 0; y < gy; ++y)
                for (int x = 0; x < gx; ++x)   // marching squares, one segment per cell
                {
                    float a = h[(size_t) (y * (gx + 1) + x)], b = h[(size_t) (y * (gx + 1) + x + 1)], c = h[(size_t) ((y + 1) * (gx + 1) + x + 1)], d = h[(size_t) ((y + 1) * (gx + 1) + x)];
                    std::vector<Point<float>> pts;
                    auto edge = [&] (float v0, float v1, Point<float> p0, Point<float> p1) { if ((v0 < iso) != (v1 < iso)) { float u = (iso - v0) / (v1 - v0); pts.push_back (p0 + (p1 - p0) * u); } };
                    Point<float> P0 (r.getX() + x * cw, r.getY() + y * ch), P1 (P0.x + cw, P0.y), P2 (P0.x + cw, P0.y + ch), P3 (P0.x, P0.y + ch);
                    edge (a, b, P0, P1); edge (b, c, P1, P2); edge (c, d, P2, P3); edge (d, a, P3, P0);
                    if (pts.size() >= 2) g.drawLine ({ pts[0], pts[1] }, px (r, 1.3f));
                    if (pts.size() == 4) g.drawLine ({ pts[2], pts[3] }, px (r, 1.3f));
                }
        }
    }
};

struct Mesh : Viz
{
    std::deque<std::array<float, 48>> rows;
    float a = 0;
    juce::String name() const override { return "Wave Mesh"; }
    juce::String blurb() const override { return "The last few seconds of waveform as a rotating 3D surface."; }
    void reset() override { rows.clear(); }
    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        std::array<float, 48> row {};
        for (int i = 0; i < 48; ++i) row[(size_t) i] = f.L[(size_t) (i * kWave / 48)];
        rows.push_front (row);
        while (rows.size() > 36) rows.pop_back();
        a += (float) f.dt * 0.15f;
        std::vector<std::array<Point<float>, 48>> pts (rows.size());
        for (size_t j = 0; j < rows.size(); ++j)
            for (int i = 0; i < 48; ++i)
            {
                P3 q { (i - 23.5f) / 24.0f, rows[j][(size_t) i] * 0.5f * k, ((float) j - 18) / 18.0f };
                q = rotX (rotY (q, std::sin (a) * 0.6f), 0.55f);
                float sc; pts[j][(size_t) i] = project (q, r, 2.6f, sc);
            }
        for (size_t j = 0; j < pts.size(); ++j)
        {
            g.setColour (p.fgA (1 - (float) j / pts.size() * 0.8f));
            Path pl; pl.startNewSubPath (pts[j][0]); for (int i = 1; i < 48; ++i) pl.lineTo (pts[j][(size_t) i]);
            g.strokePath (pl, PathStrokeType (px (r, 1.3f)));
            if (j + 1 < pts.size()) for (int i = 0; i < 48; i += 3) g.drawLine ({ pts[j][(size_t) i], pts[j + 1][(size_t) i] }, px (r, 0.8f));
        }
    }
};

struct Bloom : Viz
{
    juce::String name() const override { return "Eclipse"; }
    juce::String blurb() const override { return "A black disc whose corona flares with the spectrum."; }
    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        auto c = r.getCentre();
        float rad = r.getHeight() * (0.22f + f.energy * 0.04f);
        for (int ray = 0; ray < 360; ++ray)
        {
            float a = kTau * ray / 360;
            float u = std::abs (std::sin (a * 1.5f));
            float len = rad * (0.15f + band (f, u) * 0.9f * k + hash (ray, (int) f.index / 3) * 0.08f * f.treble);
            g.setColour (p.fgA (0.08f + band (f, u) * 0.5f));
            g.drawLine (c.x + std::cos (a) * rad, c.y + std::sin (a) * rad, c.x + std::cos (a) * (rad + len), c.y + std::sin (a) * (rad + len), px (r, 2.5f));
        }
        g.setColour (p.fgA (0.9f)); g.drawEllipse (Rectangle<float> (rad * 2, rad * 2).withCentre (c), px (r, 3 + f.beat * 6));
        g.setColour (p.bg); g.fillEllipse (Rectangle<float> (rad * 2 - px (r, 4), rad * 2 - px (r, 4)).withCentre (c));
    }
};

struct Barcode : Viz
{
    std::deque<float> cols;
    juce::String name() const override { return "Barcode"; }
    juce::String blurb() const override { return "Bars of changing weight scroll by like a printout of the track."; }
    void reset() override { cols.clear(); }
    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        cols.push_front (juce::jlimit (0.0f, 1.0f, (f.rms * 0.9f + f.flux * 0.8f + f.beat * 0.3f) * k));
        while (cols.size() > 160) cols.pop_back();
        float cw = r.getWidth() / 160;
        for (size_t i = 0; i < cols.size(); ++i)
        {
            float w = cw * (0.08f + cols[i] * cols[i] * 0.7f);
            g.setColour (p.fg);
            g.fillRect (r.getRight() - (i + 1) * cw, r.getY() + r.getHeight() * 0.25f, w, r.getHeight() * 0.5f);
        }
        g.setFont (juce::Font (juce::FontOptions (px (r, 22))));
        g.setColour (p.fgA (0.6f));
        g.drawText (juce::String::formatted ("%07lld", (long long) f.index), r.withTrimmedTop (r.getHeight() * 0.78f).withHeight (px (r, 30)), juce::Justification::centred);
    }
};

struct BandLines : Viz
{
    juce::String name() const override { return "Split Signal"; }
    juce::String blurb() const override { return "Lows, mids and highs drawn as three separate traces."; }
    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        // crude three-way split: moving averages of the waveform
        std::array<float, kWave> low {}, high {};
        float acc = 0;
        for (int i = 0; i < kWave; ++i) { acc += (f.L[(size_t) i] - acc) * 0.03f; low[(size_t) i] = acc; }
        float hp = 0, prevx = 0;
        for (int i = 0; i < kWave; ++i) { hp = 0.8f * (hp + f.L[(size_t) i] - prevx); prevx = f.L[(size_t) i]; high[(size_t) i] = hp; }
        const float* sig[3] = { low.data(), f.L.data(), high.data() };
        float gain[3] = { 3.0f, 1.0f, 2.5f };
        for (int s = 0; s < 3; ++s)
        {
            float y0 = r.getY() + r.getHeight() * (0.25f + s * 0.25f);
            Path pth;
            for (int i = 0; i < kWave; i += 2)
            {
                float x = r.getX() + r.getWidth() * (0.06f + 0.88f * i / kWave);
                float y = y0 - juce::jlimit (-1.0f, 1.0f, sig[s][i] * gain[s]) * r.getHeight() * 0.1f * k;
                if (i == 0) pth.startNewSubPath (x, y); else pth.lineTo (x, y);
            }
            g.setColour (p.fgA (s == 1 ? 1.0f : 0.7f)); g.strokePath (pth, PathStrokeType (px (r, 1.8f)));
        }
    }
};

struct City : Viz
{
    float a = 0;
    juce::String name() const override { return "Monoliths"; }
    juce::String blurb() const override { return "A slowly turning city of columns, each one a band."; }
    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        a += (float) f.dt * 0.12f;
        struct Col { float depth; Point<float> base[4], top[4]; float v; };
        std::vector<Col> cols;
        const int n = 8;
        for (int gx = 0; gx < n; ++gx)
            for (int gz = 0; gz < n; ++gz)
            {
                float v = band (f, (float) ((gx * n + gz) % 64) / 64) * k;
                float hgt = 0.05f + v * 0.9f;
                float cx = (gx - 3.5f) * 0.22f, cz = (gz - 3.5f) * 0.22f, s = 0.07f;
                Col c; c.v = v; float dsum = 0;
                P3 corners[4] = { { cx - s, 0, cz - s }, { cx + s, 0, cz - s }, { cx + s, 0, cz + s }, { cx - s, 0, cz + s } };
                for (int i = 0; i < 4; ++i)
                {
                    P3 b = rotX (rotY (P3 { corners[i].x, -0.45f, corners[i].z }, a), 0.5f), t = rotX (rotY (P3 { corners[i].x, -0.45f + hgt, corners[i].z }, a), 0.5f);
                    float sc; c.base[i] = project (b, r, 2.4f, sc); c.top[i] = project (t, r, 2.4f, sc); dsum += b.z;
                }
                c.depth = dsum;
                cols.push_back (c);
            }
        std::sort (cols.begin(), cols.end(), [] (const Col& x, const Col& y) { return x.depth > y.depth; });
        for (auto& c : cols)
        {
            for (int i = 0; i < 4; ++i)
            {
                Path side; side.startNewSubPath (c.base[i]); side.lineTo (c.base[(i + 1) % 4]); side.lineTo (c.top[(i + 1) % 4]); side.lineTo (c.top[i]); side.closeSubPath();
                g.setColour (p.ink (0.15f + 0.25f * (i % 2))); g.fillPath (side);
            }
            Path top; top.startNewSubPath (c.top[0]); for (int i = 1; i < 4; ++i) top.lineTo (c.top[i]); top.closeSubPath();
            g.setColour (p.ink (0.5f + c.v * 0.5f)); g.fillPath (top);
        }
    }
};

struct Dust : Viz
{
    struct Mote { float x, y, vx, vy, s; };
    std::vector<Mote> ms;
    juce::Random rnd { 31 };
    juce::String name() const override { return "Dust & Grain"; }
    juce::String blurb() const override { return "Drifting motes that catch the light on every beat, with film grain."; }
    void reset() override { ms.clear(); rnd.setSeed (31); }
    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        if (ms.empty()) for (int i = 0; i < 400; ++i) ms.push_back ({ rnd.nextFloat(), rnd.nextFloat(), (rnd.nextFloat() - 0.5f) * 0.02f, -0.01f - rnd.nextFloat() * 0.03f, rnd.nextFloat() });
        float dt = (float) f.dt;
        for (auto& m : ms)
        {
            m.x += m.vx * dt * (1 + f.energy * 4); m.y += m.vy * dt * (1 + f.energy * 4);
            if (m.y < -0.05f) { m.y = 1.05f; m.x = rnd.nextFloat(); }
            if (m.x < -0.05f) m.x = 1.05f; if (m.x > 1.05f) m.x = -0.05f;
            float sz = px (r, 2.0f + m.s * 7 + f.bass * 4 * m.s);
            g.setColour (p.fgA ((0.3f + m.s * 0.6f) * (0.6f + f.beat * k + f.energy)));
            g.fillEllipse (r.getX() + m.x * r.getWidth(), r.getY() + m.y * r.getHeight(), sz, sz);
        }
        juce::Random gr ((int64_t) f.index * 7919);
        for (int i = 0; i < 2500; ++i) { g.setColour (p.fgA (gr.nextFloat() * 0.12f)); g.fillRect (r.getX() + gr.nextFloat() * r.getWidth(), r.getY() + gr.nextFloat() * r.getHeight(), px (r, 2), px (r, 2)); }
        g.setColour (p.fgA (0.6f + f.bass * 0.4f * k)); g.setFont (juce::Font (juce::FontOptions (px (r, 14))));
    }
};

struct Heartbeat : Viz
{
    std::deque<float> trace;
    juce::String name() const override { return "Heart Monitor"; }
    juce::String blurb() const override { return "An ECG trace that spikes on every beat."; }
    void reset() override { trace.clear(); }
    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        static const float shape[] = { 0, 0.1f, -0.15f, 1.0f, -0.6f, 0.15f, 0.25f, 0.1f, 0 };
        for (int s = 0; s < 4; ++s) trace.push_front (f.rms * 0.05f);
        while (trace.size() < 9) trace.push_back (0);
        if (f.onset) for (int i = 8; i >= 0; --i) trace[(size_t) i] = shape[8 - i] * (0.4f + f.peak * 0.6f) * k;
        while (trace.size() > 600) trace.pop_back();
        Path pth;
        for (size_t i = 0; i < trace.size(); ++i)
        {
            float x = r.getRight() - r.getWidth() * 0.05f - r.getWidth() * 0.9f * i / 600, y = r.getCentreY() - trace[i] * r.getHeight() * 0.35f;
            if (i == 0) pth.startNewSubPath (x, y); else pth.lineTo (x, y);
        }
        g.setColour (p.fgA (0.25f)); g.strokePath (pth, PathStrokeType (px (r, 7)));
        g.setColour (p.fg); g.strokePath (pth, PathStrokeType (px (r, 2.2f)));
        float dot = px (r, 10 + f.beat * 10);
        g.fillEllipse (r.getRight() - r.getWidth() * 0.05f - dot / 2, r.getCentreY() - trace.front() * r.getHeight() * 0.35f - dot / 2, dot, dot);
    }
};

struct Rings3D : Viz
{
    float a = 0;
    juce::String name() const override { return "Gyroscope"; }
    juce::String blurb() const override { return "Interlocking rings tumbling at speeds set by the band groups."; }
    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        a += (float) f.dt;
        float speeds[5] = { 0.4f + f.bass * 2 * k, 0.6f + f.mid * 2 * k, 0.8f + f.treble * 2 * k, 0.3f + f.energy * k, 0.5f + f.beat * k };
        for (int ring = 0; ring < 5; ++ring)
        {
            float rad = 0.35f + ring * 0.07f;
            Path pth;
            for (int i = 0; i <= 120; ++i)
            {
                float th = kTau * i / 120;
                P3 q { std::cos (th) * rad, std::sin (th) * rad, 0 };
                q = rotX (rotY (rotZ (q, ring * 0.7f), a * speeds[ring]), a * speeds[(ring + 2) % 5] * 0.6f);
                float sc; auto pt = project (q, r, 2.2f, sc);
                if (i == 0) pth.startNewSubPath (pt); else pth.lineTo (pt);
            }
            g.setColour (p.fgA (0.5f + ring * 0.1f)); g.strokePath (pth, PathStrokeType (px (r, 2.5f)));
        }
    }
};

struct Typewriter : Viz
{
    juce::String name() const override { return "Signal Text"; }
    juce::String blurb() const override { return "The spectrum rendered as a wall of characters, like a terminal."; }
    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        static const char* ramp = " .:-=+*#%@";
        const int cols = 64, rows = 24;
        float cw = r.getWidth() / cols, ch = r.getHeight() * 0.72f / rows;
        r = r.withTrimmedTop (r.getHeight() * 0.06f);
        g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), ch * 0.9f, juce::Font::plain)));
        for (int c = 0; c < cols; ++c)
        {
            float v = band (f, (float) c / cols) * k;
            for (int y = 0; y < rows; ++y)
            {
                float level = (rows - y) / (float) rows;
                float d = v - level + 0.1f;
                if (d <= 0) continue;
                int idx = juce::jlimit (1, 9, (int) (d * 25) + (int) (hash (c, y, (int) f.index / 4) * 2));
                g.setColour (p.fgA (0.4f + juce::jmin (0.6f, d * 3)));
                g.drawText (juce::String::charToString (ramp[idx]), Rectangle<float> (r.getX() + c * cw, r.getY() + y * ch, cw, ch), juce::Justification::centred);
            }
        }
    }
};

struct Pendulum : Viz
{
    float t = 0;
    juce::String name() const override { return "Pendulum Wave"; }
    juce::String blurb() const override { return "A row of pendulums drifting in and out of phase, swinging harder when it's loud."; }
    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        t += (float) f.dt * (0.6f + f.energy);
        const int n = 24;
        float topY = r.getY() + r.getHeight() * 0.12f;
        g.setColour (p.fgA (0.3f)); g.drawLine (r.getX() + r.getWidth() * 0.1f, topY, r.getRight() - r.getWidth() * 0.1f, topY, px (r, 2));
        for (int i = 0; i < n; ++i)
        {
            float u = (float) i / (n - 1), x0 = r.getX() + r.getWidth() * (0.12f + 0.76f * u);
            float len = r.getHeight() * (0.4f + u * 0.35f);
            float swing = (0.3f + band (f, u) * 0.6f * k) * std::sin (t * (2.0f + i * 0.08f));
            Point<float> bob (x0 + std::sin (swing) * len * 0.5f, topY + std::cos (swing) * len);
            g.setColour (p.fgA (0.35f)); g.drawLine ({ { x0, topY }, bob }, px (r, 1.2f));
            float sz = px (r, 10 + band (f, u) * 14);
            g.setColour (p.fg); g.fillEllipse (bob.x - sz / 2, bob.y - sz / 2, sz, sz);
        }
    }
};

struct Smoke : Viz
{
    Trails tr;
    float t = 0;
    juce::String name() const override { return "Smoke"; }
    juce::String blurb() const override { return "Slow plumes rising from the bottom, stirred by the mids."; }
    void reset() override { tr.clear(); }
    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        tr.prepare (r, p, 0.05f);
        t += (float) f.dt;
        Graphics gi (tr.img);
        auto lr = r.withPosition (0, 0);
        for (int plume = 0; plume < 5; ++plume)
        {
            float x = lr.getWidth() * (0.15f + plume * 0.175f);
            for (int s = 0; s < 120; ++s)
            {
                float u = s / 120.0f;
                float y = lr.getBottom() - u * lr.getHeight() * (0.6f + f.energy * 0.5f * k);
                float dx = (noise (plume * 3.0f, u * 4 - t * 0.6f, t * 0.2f) - 0.5f) * lr.getWidth() * 0.15f * (0.5f + f.mid * 2);
                float sz = px (r, 20 + u * 140);
                gi.setColour (p.fgA ((1 - u) * 0.025f * (0.4f + band (f, plume / 5.0f) * 1.5f)));
                gi.fillEllipse (x + dx - sz / 2, y - sz / 2, sz, sz);
            }
        }
        tr.blit (g, r);
    }
};

struct Prism : Viz
{
    float a = 0;
    juce::String name() const override { return "Prism"; }
    juce::String blurb() const override { return "A beam enters a turning prism and fans out into the spectrum."; }
    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        a += (float) f.dt * 0.1f;
        auto c = r.getCentre();
        float s = r.getHeight() * 0.18f;
        Path tri; tri.addTriangle (c.x, c.y - s, c.x - s * 0.9f, c.y + s * 0.6f, c.x + s * 0.9f, c.y + s * 0.6f);
        tri.applyTransform (juce::AffineTransform::rotation (std::sin (a) * 0.2f, c.x, c.y));
        g.setColour (p.fgA (0.6f + f.rms * 0.4f)); g.drawLine (r.getX(), c.y + s * 0.2f, c.x - s * 0.45f, c.y, px (r, 3 + f.rms * 6));
        const int rays = 32;
        for (int i = 0; i < rays; ++i)
        {
            float u = (float) i / (rays - 1), ang = -0.35f + u * 0.7f;
            float v = band (f, u) * k;
            Point<float> start (c.x + s * 0.45f, c.y), end (r.getRight(), c.y + std::tan (ang) * (r.getRight() - start.x));
            g.setColour (p.fgA (0.1f + v * 0.8f)); g.drawLine ({ start, end }, px (r, 1 + v * 5));
        }
        g.setColour (p.bg); g.fillPath (tri);
        g.setColour (p.fg); g.strokePath (tri, PathStrokeType (px (r, 2.5f)));
    }
};

struct Clockwork : Viz
{
    float a = 0;
    juce::String name() const override { return "Clockwork"; }
    juce::String blurb() const override { return "Meshing gears driven by the low end."; }
    void gear (Graphics& g, Point<float> c, float rad, int teeth, float rot, const Palette& p, float alpha, float w)
    {
        Path pth;
        for (int i = 0; i <= teeth * 4; ++i)
        {
            float th = rot + kTau * i / (teeth * 4);
            float rr = rad * ((i % 4 == 1 || i % 4 == 2) ? 1.0f : 0.86f);
            Point<float> q (c.x + std::cos (th) * rr, c.y + std::sin (th) * rr);
            if (i == 0) pth.startNewSubPath (q); else pth.lineTo (q);
        }
        pth.closeSubPath();
        g.setColour (p.fgA (alpha)); g.strokePath (pth, PathStrokeType (w));
        g.drawEllipse (Rectangle<float> (rad * 0.5f, rad * 0.5f).withCentre (c), w);
    }
    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        a += (float) f.dt * (0.2f + f.bass * 2.5f * k + f.beat);
        float s = r.getHeight();
        auto c = r.getCentre();
        gear (g, c, s * 0.22f, 24, a, p, 1.0f, px (r, 3));
        gear (g, c + Point<float> (s * 0.33f, -s * 0.08f), s * 0.12f, 13, -a * 24 / 13 + 0.12f, p, 0.8f, px (r, 2.5f));
        gear (g, c + Point<float> (-s * 0.3f, s * 0.13f), s * 0.1f, 11, -a * 24 / 11, p, 0.7f, px (r, 2.5f));
        gear (g, c + Point<float> (s * 0.33f, s * 0.12f), s * 0.07f, 7, a * 24 / 7 * 0.5f, p, 0.6f, px (r, 2));
    }
};

struct Aurora : Viz
{
    float t = 0;
    juce::String name() const override { return "Aurora"; }
    juce::String blurb() const override { return "Curtains of light that ripple with the high end."; }
    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        t += (float) f.dt * (0.2f + f.treble);
        for (int curtain = 0; curtain < 4; ++curtain)
        {
            for (int i = 0; i < 160; ++i)
            {
                float u = i / 159.0f, x = r.getX() + u * r.getWidth();
                float y = r.getY() + r.getHeight() * (0.25f + curtain * 0.08f) + (noise (u * 3 + curtain * 7, t) - 0.5f) * r.getHeight() * 0.2f;
                float h = r.getHeight() * (0.15f + band (f, u) * 0.4f * k) * (0.6f + noise (u * 8, t * 2 + curtain));
                g.setGradientFill (juce::ColourGradient (p.fgA (0.22f - curtain * 0.04f), x, y, p.fgA (0), x, y + h, false));
                g.fillRect (x, y, r.getWidth() / 160 + 1, h);
            }
        }
    }
};
}

// ============================================================ 49

struct Moth : Viz   // a moth drawn in light, flapping on the kick, shedding dust, with echoes tunnelling out behind it
{
    Trails tr;
    struct Mote { float x, y, vx, vy, life, size; };
    std::vector<Mote> dust;
    juce::Random rnd { 1999 };
    float t = 0, phase = 0, punch = 0;
    juce::String name() const override { return "Moth"; }
    juce::String blurb() const override { return "A moth drawn in light: wings beat with the kick, patterns flow out of its body, dust and echoes trail off it."; }
    void reset() override { tr.clear(); dust.clear(); t = phase = punch = 0; }

    static Path forewing()
    {
        Path w;   // long and triangular, the way a moth holds them
        w.startNewSubPath (4, -22);
        w.cubicTo (110, -122, 250, -192, 345, -178);
        w.cubicTo (362, -120, 322, -40, 270, -6);
        w.cubicTo (180, 10, 70, 14, 6, 4);
        w.closeSubPath();
        return w;
    }
    static Path hindwing()
    {
        Path w;
        w.startNewSubPath (6, 4);
        w.cubicTo (90, 10, 210, 20, 236, 70);
        w.cubicTo (252, 130, 190, 172, 130, 166);
        w.cubicTo (70, 160, 20, 100, 6, 30);
        w.closeSubPath();
        return w;
    }

    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        tr.prepare (r, p, 0.2f);
        const float dt = (float) f.dt;
        t += dt;
        if (f.onset) punch = 1;
        punch = juce::jmax (0.0f, punch - dt * 5);
        phase += dt * (1.6f + 9.0f * f.bass * k) + punch * dt * 6;
        Graphics gi (tr.img);
        auto lr = r.withPosition (0, 0);
        const float u = lr.getHeight() / 1080.0f;
        const float sc = u * (1.12f + 0.12f * f.energy * k + 0.06f * punch);
        const Point<float> c (lr.getCentreX() + std::sin (t * 0.6f) * 24 * u, lr.getCentreY() + 40 * u + std::cos (t * 0.9f) * 14 * u);
        const float tilt = std::sin (t * 0.4f) * 0.07f;
        const float open = 1.0f - 0.42f * (0.5f - 0.5f * std::cos (phase)) - 0.15f * punch;   // wings fold toward the body on the beat
        const float lw = px (r, 2.2f);

        // the lamp it's drawn to: a soft glow and faint rays behind the body (kept dim, the trails build it up)
        {
            const Point<float> lamp (c.x, c.y - 30 * u);
            float rad = (60 + 110 * f.bass * k + 50 * punch) * u;
            juce::ColourGradient glow (p.fgA (0.07f + 0.06f * punch), lamp.x, lamp.y, p.fgA (0.0f), lamp.x + rad * 1.8f, lamp.y, true);
            gi.setGradientFill (glow);
            gi.fillEllipse (lamp.x - rad * 1.8f, lamp.y - rad * 1.8f, rad * 3.6f, rad * 3.6f);
            for (int i = 0; i < 24; ++i)
            {
                float a = i * kTau / 24 + t * 0.15f, len = (160 + 320 * band (f, i / 24.0f) * k) * u;
                gi.setColour (p.fgA (0.03f + 0.05f * band (f, i / 24.0f)));
                gi.drawLine (lamp.x, lamp.y, lamp.x + std::cos (a) * len, lamp.y + std::sin (a) * len, px (r, 1.2f));
            }
        }

        auto place = [&] (float side, float s, float rot) {
            return juce::AffineTransform::scale (side * open * s, s).rotated (tilt + rot).translated (c.x, c.y);
        };
        const Path fw = forewing(), hw = hindwing();

        // echoes tunnelling outward, turning a little each ring
        for (int e = 5; e >= 1; --e)
        {
            float grow = 1 + e * (0.22f + 0.12f * f.energy * k);
            float rot = std::sin (t * 0.5f + e) * 0.06f * e;
            gi.setColour (p.fgA ((0.4f - e * 0.06f) * (0.6f + f.mid)));
            for (float side : { 1.0f, -1.0f })
            {
                Path a = fw, b = hw;
                a.applyTransform (place (side, sc * grow, rot)); b.applyTransform (place (side, sc * grow, rot));
                gi.strokePath (a, PathStrokeType (px (r, 1.4f))); gi.strokePath (b, PathStrokeType (px (r, 1.4f)));
            }
        }

        // the wings
        for (float side : { 1.0f, -1.0f })
        {
            const auto tf = place (side, sc, 0);
            for (const Path* src : { &fw, &hw })
            {
                Path w = *src; w.applyTransform (tf);
                gi.setColour (p.fgA (0.05f + 0.1f * f.treble * k));
                gi.fillPath (w);
                // contour bands flowing out of the body, like scale patterns that won't sit still
                for (int band_ = 0; band_ < 5; ++band_)
                {
                    float q = std::fmod (t * (0.25f + 0.5f * f.mid * k) + band_ / 5.0f, 1.0f);
                    Path cb = *src;
                    cb.applyTransform (juce::AffineTransform::scale (q, q).translated (6 * (1 - q), 0));
                    cb.applyTransform (tf);
                    gi.setColour (p.fgA (0.5f * std::sin (q * kPi)));
                    gi.strokePath (cb, PathStrokeType (px (r, 1.2f)));
                }
                // veins from the root to the edge
                const float len = src->getLength();
                for (int v = 0; v < 9; ++v)
                {
                    auto edge = src->getPointAlongPath (len * (0.12f + v * 0.075f));
                    float wob = (noise (v * 1.7f, t * 1.5f, side) - 0.5f) * 30 * f.treble * k;
                    Path vein;
                    vein.startNewSubPath (8, src == &fw ? -4.0f : 12.0f);
                    vein.quadraticTo (edge.x * 0.5f + wob, edge.y * 0.5f - 10, edge.x, edge.y);
                    vein.applyTransform (tf);
                    gi.setColour (p.fgA (0.35f));
                    gi.strokePath (vein, PathStrokeType (px (r, 1.0f)));
                }
                gi.setColour (p.fg);
                gi.strokePath (w, PathStrokeType (lw, PathStrokeType::curved, PathStrokeType::rounded));
            }
            // eye spots on the hindwings, swelling with the mids; a small ring on each forewing
            auto eye = Point<float> (140, 92).transformedBy (tf);
            for (int ring = 0; ring < 4; ++ring)
            {
                float rr = (10 + ring * 10) * sc * (1 + f.mid * 0.8f * k) * (ring == 3 ? 1 + 0.2f * punch : 1);
                gi.setColour (p.fgA (ring == 0 ? 0.9f : 0.7f - ring * 0.12f));
                if (ring == 0) gi.fillEllipse (eye.x - rr * open, eye.y - rr, rr * 2 * open, rr * 2);
                else gi.drawEllipse (eye.x - rr * open, eye.y - rr, rr * 2 * open, rr * 2, px (r, 1.6f));
            }
            auto spot = Point<float> (215, -100).transformedBy (tf);
            float sr = 16 * sc * (1 + f.treble * k);
            gi.setColour (p.fgA (0.8f)); gi.drawEllipse (spot.x - sr * open, spot.y - sr, sr * 2 * open, sr * 2, px (r, 1.4f));

            // dust shaken off the wing edges
            int spawn = (int) (f.treble * k * 5 + punch * 10);
            for (int i = 0; i < spawn && dust.size() < 900; ++i)
            {
                const Path& src = rnd.nextBool() ? fw : hw;
                auto pt = src.getPointAlongPath (src.getLength() * rnd.nextFloat()).transformedBy (tf);
                float dx = pt.x - c.x, dy = pt.y - c.y, d = juce::jmax (1.0f, std::sqrt (dx * dx + dy * dy));
                float sp = (40 + rnd.nextFloat() * 160) * u;
                dust.push_back ({ pt.x, pt.y, dx / d * sp, dy / d * sp - 20 * u, 1.0f, (1 + rnd.nextFloat() * 2.5f) * u });
            }
        }

        // body: thorax, segmented abdomen, head and feathered antennae
        {
            auto bt = juce::AffineTransform::scale (sc, sc).rotated (tilt).translated (c.x, c.y);
            Path body;
            body.addEllipse (-24, -62, 48, 72);    // thorax
            body.addEllipse (-17, 2, 34, 132);     // abdomen
            body.addEllipse (-15, -86, 30, 28);    // head
            body.applyTransform (bt);
            gi.setColour (p.bg); gi.fillPath (body);
            gi.setColour (p.fgA (0.22f + 0.2f * f.bass)); gi.fillPath (body);
            gi.setColour (p.fg); gi.strokePath (body, PathStrokeType (lw));
            // fur on the thorax, bristling with the highs
            for (int h = 0; h < 40; ++h)
            {
                float a = h * kTau / 40, len = 8 + 10 * hash (h, 7) + 8 * f.treble * k;
                Point<float> base (std::cos (a) * 22, -26 + std::sin (a) * 33);
                Path fur; fur.startNewSubPath (base); fur.lineTo (base + Point<float> (std::cos (a), std::sin (a)) * len);
                fur.applyTransform (bt);
                gi.setColour (p.fgA (0.55f)); gi.strokePath (fur, PathStrokeType (px (r, 1.0f)));
            }
            for (int seg = 1; seg < 8; ++seg)
            {
                float y = 4 + seg * 16.0f, hw2 = 16 * std::sqrt (juce::jmax (0.0f, 1 - std::pow ((y - 68) / 66, 2.0f)));
                Path line; line.startNewSubPath (-hw2, y); line.lineTo (hw2, y + 3); line.applyTransform (bt);
                gi.setColour (p.fgA (0.6f)); gi.strokePath (line, PathStrokeType (px (r, 1.2f)));
            }
            for (float side : { 1.0f, -1.0f })
            {
                float twitch = std::sin (t * 7 + side) * 0.08f * f.treble * k + punch * 0.1f;
                Path ant;
                ant.startNewSubPath (side * 6, -82);
                ant.cubicTo (side * 30, -160, side * (80 + twitch * 300), -215, side * (135 + twitch * 300), -225);
                const float al = ant.getLength();
                for (int fth = 1; fth < 14; ++fth)
                {
                    auto a = ant.getPointAlongPath (al * fth / 14.0f);
                    auto b = ant.getPointAlongPath (juce::jmin (al, al * fth / 14.0f + 2));
                    auto n = Point<float> (-(b.y - a.y), b.x - a.x);
                    float nl = juce::jmax (0.01f, n.getDistanceFromOrigin());
                    float fl = 18 * (1 - fth / 16.0f);
                    ant.startNewSubPath (a); ant.lineTo (a + n * (fl / nl));
                    ant.startNewSubPath (a); ant.lineTo (a - n * (fl / nl));
                }
                ant.applyTransform (bt);
                gi.setColour (p.fgA (0.9f)); gi.strokePath (ant, PathStrokeType (px (r, 1.8f)));
            }
        }

        // dust: drifts out, swirls a little, fades
        for (auto& m : dust)
        {
            float sw = (noise (m.x * 0.004f, m.y * 0.004f, t * 0.3f) - 0.5f) * 120 * u;
            m.x += (m.vx + sw) * dt; m.y += (m.vy + 30 * u) * dt;
            m.life -= dt * 0.45f;
            gi.setColour (p.fgA (m.life * 0.9f));
            gi.fillEllipse (m.x - m.size, m.y - m.size, m.size * 2, m.size * 2);
        }
        dust.erase (std::remove_if (dust.begin(), dust.end(), [] (const Mote& m) { return m.life <= 0; }), dust.end());

        tr.blit (g, r);
    }
};

std::vector<std::unique_ptr<Viz>> makeAll()
{
    std::vector<std::unique_ptr<Viz>> v;
    v.push_back (std::make_unique<Ridges>());
    v.push_back (std::make_unique<Scope>());
    v.push_back (std::make_unique<Lissajous>());
    v.push_back (std::make_unique<Bars3D>());
    v.push_back (std::make_unique<Radial>());
    v.push_back (std::make_unique<PulseRings>());
    v.push_back (std::make_unique<Starfield>());
    v.push_back (std::make_unique<Burst>());
    v.push_back (std::make_unique<Tunnel>());
    v.push_back (std::make_unique<Terrain>());
    v.push_back (std::make_unique<Sphere>());
    v.push_back (std::make_unique<Cube>());
    v.push_back (std::make_unique<PolarWave>());
    v.push_back (std::make_unique<Spectrogram>());
    v.push_back (std::make_unique<CRT>());
    v.push_back (std::make_unique<MirrorBars>());
    v.push_back (std::make_unique<DotMatrix>());
    v.push_back (std::make_unique<Rain>());
    v.push_back (std::make_unique<Flow>());
    v.push_back (std::make_unique<Kaleido>());
    v.push_back (std::make_unique<Orbits>());
    v.push_back (std::make_unique<Strings>());
    v.push_back (std::make_unique<Seismo>());
    v.push_back (std::make_unique<Halftone>());
    v.push_back (std::make_unique<Glitch>());
    v.push_back (std::make_unique<VHS>());
    v.push_back (std::make_unique<Constellation>());
    v.push_back (std::make_unique<Ripples>());
    v.push_back (std::make_unique<Helix>());
    v.push_back (std::make_unique<Mandala>());
    v.push_back (std::make_unique<Lightning>());
    v.push_back (std::make_unique<Vortex>());
    v.push_back (std::make_unique<LedStack>());
    v.push_back (std::make_unique<Topo>());
    v.push_back (std::make_unique<Mesh>());
    v.push_back (std::make_unique<Bloom>());
    v.push_back (std::make_unique<Barcode>());
    v.push_back (std::make_unique<BandLines>());
    v.push_back (std::make_unique<City>());
    v.push_back (std::make_unique<Dust>());
    v.push_back (std::make_unique<Heartbeat>());
    v.push_back (std::make_unique<Rings3D>());
    v.push_back (std::make_unique<Typewriter>());
    v.push_back (std::make_unique<Pendulum>());
    v.push_back (std::make_unique<Smoke>());
    v.push_back (std::make_unique<Prism>());
    v.push_back (std::make_unique<Clockwork>());
    v.push_back (std::make_unique<Aurora>());
    v.push_back (std::make_unique<Moth>());
    return v;
}
}
