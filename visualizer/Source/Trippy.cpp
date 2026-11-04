// Styles 51-80: fractals, feedback, attractors and other trippy things, driven by the 32 ranges and the sound's character.
// Range groups used here (of 32): 0-2 sub, 3-6 bass, 7-11 low mids, 12-17 mids, 18-21 upper mids, 22-25 presence, 26-28 highs, 29-31 air.
#include "Visualizers.h"
#include <complex>

namespace bcv
{
namespace
{
using juce::Graphics; using juce::Rectangle; using juce::Path; using juce::PathStrokeType; using juce::Point; using juce::Colour;
constexpr float kPi = juce::MathConstants<float>::pi;
constexpr float kTau = juce::MathConstants<float>::twoPi;

inline float hashf (int x, int y, int z = 0)
{
    uint32_t h = (uint32_t) x * 374761393u + (uint32_t) y * 668265263u + (uint32_t) z * 2147483647u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (float) ((h ^ (h >> 16)) & 0xffffff) / (float) 0xffffff;
}
inline float sstep (float t) { return t * t * (3 - 2 * t); }
inline float vnoise (float x, float y, float z = 0)
{
    int xi = (int) std::floor (x), yi = (int) std::floor (y), zi = (int) std::floor (z);
    float xf = x - xi, yf = y - yi, zf = z - zi;
    float u = sstep (xf), v = sstep (yf), w = sstep (zf);
    auto L = [] (float a, float b, float t) { return a + (b - a) * t; };
    float a = L (L (hashf (xi, yi, zi), hashf (xi + 1, yi, zi), u), L (hashf (xi, yi + 1, zi), hashf (xi + 1, yi + 1, zi), u), v);
    float b = L (L (hashf (xi, yi, zi + 1), hashf (xi + 1, yi, zi + 1), u), L (hashf (xi, yi + 1, zi + 1), hashf (xi + 1, yi + 1, zi + 1), u), v);
    return L (a, b, w);
}
inline float fbm (float x, float y, float z, int oct = 3)
{
    float s = 0, a = 0.5f;
    for (int i = 0; i < oct; ++i) { s += a * vnoise (x, y, z); x *= 2.03f; y *= 2.03f; z *= 1.7f; a *= 0.5f; }
    return s;
}
inline float zl (const Frame& f, int a, int b) { float s = 0; for (int i = a; i <= b; ++i) s += f.zones[(size_t) i]; return s / (float) (b - a + 1); }
inline float zh (const Frame& f, int a, int b) { float s = 0; for (int i = a; i <= b; ++i) s = juce::jmax (s, f.zoneHit[(size_t) i]); return s; }
inline float clamp01 (float v) { return v > 0 ? (v < 1 ? v : 1) : 0; }   // NaN comes out as 0
inline float frac (float v) { return v - std::floor (v); }
inline float pxw (Rectangle<float> r, float w) { return w * r.getHeight() / 1080.0f; }
inline float follow (float cur, float target, float rate, float dt) { return cur + (target - cur) * juce::jmin (1.0f, dt * rate); }

/** Styles computed per pixel at a lower resolution, then scaled up smoothly. */
struct PixelViz : Viz
{
    juce::Image img;
    int W = 0, H = 0, divisor = 3, maxW = 640;
    void ensure (Rectangle<float> r)
    {
        int w = juce::jlimit (96, maxW, (int) (r.getWidth() / divisor));
        int h = juce::jmax (54, (int) std::round (w * r.getHeight() / juce::jmax (1.0f, r.getWidth())));
        if (img.getWidth() != w || img.getHeight() != h) img = juce::Image (juce::Image::RGB, w, h, true, juce::SoftwareImageType());
        W = w; H = h;
    }
    /** fn (nx, ny) -> 0..1 brightness; nx, ny centred, y from -1 to 1 */
    template <class F> void shade (const Palette& p, F fn)
    {
        juce::uint8 lr[256], lg[256], lb[256];
        for (int i = 0; i < 256; ++i) { auto c = p.ink (i / 255.0f); lr[i] = c.getRed(); lg[i] = c.getGreen(); lb[i] = c.getBlue(); }
        juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);
        const float hh = H * 0.5f, hw = W * 0.5f;
        for (int y = 0; y < H; ++y)
        {
            auto* line = bd.getLinePointer (y);
            const float ny = (y - hh) / hh;
            for (int x = 0; x < W; ++x)
            {
                const float nx = (x - hw) / hh;
                const int v = (int) (clamp01 (fn (nx, ny)) * 255.0f);
                auto* px = reinterpret_cast<juce::PixelRGB*> (line + (size_t) x * (size_t) bd.pixelStride);
                px->setARGB (255, lr[v], lg[v], lb[v]);
            }
        }
    }
    void blit (Graphics& g, Rectangle<float> r)
    {
        g.setImageResamplingQuality (Graphics::mediumResamplingQuality);
        g.drawImage (img, r);
    }
    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        ensure (r);
        render (f, p, k);
        blit (g, r);
    }
    virtual void render (const Frame& f, const Palette& p, float k) = 0;
};

/** A low-resolution density buffer for attractors and flames, shown with a log curve. */
struct DensityViz : PixelViz
{
    std::vector<float> dens;
    void plot (float x, float y, float amt)   // x, y in shade space
    {
        const float hh = H * 0.5f;
        int ix = (int) (x * hh + W * 0.5f), iy = (int) (y * hh + hh);
        if (ix >= 0 && iy >= 0 && ix < W && iy < H) dens[(size_t) iy * (size_t) W + (size_t) ix] += amt;
    }
    void prepare (float decay)
    {
        if (dens.size() != (size_t) W * (size_t) H) dens.assign ((size_t) W * (size_t) H, 0.0f);
        for (auto& d : dens) d *= decay;
    }
    void show (const Palette& p, float gain)
    {
        // normalise to the typical lit pixel rather than the hottest one, so thin curves still show
        double sum = 0; int lit = 0;
        for (auto d : dens) if (d > 0.01f) { sum += d; ++lit; }
        const float typical = lit > 0 ? (float) (sum / lit) : 1.0f;
        const float norm = 1.0f / std::log1p (typical * gain * 3);
        juce::uint8 lr[256], lg[256], lb[256];
        for (int i = 0; i < 256; ++i) { auto c = p.ink (i / 255.0f); lr[i] = c.getRed(); lg[i] = c.getGreen(); lb[i] = c.getBlue(); }
        juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < H; ++y)
        {
            auto* line = bd.getLinePointer (y);
            for (int x = 0; x < W; ++x)
            {
                const int v = (int) (std::sqrt (clamp01 (std::log1p (dens[(size_t) y * (size_t) W + (size_t) x] * gain) * norm)) * 255.0f);
                reinterpret_cast<juce::PixelRGB*> (line + (size_t) x * (size_t) bd.pixelStride)->setARGB (255, lr[v], lg[v], lb[v]);
            }
        }
    }
};

/** A persistent canvas for vector styles that leave trails. */
struct Canvas
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
};

// ============================================================ 51-66: per pixel

struct JuliaBloom : PixelViz
{
    float ang = 0, rot = 0;
    JuliaBloom() { divisor = 3; maxW = 720; }
    juce::String name() const override { return "Julia Bloom"; }
    juce::String blurb() const override { return "A Julia set that blooms and folds: the low mids steer its shape, the sub breathes it, mid hits twist it."; }
    void render (const Frame& f, const Palette& p, float k) override
    {
        const float dt = (float) f.dt;
        ang += dt * (0.06f + 0.5f * zl (f, 7, 11) * k);
        rot += dt * (0.05f + 0.2f * f.drone);
        // c walks the edge of the Mandelbrot cardioid, where the Julia sets are richest
        const float a = ang + 0.4f * zh (f, 12, 17) * k, edge = 1.01f + 0.025f * std::sin (ang * 2.3f) - 0.03f * f.bass * k;
        const float cr = (0.5f * std::cos (a) - 0.25f * std::cos (2 * a)) * edge, ci = (0.5f * std::sin (a) - 0.25f * std::sin (2 * a)) * edge;
        const float sc = 2.3f / (1 + 0.3f * zl (f, 0, 2) * k + 0.15f * f.beat);
        const float c = std::cos (rot), s = std::sin (rot);
        const float glow = 0.6f + 0.4f * zl (f, 22, 28);
        shade (p, [&] (float x, float y) {
            float zx = (x * c - y * s) * sc, zy = (x * s + y * c) * sc;
            int i = 0;
            float trap = 1e9f;
            for (; i < 48 && zx * zx + zy * zy < 16; ++i) { float t = zx * zx - zy * zy + cr; zy = 2 * zx * zy + ci; zx = t; trap = juce::jmin (trap, std::abs (zx * zy)); }
            if (i == 48) return 0.6f * std::exp (-trap * 30);   // inside: orbit-trap filigree instead of a flat hole
            float sm = i + 1 - std::log2 (std::log2 (zx * zx + zy * zy + 1e-6f) * 0.5f + 1e-6f);
            return std::pow (sm / 48.0f, 0.4f) * glow;
        });
    }
};

struct MandelDive : PixelViz
{
    double depth = 0;
    MandelDive() { divisor = 3; maxW = 720; }
    juce::String name() const override { return "Mandelbrot Dive"; }
    juce::String blurb() const override { return "An endless dive into the Mandelbrot set. Loudness sets the speed; the bands ripple with the bass."; }
    void reset() override { depth = 0; }
    void render (const Frame& f, const Palette& p, float k) override
    {
        depth += f.dt * (0.12 + 0.6 * f.energy * k + 0.4 * f.drop);
        if (depth > 15) depth = 0;
        const double cx = -0.7436438870371587, cy = 0.1318259042053120, sc = 2.2 * std::exp (-depth);
        const int maxIt = juce::jmin (220, 60 + (int) (depth * 10));
        const float ph = (float) f.t * 2 + f.bass * 3 * k;
        shade (p, [&] (float x, float y) {
            double zx = 0, zy = 0, ax = cx + x * sc, ay = cy + y * sc;
            int i = 0;
            for (; i < maxIt && zx * zx + zy * zy < 16; ++i) { double t = zx * zx - zy * zy + ax; zy = 2 * zx * zy + ay; zx = t; }
            if (i == maxIt) return 0.0f;
            float sm = (float) (i + 1 - std::log2 (std::log2 (zx * zx + zy * zy) * 0.5));
            return (0.55f + 0.45f * std::sin (sm * 0.3f - ph)) * juce::jmin (1.0f, sm / 12.0f);
        });
    }
};

struct BurningShip : PixelViz
{
    BurningShip() { divisor = 3; maxW = 720; }
    juce::String name() const override { return "Burning Ship"; }
    juce::String blurb() const override { return "The Burning Ship fractal, drifting and zooming with the sub; presence hits jolt the view."; }
    void render (const Frame& f, const Palette& p, float k) override
    {
        const float t = (float) f.t;
        const float sc = 0.09f * (1.3f + 0.6f * std::sin (t * 0.07f)) / (1 + 0.4f * zl (f, 0, 2) * k);
        const float cx = -1.762f + 0.01f * std::sin (t * 0.13f) + 0.004f * zh (f, 22, 25), cy = -0.028f + 0.008f * std::cos (t * 0.11f);
        shade (p, [&] (float x, float y) {
            float zx = 0, zy = 0, ax = cx + x * sc, ay = cy + y * sc;
            int i = 0;
            for (; i < 56 && zx * zx + zy * zy < 16; ++i) { float tx = zx * zx - zy * zy + ax; zy = std::abs (2 * zx * zy) + ay; zx = std::abs (tx); }
            if (i == 56) return 0.05f;
            return std::pow (i / 56.0f, 0.5f) * (0.7f + 0.3f * zl (f, 12, 21));
        });
    }
};

struct Plasma : PixelViz
{
    float t = 0;
    juce::String name() const override { return "Plasma"; }
    juce::String blurb() const override { return "Old-school plasma made of four waves, one each for the bass, mids, upper mids and highs."; }
    void render (const Frame& f, const Palette& p, float k) override
    {
        t += (float) f.dt * (0.6f + 1.5f * f.energy * k);
        const float a = 3 + 6 * zl (f, 3, 6) * k, b = 3 + 6 * zl (f, 12, 15) * k, c = 2 + 5 * zl (f, 20, 23) * k, d = 4 + 8 * zl (f, 26, 29) * k;
        const float sharp = 1 + 2 * f.mid * k;
        shade (p, [&] (float x, float y) {
            float v = std::sin (x * a + t) + std::sin (y * b - t * 1.3f) + std::sin ((x + y) * c + t * 0.7f) + std::sin (std::sqrt (x * x + y * y) * d - t * 2);
            return 0.5f + 0.5f * std::sin (v * kPi * sharp * 0.5f);
        });
    }
};

struct DomainWarp : PixelViz
{
    float t = 0;
    DomainWarp() { divisor = 5; }
    juce::String name() const override { return "Liquid Marble"; }
    juce::String blurb() const override { return "Noise warped through itself twice, like oil and marble. The mids stir it, the low mids twist it deeper."; }
    void render (const Frame& f, const Palette& p, float k) override
    {
        t += (float) f.dt * (0.15f + 0.5f * f.mid * k);
        const float warp = 2.5f + 3.0f * zl (f, 7, 11) * k;
        shade (p, [&] (float x, float y) {
            float qx = fbm (x * 1.4f + t * 0.3f, y * 1.4f, t * 0.2f), qy = fbm (x * 1.4f + 5.2f, y * 1.4f + 1.3f, t * 0.2f);
            float v = fbm (x * 1.4f + warp * qx, y * 1.4f + warp * qy, t * 0.3f);
            return std::pow (v * 1.25f, 2.2f);
        });
    }
};

struct Moire : PixelViz
{
    float t = 0;
    juce::String name() const override { return "Moire"; }
    juce::String blurb() const override { return "Three sets of rings interfering. The bass, mids and highs each move one centre; the upper mids tighten the rings."; }
    void render (const Frame& f, const Palette& p, float k) override
    {
        t += (float) f.dt;
        const float fr = 30 + 70 * zl (f, 18, 21) * k;
        const Point<float> c1 (0.6f * std::sin (t * 0.3f) - 0.3f * zl (f, 3, 6) * k, 0.4f * std::cos (t * 0.4f));
        const Point<float> c2 (-0.6f * std::sin (t * 0.25f + 1), -0.4f * std::cos (t * 0.35f) + 0.3f * zl (f, 12, 17) * k);
        const Point<float> c3 (0.4f * std::cos (t * 0.2f) + 0.3f * zl (f, 26, 29) * k, 0.5f * std::sin (t * 0.3f + 2));
        shade (p, [&] (float x, float y) {
            float d1 = std::hypot (x - c1.x, y - c1.y), d2 = std::hypot (x - c2.x, y - c2.y), d3 = std::hypot (x - c3.x, y - c3.y);
            float v = std::sin (d1 * fr) + std::sin (d2 * fr) + std::sin (d3 * fr * 0.8f);
            return 0.5f + 0.5f * std::tanh (v * 3);
        });
    }
};

struct ReactionDiffusion : PixelViz
{
    static constexpr int GW = 200, GH = 112;
    std::vector<float> A, B, A2, B2;
    juce::Random rnd { 5 };
    ReactionDiffusion() { divisor = 1; maxW = GW; }
    juce::String name() const override { return "Coral Growth"; }
    juce::String blurb() const override { return "Reaction-diffusion: patterns grow like coral or skin. Every range that jumps seeds new growth at its own height."; }
    void reset() override { A.clear(); }
    void seed (int cx, int cy, int rad)
    {
        for (int y = -rad; y <= rad; ++y) for (int x = -rad; x <= rad; ++x)
            if (x * x + y * y <= rad * rad) { int ix = (cx + x + GW) % GW, iy = (cy + y + GH) % GH; B[(size_t) (iy * GW + ix)] = 1; }
    }
    void render (const Frame& f, const Palette& p, float k) override
    {
        if (A.empty())
        {
            A.assign (GW * GH, 1); B.assign (GW * GH, 0); A2 = A; B2 = B;
            for (int i = 0; i < 18; ++i) seed (rnd.nextInt (GW), rnd.nextInt (GH), 3);
        }
        for (int z = 0; z < kZones; ++z) if (f.zoneOnsets & (1u << z)) seed (rnd.nextInt (GW), GH - 1 - z * GH / kZones, 2 + (int) (2 * k));
        const float F = 0.030f + 0.025f * zl (f, 12, 17), K = 0.058f + 0.006f * zl (f, 0, 5);
        const int steps = 6 + (int) (10 * f.energy * k);
        for (int s = 0; s < steps; ++s)
        {
            for (int y = 0; y < GH; ++y)
                for (int x = 0; x < GW; ++x)
                {
                    const int i = y * GW + x, l = y * GW + (x + GW - 1) % GW, r = y * GW + (x + 1) % GW, u = ((y + GH - 1) % GH) * GW + x, d = ((y + 1) % GH) * GW + x;
                    const float a = A[(size_t) i], b = B[(size_t) i];
                    const float la = A[(size_t) l] + A[(size_t) r] + A[(size_t) u] + A[(size_t) d] - 4 * a, lb = B[(size_t) l] + B[(size_t) r] + B[(size_t) u] + B[(size_t) d] - 4 * b;
                    const float abb = a * b * b;
                    A2[(size_t) i] = clamp01 (a + (1.0f * la * 0.2f - abb + F * (1 - a)));
                    B2[(size_t) i] = clamp01 (b + (0.5f * lb * 0.2f + abb - (K + F) * b));
                }
            std::swap (A, A2); std::swap (B, B2);
        }
        if (img.getWidth() != GW) img = juce::Image (juce::Image::RGB, GW, GH, true, juce::SoftwareImageType());
        W = GW; H = GH;
        juce::uint8 lr[256], lg[256], lb[256];
        for (int i = 0; i < 256; ++i) { auto c = p.ink (i / 255.0f); lr[i] = c.getRed(); lg[i] = c.getGreen(); lb[i] = c.getBlue(); }
        juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < GH; ++y)
            for (int x = 0; x < GW; ++x)
            {
                int v = (int) (clamp01 (B[(size_t) (y * GW + x)] * 3.5f) * 255);
                reinterpret_cast<juce::PixelRGB*> (bd.getPixelPointer (x, y))->setARGB (255, lr[v], lg[v], lb[v]);
            }
    }
    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        render (f, p, k);
        g.setImageResamplingQuality (Graphics::highResamplingQuality);
        g.drawImage (img, r);
    }
};

struct Feedback : PixelViz
{
    std::vector<float> buf, tmp;
    float rot = 0;
    Feedback() { divisor = 4; }
    juce::String name() const override { return "Feedback Loop"; }
    juce::String blurb() const override { return "Video feedback: each frame falls back into itself, turning and zooming. The waveform feeds it; tails stretch the echo."; }
    void reset() override { buf.clear(); }
    void render (const Frame& f, const Palette& p, float k) override
    {
        const size_t N = (size_t) W * (size_t) H;
        if (buf.size() != N) { buf.assign (N, 0); tmp.assign (N, 0); }
        rot = follow (rot, 0.02f + 0.08f * zl (f, 7, 11) * k * (f.section % 2 ? 1 : -1) + 0.05f * f.pitchMove, 3, (float) f.dt);
        const float zoom = 0.955f - 0.03f * zl (f, 0, 2) * k - 0.03f * f.drop, decay = 0.9f + 0.08f * f.tail;
        const float c = std::cos (rot) * zoom, s = std::sin (rot) * zoom, hw = W * 0.5f, hh = H * 0.5f;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x)
            {
                float dx = x - hw, dy = y - hh, sx = dx * c - dy * s + hw, sy = dx * s + dy * c + hh;
                int ix = (int) sx, iy = (int) sy;
                float v = 0;
                if (ix >= 0 && iy >= 0 && ix < W - 1 && iy < H - 1)
                {
                    float fx = sx - ix, fy = sy - iy;
                    const float* row = buf.data() + (size_t) iy * (size_t) W;
                    v = (row[ix] * (1 - fx) + row[ix + 1] * fx) * (1 - fy) + (row[ix + W] * (1 - fx) + row[ix + W + 1] * fx) * fy;
                }
                tmp[(size_t) y * (size_t) W + (size_t) x] = v * decay;
            }
        std::swap (buf, tmp);
        // feed it: the waveform drawn as a ring
        const float rad = hh * (0.35f + 0.25f * f.bass * k);
        for (int i = 0; i < 720; ++i)
        {
            float a = i * kTau / 720, w = f.L[(size_t) (i * (kWave - 1) / 719)] * hh * 0.5f * k;
            int x = (int) (hw + std::cos (a) * (rad + w)), y = (int) (hh + std::sin (a) * (rad + w));
            if (x >= 0 && y >= 0 && x < W && y < H) buf[(size_t) y * (size_t) W + (size_t) x] = 1;
        }
        if (f.onset) for (int i = 0; i < 360; ++i) { float a = i * kTau / 360; int x = (int) (hw + std::cos (a) * hh * 0.9f), y = (int) (hh + std::sin (a) * hh * 0.9f); if (x >= 0 && y >= 0 && x < W && y < H) buf[(size_t) y * (size_t) W + (size_t) x] = 1; }
        juce::uint8 lr[256], lg[256], lb[256];
        for (int i = 0; i < 256; ++i) { auto cc = p.ink (i / 255.0f); lr[i] = cc.getRed(); lg[i] = cc.getGreen(); lb[i] = cc.getBlue(); }
        juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x)
            {
                int v = (int) (clamp01 (buf[(size_t) y * (size_t) W + (size_t) x]) * 255);
                reinterpret_cast<juce::PixelRGB*> (bd.getPixelPointer (x, y))->setARGB (255, lr[v], lg[v], lb[v]);
            }
    }
};

struct KaleidoNoise : PixelViz
{
    float t = 0;
    KaleidoNoise() { divisor = 4; }
    juce::String name() const override { return "Kaleido Smoke"; }
    juce::String blurb() const override { return "Smoke folded through a kaleidoscope. Each ring outward follows a higher range; sections change the number of mirrors."; }
    void render (const Frame& f, const Palette& p, float k) override
    {
        t += (float) f.dt * (0.4f + f.energy * k);
        const int seg = 6 + 2 * (f.section % 4);
        const float wedge = kTau / seg;
        shade (p, [&] (float x, float y) {
            float r = std::sqrt (x * x + y * y), a = std::atan2 (y, x) + t * 0.05f;
            a = std::fmod (std::abs (a), wedge); if (a > wedge * 0.5f) a = wedge - a;
            float n = fbm (r * 3 - t, a * 4, t * 0.2f);
            float zone = f.zones[(size_t) juce::jlimit (0, kZones - 1, (int) (r * 24))];
            return std::pow (frac (n * 3 + t * 0.1f), 2.0f) * (0.4f + 0.8f * zone * k);
        });
    }
};

struct FoldCrystal : PixelViz
{
    float t = 0;
    FoldCrystal() { divisor = 3; maxW = 720; }
    juce::String name() const override { return "Fold Crystal"; }
    juce::String blurb() const override { return "A kaleidoscopic fold fractal that grows crystals. Mids and upper mids set the folds, the low mids twist every layer."; }
    void render (const Frame& f, const Palette& p, float k) override
    {
        t += (float) f.dt * (0.2f + 0.6f * f.energy * k);
        const float ox = 0.5f + 0.3f * zl (f, 12, 15) * k, oy = 0.3f + 0.3f * zl (f, 18, 21) * k, ang = t * 0.3f + 0.6f * zl (f, 7, 11) * k;
        const float ca = std::cos (ang), sa = std::sin (ang);
        shade (p, [&] (float x, float y) {
            float px = x * 1.3f, py = y * 1.3f, sc = 1, dmin = 1e9f;
            for (int i = 0; i < 8; ++i)
            {
                px = std::abs (px) - ox; py = std::abs (py) - oy;
                float nx = px * ca - py * sa; py = px * sa + py * ca; px = nx;
                px *= 1.35f; py *= 1.35f; sc *= 1.35f;
                dmin = juce::jmin (dmin, std::sqrt (px * px + py * py) / sc);
            }
            return std::exp (-dmin * 9) + 0.35f * std::exp (-dmin * 40);
        });
    }
};

struct Kaliset : PixelViz
{
    float t = 0;
    Kaliset() { divisor = 3; maxW = 720; }
    juce::String name() const override { return "Kaliset"; }
    juce::String blurb() const override { return "The Kali fractal: endless filaments and cells. The sub and bass push it apart, the highs bring out the fine threads."; }
    void render (const Frame& f, const Palette& p, float k) override
    {
        t += (float) f.dt * (0.05f + 0.15f * f.energy * k);
        const float cx = -0.62f - 0.12f * zl (f, 0, 6) * k + 0.05f * std::sin (t), cy = -0.55f - 0.1f * zl (f, 12, 17) * k + 0.05f * std::cos (t * 1.3f);
        const float fine = 0.8f + 1.2f * zl (f, 26, 31) * k;
        shade (p, [&] (float x, float y) {
            float px = x * 0.7f + 0.2f * std::sin (t), py = y * 0.7f + 0.2f * std::cos (t * 0.8f), acc = 0, last = 0;
            for (int i = 0; i < 14; ++i)
            {
                float d = px * px + py * py + 1e-4f;
                px = std::abs (px) / d + cx; py = std::abs (py) / d + cy;
                float l = std::sqrt (px * px + py * py);
                acc += std::abs (l - last); last = l;
            }
            return std::pow (acc / 14.0f * 0.35f, fine);
        });
    }
};

struct HyperTunnel : PixelViz
{
    float t = 0, twist = 0;
    HyperTunnel() { divisor = 3; }
    juce::String name() const override { return "Hyper Tunnel"; }
    juce::String blurb() const override { return "A checkered tunnel rushing at you. Bass hits bulge it, the low mids twist it, the number of panels follows the busyness."; }
    void render (const Frame& f, const Palette& p, float k) override
    {
        t += (float) f.dt * (0.6f + 2.5f * f.energy * k + 2 * f.drop);
        twist = follow (twist, (zl (f, 7, 11) - 0.3f) * 3 * k, 2, (float) f.dt);
        const float panels = 6 + std::floor (f.density * 10), bulge = 1 - 0.25f * zh (f, 3, 6) * k;
        shade (p, [&] (float x, float y) {
            float r = std::sqrt (x * x + y * y) * bulge + 1e-3f, a = std::atan2 (y, x);
            float u = 0.35f / r + t, v = a / kPi * panels + twist * u;
            float ch = std::sin (u * 10) * std::sin (v * kPi);
            return (0.5f + 0.5f * std::tanh (ch * 6)) * juce::jmin (1.0f, r * 1.8f);
        });
    }
};

struct Metaballs : PixelViz
{
    float t = 0;
    Metaballs() { divisor = 4; }
    juce::String name() const override { return "Lava"; }
    juce::String blurb() const override { return "32 blobs, one for each range, melting into each other. Each one swells with its range and flares when it jumps."; }
    void render (const Frame& f, const Palette& p, float k) override
    {
        t += (float) f.dt * (0.3f + 0.6f * f.energy * k);
        float bx[kZones], by[kZones], br[kZones];
        for (int i = 0; i < kZones; ++i)
        {
            bx[i] = 1.4f * std::sin (t * (0.3f + hashf (i, 1) * 0.5f) + hashf (i, 2) * 6);
            by[i] = 0.8f * std::cos (t * (0.25f + hashf (i, 3) * 0.5f) + hashf (i, 4) * 6);
            float rr = 0.04f + 0.11f * f.zones[(size_t) i] * k + 0.06f * f.zoneHit[(size_t) i];
            br[i] = rr * rr;
        }
        shade (p, [&] (float x, float y) {
            float fsum = 0;
            for (int i = 0; i < kZones; ++i) { float dx = x - bx[i], dy = y - by[i]; fsum += br[i] / (dx * dx + dy * dy + 1e-4f); }
            return clamp01 ((fsum - 0.8f) * 3) * 0.85f + 0.5f * std::exp (-(fsum - 1) * (fsum - 1) * 30);
        });
    }
};

struct Cells : PixelViz
{
    float t = 0;
    Cells() { divisor = 4; }
    juce::String name() const override { return "Cells"; }
    juce::String blurb() const override { return "32 living cells, one per range. Each glows with its range and flashes when it jumps; the walls shiver with the highs."; }
    void render (const Frame& f, const Palette& p, float k) override
    {
        t += (float) f.dt * (0.15f + 0.4f * f.energy * k);
        float sx[kZones], sy[kZones];
        for (int i = 0; i < kZones; ++i)
        {
            sx[i] = 1.7f * (vnoise (i * 3.1f, t, 0) * 2 - 1);
            sy[i] = 0.95f * (vnoise (i * 3.1f, t, 5) * 2 - 1);
        }
        const float wall = 50 + 40 * zl (f, 26, 31) * k;
        shade (p, [&] (float x, float y) {
            float d1 = 1e9f, d2 = 1e9f; int id = 0;
            for (int i = 0; i < kZones; ++i)
            {
                float dx = x - sx[i], dy = y - sy[i], d = dx * dx + dy * dy;
                if (d < d1) { d2 = d1; d1 = d; id = i; } else if (d < d2) d2 = d;
            }
            float edge = std::sqrt (d2) - std::sqrt (d1);
            return std::exp (-edge * wall) + (0.15f + 0.55f * f.zones[(size_t) id] * k + 0.4f * f.zoneHit[(size_t) id]) * (1 - clamp01 (std::sqrt (d1) * 1.5f));
        });
    }
};

struct Cymatics : PixelViz
{
    float n = 2, m = 3;
    Cymatics() { divisor = 3; }
    juce::String name() const override { return "Cymatics"; }
    juce::String blurb() const override { return "Sand on a vibrating plate. The loudest range picks the pattern, so it reshapes itself as the sound moves around."; }
    void render (const Frame& f, const Palette& p, float k) override
    {
        int top = 0;
        for (int z = 1; z < kZones; ++z) if (f.zones[(size_t) z] > f.zones[(size_t) top]) top = z;
        n = follow (n, 1.0f + (float) (top % 7), 1.5f, (float) f.dt);
        m = follow (m, 2.0f + (float) ((top * 3 + f.section) % 9), 1.5f, (float) f.dt);
        const float sharp = 14 + 14 * (1 - f.bass * k);
        shade (p, [&] (float x, float y) {
            float X = x * 0.56f, Y = y;
            float v = std::sin (n * kPi * X) * std::sin (m * kPi * Y) - std::sin (m * kPi * X) * std::sin (n * kPi * Y);
            return std::exp (-std::abs (v) * sharp) * (0.7f + 0.3f * f.energy);
        });
    }
};

struct Hypno : PixelViz
{
    float t = 0, zoom = 0;
    Hypno() { divisor = 3; }
    juce::String name() const override { return "Hypno Spiral"; }
    juce::String blurb() const override { return "A log spiral that pulls you in forever. More arms when it's busy; the sub throbs the zoom."; }
    void render (const Frame& f, const Palette& p, float k) override
    {
        t += (float) f.dt * (0.4f + 1.2f * f.energy * k);
        zoom = follow (zoom, zl (f, 0, 2) * k, 8, (float) f.dt);
        const float arms = 2 + std::floor (f.density * 6), kk = 4 + 2 * zoom;
        shade (p, [&] (float x, float y) {
            float r = std::sqrt (x * x + y * y) + 1e-4f, a = std::atan2 (y, x);
            float s = std::log (r) * kk + a * arms / kTau - t;
            return std::pow (0.5f + 0.5f * std::sin (s * kTau), 3.0f) * juce::jmin (1.0f, r * 3);
        });
    }
};

// ============================================================ 67-80: vector and particles

struct FractalBloom : Viz
{
    float spin = 0;
    juce::String name() const override { return "Fractal Bloom"; }
    juce::String blurb() const override { return "Six fractal trees grown from the centre into a flower. The mids open the branches, the low mids sway them, the air lights the tips."; }
    void branch (Graphics& g, Point<float> a, float ang, float len, int depth, float spread, float sway, float tips, Rectangle<float> r, const Palette& p)
    {
        Point<float> b (a.x + std::cos (ang) * len, a.y + std::sin (ang) * len);
        g.setColour (p.fgA (0.25f + depth * 0.09f));
        g.drawLine ({ a, b }, pxw (r, 0.6f + depth * 0.35f));
        if (depth == 0) { if (tips > 0.05f) { g.setColour (p.fgA (tips)); g.fillEllipse (b.x - 2, b.y - 2, 4, 4); } return; }
        branch (g, b, ang - spread + sway, len * 0.72f, depth - 1, spread, sway, tips, r, p);
        branch (g, b, ang + spread + sway, len * 0.72f, depth - 1, spread, sway, tips, r, p);
    }
    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        spin += (float) f.dt * (0.05f + 0.3f * f.drone);
        const auto c = r.getCentre();
        const float len = r.getHeight() * 0.15f * (1 + 0.3f * zl (f, 0, 2) * k), spread = 0.3f + 0.5f * zl (f, 12, 17) * k;
        const float sway = 0.15f * std::sin ((float) f.t * 0.8f) * (0.3f + zl (f, 7, 11) * k), tips = zl (f, 26, 31) * k;
        for (int i = 0; i < 6; ++i) branch (g, c, spin + i * kTau / 6, len, 8, spread, sway, tips, r, p);
    }
};

struct Sierpinski : Viz
{
    float t = 0;
    juce::String name() const override { return "Sierpinski Pulse"; }
    juce::String blurb() const override { return "Sierpinski triangles where every level turns and pulses with its own band of ranges."; }
    void tri (Graphics& g, Point<float> c, float s, float rot, int depth, const Frame& f, float k, Rectangle<float> r, const Palette& p)
    {
        const float lv = zl (f, juce::jmin (kZones - 4, (6 - depth) * 5), juce::jmin (kZones - 1, (6 - depth) * 5 + 3));
        const float rr = rot + (6 - depth) * 0.08f * std::sin (t * 0.7f + depth) * (1 + 2 * lv * k);
        Point<float> v[3];
        for (int i = 0; i < 3; ++i) v[i] = { c.x + std::cos (rr + i * kTau / 3 - kPi / 2) * s, c.y + std::sin (rr + i * kTau / 3 - kPi / 2) * s };
        Path path; path.addTriangle (v[0], v[1], v[2]);
        g.setColour (p.fgA (0.2f + 0.7f * lv * k + 0.1f));
        g.strokePath (path, PathStrokeType (pxw (r, 0.8f + depth * 0.3f)));
        if (depth == 0) return;
        for (int i = 0; i < 3; ++i) tri (g, { (c.x + v[i].x) * 0.5f, (c.y + v[i].y) * 0.5f }, s * 0.5f * (1 + 0.08f * lv * k), rr, depth - 1, f, k, r, p);
    }
    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        t += (float) f.dt * (0.5f + f.energy * k);
        tri (g, r.getCentre().translated (0, r.getHeight() * 0.05f), r.getHeight() * 0.48f * (1 + 0.1f * zl (f, 0, 2) * k), t * 0.05f, 6, f, k, r, p);
    }
};

struct KochBloom : Viz
{
    float t = 0;
    juce::String name() const override { return "Snowflake"; }
    juce::String blurb() const override { return "Nested Koch snowflakes spinning against each other, each breathing with a different part of the sound."; }
    static void koch (Path& path, Point<float> a, Point<float> b, int depth, bool start)
    {
        if (start) path.startNewSubPath (a);
        if (depth == 0) { path.lineTo (b); return; }
        auto d = (b - a) / 3.0f;
        auto p1 = a + d, p2 = a + d * 2.0f;
        auto peak = p1 + Point<float> (d.x * 0.5f - d.y * 0.866f, d.y * 0.5f + d.x * 0.866f);
        koch (path, a, p1, depth - 1, false); koch (path, p1, peak, depth - 1, false); koch (path, peak, p2, depth - 1, false); koch (path, p2, b, depth - 1, false);
    }
    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        t += (float) f.dt;
        const auto c = r.getCentre();
        const float groups[4] = { zl (f, 0, 6), zl (f, 7, 17), zl (f, 18, 25), zl (f, 26, 31) };
        for (int layer = 0; layer < 4; ++layer)
        {
            const float s = r.getHeight() * (0.46f - layer * 0.1f) * (1 + 0.15f * groups[layer] * k), rot = t * (layer % 2 ? -0.1f : 0.13f) * (1 + layer * 0.3f);
            Path path;
            Point<float> v[3];
            for (int i = 0; i < 3; ++i) v[i] = { c.x + std::cos (rot + i * kTau / 3) * s, c.y + std::sin (rot + i * kTau / 3) * s };
            for (int i = 0; i < 3; ++i) koch (path, v[i], v[(i + 1) % 3], layer < 2 ? 4 : 3, i == 0);
            path.closeSubPath();
            g.setColour (p.fgA (0.55f + 0.45f * groups[layer] * k));
            g.strokePath (path, PathStrokeType (pxw (r, 1.6f + 1.5f * groups[layer])));
        }
    }
};

struct Dragon : Viz
{
    std::vector<int> turns;
    float spin = 0;
    juce::String name() const override { return "Dragon Fold"; }
    juce::String blurb() const override { return "The dragon curve folding and unfolding like paper. The bass bends the folds, sections flip which way it grows."; }
    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        if (turns.empty())
        {
            turns.push_back (1);
            for (int i = 0; i < 11; ++i) { std::vector<int> nx = turns; nx.push_back (1); for (int j = (int) turns.size() - 1; j >= 0; --j) nx.push_back (-turns[(size_t) j]); turns = nx; }
        }
        spin += (float) f.dt * 0.1f;
        const float fold = kPi * 0.5f * (0.55f + 0.45f * std::sin ((float) f.t * 0.25f)) + 0.35f * zl (f, 3, 6) * k * (f.section % 2 ? 1 : -1);
        std::vector<Point<float>> pts; pts.reserve (turns.size() + 2);
        float x = 0, y = 0, a = spin;
        pts.push_back ({ 0, 0 });
        for (int tn : turns) { x += std::cos (a); y += std::sin (a); pts.push_back ({ x, y }); a += tn * fold; }
        x += std::cos (a); y += std::sin (a); pts.push_back ({ x, y });
        Rectangle<float> b = Rectangle<float>::findAreaContainingPoints (pts.data(), (int) pts.size());
        const float sc = juce::jmin (r.getWidth() * 0.85f / juce::jmax (1.0f, b.getWidth()), r.getHeight() * 0.85f / juce::jmax (1.0f, b.getHeight()));
        const auto off = r.getCentre() - b.getCentre() * sc;
        const int n = (int) pts.size();
        for (int i = 1; i < n; i += 1)
        {
            float u = (float) i / n;
            g.setColour (p.fgA (0.55f + 0.45f * f.bands[(size_t) juce::jmin (kBands - 1, (int) (u * kBands))] * k));
            g.drawLine ({ pts[(size_t) i - 1] * sc + off, pts[(size_t) i] * sc + off }, pxw (r, 2.0f));
        }
    }
};

struct Lorenz : Viz
{
    std::deque<std::array<float, 3>> trail;
    double x = 0.1, y = 0, z = 0;
    float view = 0;
    juce::String name() const override { return "Strange Attractor"; }
    juce::String blurb() const override { return "The Lorenz attractor tracing its butterfly. The sub and bass push its chaos, the presence range bends its shape."; }
    void reset() override { trail.clear(); x = 0.1; y = z = 0; }
    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        const double sigma = 10, rho = 28 + 14 * zl (f, 0, 6) * k, beta = 8.0 / 3 + 1.2 * zl (f, 22, 25) * k, h = 0.005 * (1 + 2 * f.energy * k);
        for (int i = 0; i < 40; ++i)
        {
            double dx = sigma * (y - x), dy = x * (rho - z) - y, dz = x * y - beta * z;
            x += dx * h; y += dy * h; z += dz * h;
            trail.push_back ({ (float) x, (float) y, (float) z });
        }
        while (trail.size() > 4000) trail.pop_front();
        view += (float) f.dt * (0.15f + 0.3f * f.drone);
        const float cv = std::cos (view), sv = std::sin (view), sc = r.getHeight() / 70.0f;
        const auto c = r.getCentre();
        Point<float> prev; bool have = false; size_t i = 0;
        for (auto& q : trail)
        {
            float px = q[0] * cv - q[1] * sv, pz = q[2] - 25;
            Point<float> pt (c.x + px * sc, c.y - pz * sc);
            if (have) { g.setColour (p.fgA (0.15f + (float) i / trail.size() * (0.6f + 0.4f * f.mid * k))); g.drawLine ({ prev, pt }, pxw (r, 1.8f)); }
            prev = pt; have = true; ++i;
        }
    }
};

struct Clifford : DensityViz
{
    float pa = -1.4f, pb = 1.6f, pc = 1.0f, pd = 0.7f, x = 0.1f, y = 0.1f;
    Clifford() { divisor = 3; }
    juce::String name() const override { return "Clifford Silk"; }
    juce::String blurb() const override { return "A Clifford attractor drawn from 60,000 points like silk. Each quarter of the ranges bends one of its four controls."; }
    void render (const Frame& f, const Palette& p, float k) override
    {
        const float t = (float) f.t, dt = (float) f.dt;
        pa = follow (pa, -1.4f + 0.5f * std::sin (t * 0.07f) + 0.4f * zl (f, 0, 7) * k, 2, dt);
        pb = follow (pb, 1.6f + 0.5f * zl (f, 8, 15) * k, 2, dt);
        pc = follow (pc, 1.0f + 0.5f * std::sin (t * 0.05f) + 0.4f * zl (f, 16, 23) * k, 2, dt);
        pd = follow (pd, 0.7f + 0.5f * zl (f, 24, 31) * k, 2, dt);
        prepare (0.8f);
        for (int i = 0; i < 120000; ++i)
        {
            float nx = std::sin (pa * y) + pc * std::cos (pa * x), ny = std::sin (pb * x) + pd * std::cos (pb * y);
            x = nx; y = ny;
            plot (x * 0.42f, y * 0.42f, 1);
        }
        show (p, 3.0f);
    }
};

struct Flame : DensityViz
{
    float x = 0, y = 0, fx = 0, fy = 0, fs = 1;
    std::vector<Point<float>> pts;
    juce::Random rnd { 3 };
    Flame() { divisor = 3; }
    juce::String name() const override { return "Fractal Flame"; }
    juce::String blurb() const override { return "A fractal flame from four drifting maps with swirl and sine twists. Each map listens to a different part of the spectrum."; }
    void render (const Frame& f, const Palette& p, float k) override
    {
        const float t = (float) f.t;
        float m[4][6];
        for (int i = 0; i < 4; ++i)
        {
            const float lv = zl (f, i * 8, i * 8 + 7) * k;
            for (int j = 0; j < 6; ++j) m[i][j] = (vnoise (i * 7.0f + j, t * 0.05f, 0) * 2 - 1) * (j % 3 == 2 ? 0.8f : 0.9f) + (j == 0 ? 0.2f * lv : 0) + (j == 4 ? -0.2f * lv : 0);
        }
        prepare (0.5f);
        const float swirl = 0.5f + 1.5f * f.mid * k;
        pts.clear();
        float lo[2] { 1e9f, 1e9f }, hi[2] { -1e9f, -1e9f };
        for (int i = 0; i < 50000; ++i)
        {
            const int w = rnd.nextInt (4);
            float nx = m[w][0] * x + m[w][1] * y + m[w][2], ny = m[w][3] * x + m[w][4] * y + m[w][5];
            if (w % 2 == 0) { float r2 = nx * nx + ny * ny; float s = std::sin (r2 * swirl), c = std::cos (r2 * swirl); x = nx * s - ny * c; y = nx * c + ny * s; }
            else { x = std::sin (nx); y = std::sin (ny); }
            if (i > 20 && std::isfinite (x) && std::isfinite (y)) { pts.push_back ({ x, y }); lo[0] = juce::jmin (lo[0], x); lo[1] = juce::jmin (lo[1], y); hi[0] = juce::jmax (hi[0], x); hi[1] = juce::jmax (hi[1], y); }
            else if (! std::isfinite (x) || ! std::isfinite (y)) { x = y = 0.1f; }
        }
        // fit it to the frame (smoothly, so it doesn't jump)
        const float cx = (lo[0] + hi[0]) * 0.5f, cy = (lo[1] + hi[1]) * 0.5f, span = juce::jmax (0.05f, juce::jmax ((hi[0] - lo[0]) / (W / (float) H), hi[1] - lo[1]));
        fx = follow (fx, cx, 3, (float) f.dt); fy = follow (fy, cy, 3, (float) f.dt); fs = follow (fs, 1.8f / span, 3, (float) f.dt);
        for (auto& q : pts) plot ((q.x - fx) * fs, (q.y - fy) * fs, 1);
        show (p, 3.0f);
    }
};

struct Pythagoras : Viz
{
    juce::String name() const override { return "Pythagoras Tree"; }
    juce::String blurb() const override { return "A tree of squares leaning in the wind of the mids, its crown shivering with the highs."; }
    void sq (Graphics& g, Point<float> a, Point<float> b, int depth, float ang, const Frame& f, float k, Rectangle<float> r, const Palette& p)
    {
        auto d = b - a; Point<float> n (-d.y, d.x);
        Point<float> c = b + n, e = a + n;
        Path path; path.startNewSubPath (a); path.lineTo (b); path.lineTo (c); path.lineTo (e); path.closeSubPath();
        g.setColour (p.fgA (0.35f + 0.06f * (10 - depth) * (0.5f + zl (f, 26, 31) * k)));
        g.strokePath (path, PathStrokeType (pxw (r, 1.0f)));
        const float len = d.getDistanceFromOrigin();
        if (depth == 0 || len < 2) return;
        // the apex sits on the circle over the top edge (Thales), leaning by ang
        const auto top = c - e, out = n / len;
        const float ca = std::cos (ang), sa = std::sin (ang);
        const Point<float> apex = e + top * (ca * ca) + out * (len * ca * sa);
        sq (g, e, apex, depth - 1, ang, f, k, r, p);
        sq (g, apex, c, depth - 1, ang, f, k, r, p);
    }
    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        const float ang = juce::jlimit (0.2f, 1.35f, 0.785f + 0.25f * std::sin ((float) f.t * 0.4f) + 0.45f * (zl (f, 12, 17) - 0.4f) * k);
        const float s = r.getHeight() * 0.14f * (1 + 0.1f * zl (f, 0, 2) * k);
        Point<float> a (r.getCentreX() - s / 2, r.getBottom() - r.getHeight() * 0.06f), b (r.getCentreX() + s / 2, a.y);
        sq (g, b, a, 10, ang, f, k, r, p);
    }
};

struct FlowerOfLife : Viz
{
    float spin = 0;
    juce::String name() const override { return "Flower of Life"; }
    juce::String blurb() const override { return "Rings of overlapping circles turning in opposite directions; each ring outward follows a higher range."; }
    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        spin += (float) f.dt * (0.05f + 0.2f * f.drone);
        const auto c = r.getCentre();
        const float R = r.getHeight() * 0.11f * (1 + 0.15f * zl (f, 0, 2) * k);
        for (int layer = 0; layer < 2; ++layer)
        {
            const float rot = layer == 0 ? spin : -spin + kPi / 6;
            for (int q = -4; q <= 4; ++q)
                for (int rr = -4; rr <= 4; ++rr)
                {
                    if (std::abs (q + rr) > 4) continue;
                    float hx = R * (q + rr * 0.5f), hy = R * rr * 0.866f;
                    float x = hx * std::cos (rot) - hy * std::sin (rot), y = hx * std::sin (rot) + hy * std::cos (rot);
                    int ring = juce::jmax (std::abs (q), std::abs (rr), std::abs (q + rr));
                    float lv = zl (f, ring * 6, juce::jmin (kZones - 1, ring * 6 + 5)) * k;
                    float rad = R * (1 + 0.25f * lv);
                    g.setColour (p.fgA ((0.35f + 0.65f * lv) * (layer == 0 ? 1 : 0.6f)));
                    g.drawEllipse (c.x + x - rad, c.y + y - rad, rad * 2, rad * 2, pxw (r, 1.8f));
                }
        }
    }
};

struct Maurer : Viz
{
    float n = 4, d = 71, n2 = 6, d2 = 29, rot = 0;
    juce::String name() const override { return "Maurer Rose"; }
    juce::String blurb() const override { return "Two Maurer roses: string-art petals whose counts shift with the low and the high ranges."; }
    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        const float dt = (float) f.dt;
        n = follow (n, 2 + 6 * zl (f, 0, 11) * k, 0.5f, dt); d = follow (d, 29 + 70 * zl (f, 12, 21) * k, 0.3f, dt);
        n2 = follow (n2, 3 + 5 * zl (f, 22, 31) * k, 0.5f, dt); d2 = follow (d2, 19 + 50 * f.brightness, 0.3f, dt);
        rot += dt * 0.08f;
        const auto c = r.getCentre();
        for (int layer = 0; layer < 2; ++layer)
        {
            const float nn = layer == 0 ? n : n2, dd = layer == 0 ? d : d2, R = r.getHeight() * (layer == 0 ? 0.45f : 0.3f);
            Path path;
            for (int i = 0; i <= 360; ++i)
            {
                float th = i * dd * kPi / 180, rad = R * std::sin (nn * th);
                Point<float> pt (c.x + rad * std::cos (th + rot * (layer ? -1 : 1)), c.y + rad * std::sin (th + rot * (layer ? -1 : 1)));
                if (i == 0) path.startNewSubPath (pt); else path.lineTo (pt);
            }
            g.setColour (p.fgA (layer == 0 ? 0.45f : 0.7f));
            g.strokePath (path, PathStrokeType (pxw (r, 0.9f)));
        }
    }
};

struct Droste : Viz
{
    float z = 0, rotStep = 0.08f;
    juce::String name() const override { return "Droste"; }
    juce::String blurb() const override { return "Frames inside frames inside frames, zooming in forever. The low mids twist each level; sections swap squares and circles."; }
    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        const float dt = (float) f.dt;
        z += dt * (0.25f + 0.8f * f.energy * k + 0.6f * f.drop);
        rotStep = follow (rotStep, 0.04f + 0.18f * zl (f, 7, 11) * k, 2, dt);
        const auto c = r.getCentre();
        const float ph = frac (z), base = r.getHeight() * 0.62f;
        const bool circles = f.section % 2 == 1;
        for (int i = -1; i < 40; ++i)
        {
            const float lvl = i + 1 - ph, sc = std::pow (0.86f, lvl), s = base * sc;
            if (s < 2) break;
            const float rot = lvl * rotStep + (float) f.t * 0.05f;
            const float lv = f.bands[(size_t) juce::jlimit (0, kBands - 1, (int) (lvl * 3))];
            g.setColour (p.fgA (juce::jmin (1.0f, (0.5f + 0.5f * lv * k) * juce::jmin (1.0f, lvl + 1))));
            if (circles) g.drawEllipse (c.x - s, c.y - s, s * 2, s * 2, pxw (r, 1 + 2 * lv));
            else
            {
                Path sq; sq.addRectangle (-s * 1.2f, -s, s * 2.4f, s * 2);
                sq.applyTransform (juce::AffineTransform::rotation (rot).translated (c.x, c.y));
                g.strokePath (sq, PathStrokeType (pxw (r, 1 + 2 * lv)));
            }
        }
    }
};

struct ThirdEye : Viz
{
    float lid = 1, look = 0, lookY = 0, pupil = 0.3f, spin = 0;
    float small[6] {};
    juce::String name() const override { return "Third Eye"; }
    juce::String blurb() const override { return "An eye with an iris of 128 spectrum fibres. It glances with the panning and pitch, dilates in silence, blinks on big changes; six small eyes orbit it."; }
    void eye (Graphics& g, Point<float> c, float R, float open, float lookX, float lookYY, float pup, const Frame& f, float k, Rectangle<float> r, const Palette& p, bool fibres)
    {
        Path lids;
        lids.startNewSubPath (c.x - R * 1.7f, c.y);
        lids.quadraticTo (c.x, c.y - R * 1.6f * open, c.x + R * 1.7f, c.y);
        lids.quadraticTo (c.x, c.y + R * 1.6f * open, c.x - R * 1.7f, c.y);
        g.setColour (p.fg); g.strokePath (lids, PathStrokeType (pxw (r, fibres ? 2.5f : 1.5f)));
        if (open < 0.08f) return;
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (lids);
        const Point<float> ic (c.x + lookX * R * 0.35f, c.y + lookYY * R * 0.25f);
        if (fibres)
            for (int i = 0; i < kBands; ++i)
            {
                float a = i * kTau / kBands + spin, len = R * (0.35f + 0.6f * f.bands[(size_t) i] * k);
                g.setColour (p.fgA (0.45f + 0.55f * f.bands[(size_t) i]));
                g.drawLine (ic.x + std::cos (a) * R * pup, ic.y + std::sin (a) * R * pup, ic.x + std::cos (a) * juce::jmin (R, len), ic.y + std::sin (a) * juce::jmin (R, len), pxw (r, 1.4f));
            }
        g.setColour (p.fgA (0.8f)); g.drawEllipse (ic.x - R, ic.y - R, R * 2, R * 2, pxw (r, 2));
        g.setColour (p.bg); g.fillEllipse (ic.x - R * pup, ic.y - R * pup, R * pup * 2, R * pup * 2);
        g.setColour (p.fg); g.drawEllipse (ic.x - R * pup, ic.y - R * pup, R * pup * 2, R * pup * 2, pxw (r, 1.5f));
        g.fillEllipse (ic.x - R * 0.12f + R * 0.25f * pup, ic.y - R * 0.12f - R * 0.25f * pup, R * 0.1f, R * 0.1f);
    }
    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        const float dt = (float) f.dt;
        lid = follow (lid, f.change > 0.75f || f.drop > 0.85f ? 0.0f : 1.0f, f.change > 0.75f ? 30.0f : 6.0f, dt);
        look = follow (look, juce::jlimit (-1.0f, 1.0f, f.pan * 2), 4, dt);
        lookY = follow (lookY, -f.pitchMove, 3, dt);
        pupil = follow (pupil, 0.22f + 0.35f * f.silence + 0.15f * zl (f, 0, 2) * k, 5, dt);
        spin += dt * (0.05f + 0.4f * f.mid * k);
        const auto c = r.getCentre();
        const float R = r.getHeight() * 0.22f;
        eye (g, c, R, lid, look, lookY, pupil, f, k, r, p, true);
        for (int i = 0; i < 6; ++i)
        {
            small[i] = follow (small[i], zh (f, i * 5, i * 5 + 4) > 0.6f ? 0.0f : 1.0f, small[i] > 0.5f ? 25.0f : 5.0f, dt);
            float a = i * kTau / 6 + spin * 0.3f;
            Point<float> sc (c.x + std::cos (a) * R * 2.7f, c.y + std::sin (a) * R * 1.75f);
            eye (g, sc, R * 0.22f, small[i], look, lookY, 0.35f, f, k, r, p, false);
        }
    }
};

struct Mycelium : Viz
{
    Canvas cv;
    struct Tip { float x, y, a, life; };
    std::vector<Tip> tips;
    juce::Random rnd { 11 };
    juce::String name() const override { return "Mycelium"; }
    juce::String blurb() const override { return "A fungal network spreading through the dark. Every range that jumps sprouts new threads from its own height; the highs make them branch."; }
    void reset() override { tips.clear(); cv.img = {}; }
    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        cv.prepare (r, p, 0.012f + 0.03f * (1 - f.tail));
        auto lr = r.withPosition (0, 0);
        const float u = lr.getHeight() / 1080.0f, dt = (float) f.dt;
        for (int z = 0; z < kZones; ++z)
            if ((f.zoneOnsets & (1u << z)) && tips.size() < 600)
            {
                float y = lr.getBottom() - (z + 0.5f) * lr.getHeight() / kZones;
                for (int i = 0; i < 2 + (int) (2 * k); ++i) tips.push_back ({ rnd.nextFloat() * lr.getWidth(), y, rnd.nextFloat() * kTau, 1.5f + rnd.nextFloat() * 2 });
            }
        if (tips.size() < 6) tips.push_back ({ lr.getCentreX(), lr.getCentreY(), rnd.nextFloat() * kTau, 3 });
        Graphics gi (cv.img);
        std::vector<Tip> born;
        const float branchP = 0.02f + 0.15f * zl (f, 26, 31) * k;
        for (auto& t : tips)
        {
            t.a += (rnd.nextFloat() - 0.5f) * 0.5f;
            float sp = (60 + 160 * f.energy * k) * u * dt;
            float nx = t.x + std::cos (t.a) * sp, ny = t.y + std::sin (t.a) * sp;
            gi.setColour (p.fgA (juce::jmin (1.0f, 0.4f + t.life * 0.5f) * (0.7f + 0.3f * f.mid)));
            gi.drawLine (t.x, t.y, nx, ny, pxw (r, 1.4f + t.life * 0.8f));
            t.x = nx; t.y = ny; t.life -= dt * 0.5f;
            if (rnd.nextFloat() < branchP && born.size() + tips.size() < 700) born.push_back ({ t.x, t.y, t.a + (rnd.nextBool() ? 0.6f : -0.6f), t.life * 0.8f });
        }
        tips.insert (tips.end(), born.begin(), born.end());
        tips.erase (std::remove_if (tips.begin(), tips.end(), [&] (const Tip& t) { return t.life <= 0 || ! lr.expanded (20).contains (t.x, t.y); }), tips.end());
        cv.blit (g, r);
    }
};

struct Tesseract : Viz
{
    float a1 = 0, a2 = 0, a3 = 0;
    juce::String name() const override { return "Tesseract"; }
    juce::String blurb() const override { return "A four-dimensional cube turning through itself. Bass, mids and highs each spin a different pair of axes."; }
    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        const float dt = (float) f.dt;
        a1 += dt * (0.2f + 1.5f * zl (f, 0, 8) * k); a2 += dt * (0.15f + 1.2f * zl (f, 9, 20) * k); a3 += dt * (0.1f + 1.0f * zl (f, 21, 31) * k);
        std::array<std::array<float, 4>, 16> v {};
        for (int i = 0; i < 16; ++i) for (int d = 0; d < 4; ++d) v[(size_t) i][(size_t) d] = (i >> d) & 1 ? 1.0f : -1.0f;
        auto rot = [] (std::array<float, 4>& q, int i, int j, float a) { float c = std::cos (a), s = std::sin (a), x = q[(size_t) i], y = q[(size_t) j]; q[(size_t) i] = x * c - y * s; q[(size_t) j] = x * s + y * c; };
        std::array<Point<float>, 16> pts;
        const auto c = r.getCentre();
        const float sc = r.getHeight() * 0.2f * (1 + 0.2f * zl (f, 0, 2) * k);
        for (int i = 0; i < 16; ++i)
        {
            auto q = v[(size_t) i];
            rot (q, 0, 3, a1); rot (q, 1, 3, a2); rot (q, 0, 2, a3); rot (q, 1, 2, a1 * 0.5f);
            float w = 1 / (2.6f - q[3]), x = q[0] * w, y = q[1] * w, z = q[2] * w, zz = 1 / (2.4f - z);
            pts[(size_t) i] = { c.x + x * zz * sc * 2.4f, c.y + y * zz * sc * 2.4f };
        }
        for (int i = 0; i < 16; ++i)
            for (int d = 0; d < 4; ++d)
            {
                int j = i ^ (1 << d);
                if (j < i) continue;
                g.setColour (p.fgA (0.6f + 0.4f * f.bands[(size_t) ((i * 4 + d) * 2)] * k));
                g.drawLine ({ pts[(size_t) i], pts[(size_t) j] }, pxw (r, 3.0f));
            }
        for (auto& q : pts) { g.setColour (p.fg); g.fillEllipse (q.x - 4, q.y - 4, 8, 8); }
    }
};
}

void addTrippyStyles (std::vector<std::unique_ptr<Viz>>& v)
{
    v.push_back (std::make_unique<JuliaBloom>());
    v.push_back (std::make_unique<MandelDive>());
    v.push_back (std::make_unique<BurningShip>());
    v.push_back (std::make_unique<Kaliset>());
    v.push_back (std::make_unique<FoldCrystal>());
    v.push_back (std::make_unique<Feedback>());
    v.push_back (std::make_unique<HyperTunnel>());
    v.push_back (std::make_unique<Hypno>());
    v.push_back (std::make_unique<KaleidoNoise>());
    v.push_back (std::make_unique<DomainWarp>());
    v.push_back (std::make_unique<Plasma>());
    v.push_back (std::make_unique<Moire>());
    v.push_back (std::make_unique<ReactionDiffusion>());
    v.push_back (std::make_unique<Metaballs>());
    v.push_back (std::make_unique<Cells>());
    v.push_back (std::make_unique<Cymatics>());
    v.push_back (std::make_unique<Clifford>());
    v.push_back (std::make_unique<Flame>());
    v.push_back (std::make_unique<Lorenz>());
    v.push_back (std::make_unique<FractalBloom>());
    v.push_back (std::make_unique<Sierpinski>());
    v.push_back (std::make_unique<KochBloom>());
    v.push_back (std::make_unique<Dragon>());
    v.push_back (std::make_unique<Pythagoras>());
    v.push_back (std::make_unique<FlowerOfLife>());
    v.push_back (std::make_unique<Maurer>());
    v.push_back (std::make_unique<Droste>());
    v.push_back (std::make_unique<Tesseract>());
    v.push_back (std::make_unique<ThirdEye>());
    v.push_back (std::make_unique<Mycelium>());
}
}
