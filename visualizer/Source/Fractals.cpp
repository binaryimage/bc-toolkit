// Styles 81-101: more fractals. Escape-time sets, ray-marched 3D fractals, point clouds and line fractals.
// The maths lives in FractalMath.h; this file wires each one to the music.
// Range groups used here (of 32): 0-2 sub, 3-6 bass, 7-11 low mids, 12-17 mids, 18-21 upper mids, 22-25 presence, 26-28 highs, 29-31 air.
#include "Visualizers.h"
#include "Parallel.h"
#include "FractalMath.h"
#include <complex>
#include <thread>

namespace bcv
{
namespace
{
using juce::Graphics; using juce::Rectangle; using juce::Path; using juce::PathStrokeType; using juce::Point;
namespace fr = fractal;
constexpr float kPi = juce::MathConstants<float>::pi;
constexpr float kTau = juce::MathConstants<float>::twoPi;

inline float zl (const Frame& f, int a, int b) { float s = 0; for (int i = a; i <= b; ++i) s += f.zones[(size_t) i]; return s / (float) (b - a + 1); }
inline float zh (const Frame& f, int a, int b) { float s = 0; for (int i = a; i <= b; ++i) s = juce::jmax (s, f.zoneHit[(size_t) i]); return s; }
inline float follow (float cur, float target, float rate, float dt) { return cur + (target - cur) * juce::jmin (1.0f, dt * rate); }
inline float pxw (Rectangle<float> r, float w) { return w * r.getHeight() / 1080.0f; }

/** Runs rows (y0, y1) over [0, rows) on several threads, handing out small blocks so slow rows don't hold the rest up. */
template <class Fn> void splitRows (int rows, const Fn& fn)
{
    const int threads = juce::jlimit (1, 16, pixelThreadCount());
    if (threads <= 1 || rows < 32) { fn (0, rows); return; }
    std::atomic<int> next { 0 };
    constexpr int block = 4;
    auto work = [&] { for (int y0; (y0 = next.fetch_add (block)) < rows;) fn (y0, juce::jmin (rows, y0 + block)); };
    std::vector<std::thread> pool;
    pool.reserve ((size_t) threads - 1);
    for (int i = 0; i < threads - 1; ++i) pool.emplace_back (work);
    work();
    for (auto& t : pool) t.join();
}

/** Styles computed per pixel at a lower resolution (on every core), then scaled up smoothly. */
struct FracPixels : Viz
{
    juce::Image img;
    int W = 0, H = 0, divisor = 3, maxW = 640;
    void ensure (Rectangle<float> r)
    {
        const int w = juce::jlimit (96, maxW, (int) (r.getWidth() / (float) divisor));
        const int h = juce::jmax (54, (int) std::round ((float) w * r.getHeight() / juce::jmax (1.0f, r.getWidth())));
        if (img.getWidth() != w || img.getHeight() != h) img = juce::Image (juce::Image::RGB, w, h, true, juce::SoftwareImageType());
        W = w; H = h;
    }
    /** k (x, y) -> 0..1, with y from -1 to 1 */
    template <class K> void shade (const Palette& p, const K& k)
    {
        juce::uint8 lr[256], lg[256], lb[256];
        for (int i = 0; i < 256; ++i) { auto c = p.ink ((float) i / 255.0f); lr[i] = c.getRed(); lg[i] = c.getGreen(); lb[i] = c.getBlue(); }
        juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);
        const float hh = (float) H * 0.5f, hw = (float) W * 0.5f;
        splitRows (H, [&] (int y0, int y1)
        {
            for (int y = y0; y < y1; ++y)
            {
                auto* line = bd.getLinePointer (y);
                const float ny = ((float) y - hh) / hh;
                for (int x = 0; x < W; ++x)
                {
                    const int v = (int) (fr::clamp01 (k (((float) x - hw) / hh, ny)) * 255.0f);
                    reinterpret_cast<juce::PixelRGB*> (line + (size_t) x * (size_t) bd.pixelStride)->setARGB (255, lr[v], lg[v], lb[v]);
                }
            }
        });
    }
    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        ensure (r);
        render (f, p, k);
        g.setImageResamplingQuality (Graphics::mediumResamplingQuality);
        g.drawImage (img, r);
    }
    virtual void render (const Frame& f, const Palette& p, float k) = 0;
};

/** Point-cloud styles: plot into a density grid, then show it with a log curve (or against the hottest 0.5%). */
struct FracPoints : FracPixels
{
    fr::Density den;
    void prepare (float keep) { den.size (W, H); den.fade (keep); }
    void show (const Palette& p, float gain, bool percentile = false)
    {
        const float norm = den.normaliser (gain), top = percentile ? den.percentile (0.995f) : 1.0f;
        juce::uint8 lr[256], lg[256], lb[256];
        for (int i = 0; i < 256; ++i) { auto c = p.ink ((float) i / 255.0f); lr[i] = c.getRed(); lg[i] = c.getGreen(); lb[i] = c.getBlue(); }
        juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < H; ++y)
        {
            auto* line = bd.getLinePointer (y);
            for (int x = 0; x < W; ++x)
            {
                const float v = percentile ? std::sqrt (fr::clamp01 (den.d[(size_t) y * (size_t) W + (size_t) x] / top)) : den.at (x, y, gain, norm);
                const int i = (int) (v * 255.0f);
                reinterpret_cast<juce::PixelRGB*> (line + (size_t) x * (size_t) bd.pixelStride)->setARGB (255, lr[i], lg[i], lb[i]);
            }
        }
    }
};

/** A 3D fractal seen through a camera, shaded by the shared ray marcher. */
template <class DE>
struct Marched
{
    DE de; fr::Camera cam; int steps = 56; float maxD = 10, glow = 1, trapMix = 0.6f, hit = 0.0025f; fr::V3 light { 0.5f, -0.7f, -0.5f };
    float operator() (float x, float y) const { return fr::march (de, cam, x, y, steps, maxD, glow, light, trapMix, hit); }
};

// ============================================================ 81-88: escape time

struct NewtonBasins : FracPixels
{
    float rot = 0, ang = 0, zoomS = 1;
    NewtonBasins() { divisor = 3; maxW = 720; }
    juce::String name() const override { return "Newton Basins"; }
    juce::String blurb() const override { return "Newton's method finding three roots: three basins with braided borders. The mids turn the roots, bass hits make it overshoot and boil."; }
    void reset() override { rot = ang = 0; zoomS = 1; }
    void render (const Frame& f, const Palette& p, float k) override
    {
        const float dt = (float) f.dt;
        rot += dt * (0.04f + 0.25f * zl (f, 7, 11) * k);
        ang += dt * (0.08f + 0.6f * zl (f, 12, 17) * k);
        zoomS = follow (zoomS, 1 + 0.35f * zl (f, 0, 2) * k + 0.25f * f.drop, 5, dt);
        fr::Newton n;
        const float m = 1 + 0.25f * std::sin ((float) f.t * 0.11f);
        n.cr = m * std::cos (ang); n.ci = m * std::sin (ang);
        n.relax = 1 + 0.45f * zh (f, 3, 6) * k + 0.15f * std::sin ((float) f.t * 0.07f) * (0.5f + f.drone);
        n.zoom = 0.85f / zoomS; n.rot = rot;
        n.cx = 0.15f * std::sin ((float) f.t * 0.05f); n.cy = 0.15f * std::cos ((float) f.t * 0.043f);
        n.prepare();
        shade (p, n);
    }
};

struct NovaStyle : FracPixels
{
    float t = 0, rot = 0;
    NovaStyle() { divisor = 3; maxW = 720; }
    juce::String name() const override { return "Nova"; }
    juce::String blurb() const override { return "The Nova fractal: Newton's method knocked off course at every pixel. The low mids bend it, the sub breathes it, the highs sharpen the spirals."; }
    void reset() override { t = rot = 0; }
    void render (const Frame& f, const Palette& p, float k) override
    {
        const float dt = (float) f.dt;
        t += dt * (0.2f + 0.8f * f.energy * k);
        rot += dt * (0.03f + 0.15f * f.drone);
        fr::Nova n;
        n.relax = 1 + 0.25f * std::sin (t * 0.13f) + 0.35f * zl (f, 7, 11) * k;
        n.zoom = (0.75f + 0.25f * std::sin (t * 0.05f)) / (1 + 0.3f * zl (f, 0, 2) * k);
        n.cx = -0.35f + 0.1f * std::sin (t * 0.07f); n.cy = 0.08f * std::cos (t * 0.06f);
        const float sa = 0.25f * zl (f, 26, 31) * k;
        n.seedR = 1 + sa * std::cos (t); n.seedI = sa * std::sin (t);
        n.rot = rot;
        shade (p, n);
    }
};

struct PhoenixStyle : FracPixels
{
    float t = 0, rot = 1.5708f, zoomS = 1;
    PhoenixStyle() { divisor = 3; maxW = 720; }
    juce::String name() const override { return "Phoenix Feathers"; }
    juce::String blurb() const override { return "A Phoenix Julia set, where every step remembers the last one: two feathered wings. The sub folds them, the mids ruffle the feathers."; }
    void reset() override { t = 0; rot = 1.5708f; zoomS = 1; }
    void render (const Frame& f, const Palette& p, float k) override
    {
        const float dt = (float) f.dt;
        t += dt * (0.3f + 0.7f * f.energy * k);
        rot += dt * 0.02f * (0.5f + zl (f, 7, 11) * k);
        zoomS = follow (zoomS, 1 + 0.18f * zh (f, 3, 6) * k + 0.1f * f.drop, 8, dt);
        fr::Phoenix ph;
        ph.cr = 0.5667f + 0.03f * std::sin (t * 0.21f) + 0.025f * zl (f, 12, 17) * k;
        ph.ci = 0.02f * std::sin (t * 0.17f);
        ph.pr = -0.5f + 0.06f * std::sin (t * 0.13f) - 0.06f * zl (f, 0, 2) * k;
        ph.pi = 0.04f * std::cos (t * 0.11f);
        ph.zoom = 1.35f / zoomS; ph.rot = rot;
        shade (p, ph);
    }
};

struct TricornStyle : FracPixels
{
    double depth = 0;
    float rot = 0;
    TricornStyle() { divisor = 3; maxW = 720; }
    juce::String name() const override { return "Tricorn"; }
    juce::String blurb() const override { return "The Tricorn (Mandelbar): a three-pointed cousin of the Mandelbrot set, diving into its needle and back. The low mids bend it, loudness sets the speed."; }
    void reset() override { depth = 0; rot = 0; }
    void render (const Frame& f, const Palette& p, float k) override
    {
        const float dt = (float) f.dt;
        depth += dt * (0.05 + 0.25 * f.energy * k + 0.3 * f.drop);
        rot += dt * (0.02f + 0.1f * f.drone);
        fr::Tricorn tc;
        const float u = 0.5f - 0.5f * std::cos ((float) depth);           // dives in, comes back out
        tc.zoom = 1.6f * std::exp (-2.6f * u) / (1 + 0.15f * zl (f, 0, 2) * k);
        tc.cx = fr::mixf (-0.25f, -1.36f, u); tc.cy = fr::mixf (0.0f, 0.0f, u);
        tc.rot = rot + u * 1.0472f;
        tc.bend = 0.12f * std::sin ((float) f.t * 0.09f) + 0.22f * zl (f, 7, 11) * k;
        shade (p, tc);
    }
};

struct MultibrotStyle : FracPixels
{
    float t = 0, rot = 0, zoomS = 1;
    MultibrotStyle() { divisor = 3; maxW = 640; }
    juce::String name() const override { return "Multibrot Morph"; }
    juce::String blurb() const override { return "The Mandelbrot formula with a power that slides from 3 to 7, so the set keeps growing and losing arms. The mids push the power, bass hits punch the zoom."; }
    void reset() override { t = rot = 0; zoomS = 1; }
    void render (const Frame& f, const Palette& p, float k) override
    {
        const float dt = (float) f.dt;
        t += dt * (0.25f + 0.6f * f.energy * k);
        rot += dt * (0.05f + 0.2f * zl (f, 7, 11) * k);
        zoomS = follow (zoomS, 1 + 0.2f * zh (f, 3, 6) * k + 0.15f * f.drop, 8, dt);
        fr::Multibrot m;
        m.power = 3 + 3 * (0.5f - 0.5f * std::cos (t * 0.11f)) + 0.8f * zl (f, 12, 17) * k;
        m.zoom = 1.45f / zoomS; m.rot = rot;
        shade (p, m);
    }
};

struct MagnetStyle : FracPixels
{
    float t = 0, rot = 0, zoomS = 1;
    MagnetStyle() { divisor = 3; maxW = 720; }
    juce::String name() const override { return "Magnet"; }
    juce::String blurb() const override { return "A fractal from the physics of magnets: bubbles of bubbles round a black heart. The sub swells it, presence hits shove the view."; }
    void reset() override { t = rot = 0; zoomS = 1; }
    void render (const Frame& f, const Palette& p, float k) override
    {
        const float dt = (float) f.dt;
        t += dt * (0.2f + 0.6f * f.energy * k);
        rot += dt * (0.02f + 0.12f * f.drone);
        zoomS = follow (zoomS, 1 + 0.3f * zl (f, 0, 2) * k + 0.2f * f.drop, 5, dt);
        fr::Magnet m;
        const float in = 0.5f - 0.5f * std::cos (t * 0.06f);
        m.zoom = fr::mixf (2.6f, 1.0f, in) / zoomS;
        m.cx = fr::mixf (1.4f, 2.0f, in) + 0.03f * zh (f, 22, 25) * k; m.cy = fr::mixf (0.0f, 1.2f, in);
        m.rot = rot;
        shade (p, m);
    }
};

struct LyapunovStyle : FracPixels
{
    float t = 0, rot = 0;
    LyapunovStyle() { divisor = 3; maxW = 640; }
    juce::String name() const override { return "Lyapunov Zircon"; }
    juce::String blurb() const override { return "A Lyapunov fractal: order glows, chaos goes dark. The window drifts over the map, the sub zooms it, each new section changes the pattern."; }
    void reset() override { t = rot = 0; }
    void render (const Frame& f, const Palette& p, float k) override
    {
        const float dt = (float) f.dt;
        t += dt * (0.2f + 0.6f * f.energy * k);
        rot += dt * (0.02f + 0.1f * zl (f, 7, 11) * k);
        static const unsigned pats[] = { 0b10u, 0b0110u, 0b00110u, 0b001u, 0b011010u };
        static const int lens[] = { 2, 4, 5, 3, 6 };
        fr::Lyapunov l;
        const int which = ((f.section % 5) + 5) % 5;
        l.pattern = pats[which]; l.patternLen = lens[which];
        l.a0 = 3.3f + 0.12f * std::sin (t * 0.05f); l.b0 = 3.5f + 0.12f * std::cos (t * 0.043f);
        l.span = 0.3f / (1 + 0.35f * zl (f, 0, 2) * k);
        l.rot = rot;
        shade (p, l);
    }
};

struct StalksStyle : FracPixels
{
    float ang = 0, rot = 0;
    StalksStyle() { divisor = 3; maxW = 720; }
    juce::String name() const override { return "Pickover Stalks"; }
    juce::String blurb() const override { return "A Julia set drawn only where its orbits brush the axes: glowing stalks and spirals. The low mids steer it, the highs thicken the stalks."; }
    void reset() override { ang = rot = 0; }
    void render (const Frame& f, const Palette& p, float k) override
    {
        const float dt = (float) f.dt;
        ang += dt * (0.05f + 0.35f * zl (f, 7, 11) * k);
        rot += dt * (0.03f + 0.15f * f.drone);
        fr::Stalks s;
        const float a = ang + 0.3f * zh (f, 12, 17) * k, edge = 1.0f - 0.04f * zl (f, 3, 6) * k;
        s.cr = (0.5f * std::cos (a) - 0.25f * std::cos (2 * a)) * edge;
        s.ci = (0.5f * std::sin (a) - 0.25f * std::sin (2 * a)) * edge;
        s.width = 45 - 25 * zl (f, 26, 31) * k;
        s.zoom = 1.5f / (1 + 0.25f * zl (f, 0, 2) * k + 0.1f * f.beat);
        s.rot = rot;
        shade (p, s);
    }
};

// ============================================================ 89-93: ray-marched 3D

struct BulbStyle : FracPixels
{
    float yaw = 0, pitch = 0, dist = 2.6f, lightA = 0;
    BulbStyle() { divisor = 4; maxW = 480; }
    juce::String name() const override { return "Mandelbulb"; }
    juce::String blurb() const override { return "The Mandelbulb, the 3D Mandelbrot set, turning in the light. Loudness turns it, the sub pulls the camera in, the low mids swing the light round."; }
    void reset() override { yaw = pitch = lightA = 0; dist = 2.6f; }
    void render (const Frame& f, const Palette& p, float k) override
    {
        const float dt = (float) f.dt;
        yaw += dt * (0.08f + 0.4f * f.energy * k + 0.6f * f.drop);
        pitch = 0.35f * std::sin ((float) f.t * 0.07f);
        dist = follow (dist, 2.7f - 0.5f * zl (f, 0, 2) * k - 0.25f * f.drop, 4, dt);
        lightA += dt * (0.1f + 0.8f * zl (f, 7, 11) * k);
        Marched<fr::Mandelbulb> m;
        m.de.morph = 0.12f * std::sin ((float) f.t * 0.05f) + 0.15f * zl (f, 12, 17) * k;
        m.cam.look (fr::rotY (fr::rotX ({ 0, 0, -dist }, pitch), yaw), { 0, 0, 0 });
        m.light = fr::V3 { std::cos (lightA), -0.7f, std::sin (lightA) }.norm();
        m.steps = 48; m.maxD = 6; m.glow = 0.8f + 0.6f * zl (f, 26, 31) * k;
        shade (p, m);
    }
};

struct BoxStyle : FracPixels
{
    float yaw = 0.6f, t = 0;
    BoxStyle() { divisor = 4; maxW = 480; }
    juce::String name() const override { return "Mandelbox"; }
    juce::String blurb() const override { return "The Mandelbox: a cube folded into itself until it turns to carved stone, flown round and into. The bass changes the fold, loudness flies it."; }
    void reset() override { yaw = 0.6f; t = 0; }
    void render (const Frame& f, const Palette& p, float k) override
    {
        const float dt = (float) f.dt;
        t += dt * (0.1f + 0.35f * f.energy * k);
        yaw += dt * (0.05f + 0.25f * f.energy * k);
        Marched<fr::Mandelbox> m;
        m.de.scale = -1.75f + 0.12f * std::sin (t * 0.3f) - 0.1f * zl (f, 3, 6) * k;
        const float near = 0.5f - 0.5f * std::cos (t * 0.4f);              // swoops in close, then back out
        const float d = fr::mixf (6.0f, 3.0f, near);
        m.cam.look (fr::rotY ({ 0, d * 0.55f, -d }, yaw), { 0, 0, 0 });
        m.light = fr::V3 { 0.6f, -0.6f, -0.4f }.norm();
        m.steps = 56; m.maxD = 14; m.glow = 0.9f + 0.6f * zl (f, 26, 31) * k;
        shade (p, m);
    }
};

struct MengerStyle : FracPixels
{
    float z = 0, sway = 0, roll = 0;
    MengerStyle() { divisor = 4; maxW = 480; }
    juce::String name() const override { return "Menger Tunnels"; }
    juce::String blurb() const override { return "Flying down the endless tunnels of a Menger sponge. Loudness sets the speed, drops throw it forward, the low mids roll the view."; }
    void reset() override { z = sway = roll = 0; }
    void render (const Frame& f, const Palette& p, float k) override
    {
        const float dt = (float) f.dt;
        z += dt * (0.25f + 1.1f * f.energy * k + 1.5f * f.drop);
        if (z > 54) z -= 54;                          // the sponge repeats every 2 (and its holes every 2/27), so this is seamless
        sway += dt * (0.2f + 0.6f * zl (f, 7, 11) * k);
        roll = follow (roll, 0.4f * std::sin (sway * 0.7f) * (0.4f + zl (f, 7, 11) * k), 3, dt);
        Marched<fr::Menger> m;
        const fr::V3 eye { 0.04f * std::sin (sway), 0.04f * std::cos (sway * 0.8f), z };
        m.cam.look (eye, eye + fr::V3 { 0.12f * std::sin (sway * 0.5f), 0.08f * std::cos (sway * 0.4f), 3 }, roll);
        m.light = fr::V3 { 0.4f * std::sin (sway), -0.8f, 0.3f }.norm();
        m.steps = 56; m.maxD = 14; m.glow = 0.3f; m.trapMix = 0.5f;
        shade (p, m);
    }
};

struct ApolloStyle : FracPixels
{
    float t = 0;
    ApolloStyle() { divisor = 5; maxW = 360; }
    juce::String name() const override { return "Apollonian Foam"; }
    juce::String blurb() const override { return "Inside an endless sphere packing: bubbles in bubbles, flown through. Loudness flies it, the sub inflates the bubbles."; }
    void reset() override { t = 0; }
    void render (const Frame& f, const Palette& p, float k) override
    {
        t += (float) f.dt * (0.4f + 1.2f * f.energy * k + 1.2f * f.drop);
        Marched<fr::Apollonian> m;
        m.de.k = 1.1f + 0.12f * zl (f, 0, 2) * k;
        m.cam.look ({ 2.8f * std::cos (0.1f + 0.33f * t), 0.4f + 0.3f * std::cos (0.37f * t), 2.8f * std::cos (0.5f + 0.35f * t) },
                    { 1.9f * std::cos (1.2f + 0.41f * t), 0.4f + 0.1f * std::cos (0.27f * t), 1.9f * std::cos (2.0f + 0.38f * t) });
        m.steps = 44; m.maxD = 10; m.glow = 0.6f; m.hit = 0.004f;
        shade (p, m);
    }
};

struct TetraStyle : FracPixels
{
    float yaw = 0;
    TetraStyle() { divisor = 4; maxW = 480; }
    juce::String name() const override { return "Sierpinski Tetra"; }
    juce::String blurb() const override { return "A 3D Sierpinski tetrahedron made of ever smaller ones, turning and twisting. The mids twist its folds, the bass swells it."; }
    void reset() override { yaw = 0; }
    void render (const Frame& f, const Palette& p, float k) override
    {
        const float dt = (float) f.dt;
        yaw += dt * (0.1f + 0.45f * f.energy * k + 0.5f * f.drop);
        Marched<fr::Tetra> m;
        m.de.a1 = 0.03f * std::sin ((float) f.t * 0.13f) + 0.04f * zl (f, 12, 17) * k;
        m.de.a2 = 0.03f * std::cos ((float) f.t * 0.11f);
        m.de.scale = 2 + 0.06f * zl (f, 3, 6) * k;
        m.cam.look (fr::rotY ({ 0, 0.9f + 0.4f * std::sin ((float) f.t * 0.05f), -3.0f / (1 + 0.15f * zl (f, 0, 2) * k) }, yaw), { 0, 0, 0 });
        m.cam.lens = 1.5f;
        m.steps = 56; m.maxD = 8; m.glow = 1; m.trapMix = 0.5f;
        shade (p, m);
    }
};

// ============================================================ 94-97: points

struct FernStyle : FracPoints
{
    fr::Fern fern;
    float bend = 0, curl = 0;
    FernStyle() { divisor = 3; maxW = 640; }
    juce::String name() const override { return "Barnsley Fern"; }
    juce::String blurb() const override { return "Barnsley's fern from four simple maps, swaying and curling. The low mids lean it, the mids curl the fronds, the highs light it up."; }
    void reset() override { bend = curl = 0; den.clear(); }
    void render (const Frame& f, const Palette& p, float k) override
    {
        const float dt = (float) f.dt;
        bend = follow (bend, 0.03f * std::sin ((float) f.t * 0.4f) + 0.05f * (zl (f, 7, 11) - 0.3f) * k, 3, dt);
        curl = follow (curl, 0.025f * std::sin ((float) f.t * 0.23f) + 0.04f * (zl (f, 12, 17) - 0.3f) * k, 3, dt);
        prepare (0.35f);
        fern.bend = bend; fern.curl = curl;
        fern.run (den, 90000, 0.15f * (1 + 0.05f * zl (f, 0, 2) * k), -0.05f, 0.8f, 1);
        show (p, 2.0f + 2.0f * zl (f, 26, 31) * k);
    }
};

struct HopalongStyle : FracPoints
{
    fr::Hopalong h;
    float rot = 0;
    HopalongStyle() { divisor = 3; maxW = 640; }
    juce::String name() const override { return "Hopalong"; }
    juce::String blurb() const override { return "Martin's Hopalong attractor: rings and petals of dots from one tiny formula. Each third of the ranges bends one of its three numbers."; }
    void reset() override { rot = 0; den.clear(); }
    void render (const Frame& f, const Palette& p, float k) override
    {
        const float dt = (float) f.dt, t = (float) f.t;
        rot += dt * (0.03f + 0.15f * f.drone);
        h.a = follow (h.a, 2.0f + 0.4f * std::sin (t * 0.031f) + 0.5f * zl (f, 0, 10) * k, 2, dt);
        h.b = follow (h.b, 0.5f + 0.2f * std::sin (t * 0.023f) + 0.3f * zl (f, 11, 21) * k, 2, dt);
        h.c = follow (h.c, 1.2f + 0.4f * std::cos (t * 0.027f) + 0.4f * zl (f, 22, 31) * k, 2, dt);
        prepare (0.55f);
        h.run (den, 120000, 0.12f / (1 + 0.15f * zl (f, 0, 2) * k), rot);
        show (p, 2.0f);
    }
};

struct ThomasStyle : FracPoints
{
    fr::Thomas th;
    float yaw = 0;
    ThomasStyle() { divisor = 3; maxW = 640; }
    juce::String name() const override { return "Thomas Drift"; }
    juce::String blurb() const override { return "Thousands of specks drifting through Thomas's attractor, a looping knot seen turning in 3D. Loudness speeds the drift, the sub loosens the knot."; }
    void reset() override { th.pts.clear(); yaw = 0; den.clear(); }
    void render (const Frame& f, const Palette& p, float k) override
    {
        if (th.pts.empty()) { th.seed (9000); for (int i = 0; i < 200; ++i) th.step (0.05f); }
        const float dt = (float) f.dt;
        th.b = 0.19f + 0.012f * std::sin ((float) f.t * 0.05f) - 0.02f * zl (f, 0, 2) * k;
        yaw += dt * (0.1f + 0.3f * f.energy * k);
        const float h = dt * (0.8f + 2.5f * f.energy * k + 2 * f.drop);
        prepare (0.72f);
        for (int i = 0; i < 3; ++i) { th.step (h / 3); th.draw (den, yaw, 0.35f * std::sin ((float) f.t * 0.07f), 0.21f); }
        show (p, 2.0f + 2.0f * zl (f, 26, 31) * k);
    }
};

struct BuddhaStyle : FracPoints
{
    fr::Buddha b;
    float rot = 0;
    BuddhaStyle() { divisor = 3; maxW = 640; }
    juce::String name() const override { return "Buddhabrot"; }
    juce::String blurb() const override { return "The ghost of the Mandelbrot set: the paths of the points that escape it, piled up into a glowing figure. The low mids warp it, the mids add detail."; }
    void reset() override { rot = 0; den.clear(); }
    void render (const Frame& f, const Palette& p, float k) override
    {
        rot += (float) f.dt * (0.01f + 0.05f * f.drone);
        prepare (0.94f);
        b.run (den, 26000, 60 + (int) (30 * zl (f, 12, 17) * k), 0.6f / (1 + 0.12f * zl (f, 0, 2) * k), rot,
               0.18f * zl (f, 7, 11) * k * std::sin ((float) f.t * 0.3f));
        show (p, 1.0f, true);
    }
};

// ============================================================ 98-101: lines

struct HilbertStyle : Viz
{
    std::vector<Point<float>> pts;   // one unit square, order 6
    float spin = 0, run = 0;
    juce::String name() const override { return "Hilbert Curve"; }
    juce::String blurb() const override { return "A Hilbert curve, one line that fills a square, with the spectrum running along it. The bass bends the corners, the beat sends a light down the line."; }
    void reset() override { spin = run = 0; }
    static Point<float> d2xy (int n, int d)
    {
        int x = 0, y = 0;
        for (int s = 1, t = d; s < n; s *= 2, t /= 4)
        {
            const int rx = 1 & (t / 2), ry = 1 & (t ^ rx);
            if (ry == 0) { if (rx == 1) { x = s - 1 - x; y = s - 1 - y; } std::swap (x, y); }
            x += s * rx; y += s * ry;
        }
        return { (float) x, (float) y };
    }
    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        constexpr int n = 32;
        if (pts.empty()) for (int d = 0; d < n * n; ++d) { auto q = d2xy (n, d); pts.push_back ({ (q.x + 0.5f) / n - 0.5f, (q.y + 0.5f) / n - 0.5f }); }
        const float dt = (float) f.dt;
        spin += dt * (0.02f + 0.08f * f.drone);
        run += dt * (0.05f + 0.25f * f.energy * k) + 0.02f * f.beat;
        const auto c = r.getCentre();
        const float side = r.getHeight() * 0.9f * (1 + 0.06f * zl (f, 0, 2) * k);
        const float bendAmt = 0.45f * zl (f, 3, 6) * k;             // corners pulled toward their neighbours' middle
        const float cs = std::cos (spin), sn = std::sin (spin);
        auto place = [&] (Point<float> q) { return Point<float> (c.x + (q.x * cs - q.y * sn) * side, c.y + (q.x * sn + q.y * cs) * side); };
        const int N = (int) pts.size(), chunk = 16;
        for (int i0 = 0; i0 < N - 1; i0 += chunk)
        {
            Path path;
            const int i1 = juce::jmin (N - 1, i0 + chunk);
            for (int i = i0; i <= i1; ++i)
            {
                auto q = pts[(size_t) i];
                if (bendAmt > 0.01f && i > 0 && i < N - 1) q = q + ((pts[(size_t) i - 1] + pts[(size_t) i + 1]) * 0.5f - q) * bendAmt;
                if (i == i0) path.startNewSubPath (place (q)); else path.lineTo (place (q));
            }
            const float u = (float) i0 / (float) N;
            const float band = f.bands[(size_t) juce::jmin (kBands - 1, (int) (u * kBands))];
            const float head = std::exp (-std::pow ((u - (run - std::floor (run))) * 12.0f, 2.0f));    // the light running along
            g.setColour (p.fgA (0.45f + 0.5f * band * k + 0.6f * head));
            g.strokePath (path, PathStrokeType (pxw (r, 3.0f + 4.0f * head), PathStrokeType::curved, PathStrokeType::rounded));
        }
    }
};

struct LevyStyle : Viz
{
    std::vector<Point<float>> pts;
    float spin = 0, fx = 0, fy = 0, fs = 0;
    juce::String name() const override { return "Levy Lightning"; }
    juce::String blurb() const override { return "The Levy C curve with its folding angle let loose, so it flickers between a tapestry and forked lightning. The low mids set the angle, presence hits crack it."; }
    void reset() override { spin = 0; fs = 0; }
    void build (Point<float> a, Point<float> b, int depth, float ang)
    {
        if (depth == 0) { pts.push_back (b); return; }
        const auto d = b - a;
        const float h = 0.5f / std::cos (ang);
        const Point<float> m (a.x + (d.x * std::cos (ang) - d.y * std::sin (ang)) * h, a.y + (d.x * std::sin (ang) + d.y * std::cos (ang)) * h);
        build (a, m, depth - 1, ang); build (m, b, depth - 1, ang);
    }
    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        spin += (float) f.dt * (0.03f + 0.1f * f.drone);
        const float ang = juce::jlimit (0.35f, 1.1f, 0.7854f + 0.18f * std::sin ((float) f.t * 0.13f) + 0.2f * (zl (f, 7, 11) - 0.3f) * k + 0.12f * zh (f, 22, 25) * k);
        pts.clear(); pts.reserve (1100);
        pts.push_back ({ 0, 0 });
        build ({ 0, 0 }, { std::cos (spin), std::sin (spin) }, 10, ang);
        auto b = Rectangle<float>::findAreaContainingPoints (pts.data(), (int) pts.size());
        const float sc = juce::jmin (r.getWidth() * 0.85f / juce::jmax (1e-4f, b.getWidth()), r.getHeight() * 0.85f / juce::jmax (1e-4f, b.getHeight()));
        const float dt = (float) f.dt;
        if (fs <= 0) { fx = b.getCentreX(); fy = b.getCentreY(); fs = sc; }
        fx = follow (fx, b.getCentreX(), 4, dt); fy = follow (fy, b.getCentreY(), 4, dt); fs = follow (fs, sc, 4, dt);
        const auto c = r.getCentre();
        auto place = [&] (Point<float> q) { return Point<float> (c.x + (q.x - fx) * fs, c.y + (q.y - fy) * fs); };
        const int N = (int) pts.size(), chunk = 16;
        for (int i0 = 0; i0 < N - 1; i0 += chunk)
        {
            Path path;
            const int i1 = juce::jmin (N - 1, i0 + chunk);
            path.startNewSubPath (place (pts[(size_t) i0]));
            for (int i = i0 + 1; i <= i1; ++i) path.lineTo (place (pts[(size_t) i]));
            const float u = (float) i0 / (float) N;
            g.setColour (p.fgA (0.4f + 0.6f * f.bands[(size_t) juce::jmin (kBands - 1, (int) (u * kBands))] * k));
            g.strokePath (path, PathStrokeType (pxw (r, 2.4f), PathStrokeType::curved, PathStrokeType::rounded));
        }
    }
};

struct GasketStyle : Viz
{
    struct C { float k; std::complex<float> z; };   // curvature and centre (outer circle has negative curvature)
    std::vector<std::pair<C, int>> circles;
    float spin = 0;
    juce::String name() const override { return "Apollonian Gasket"; }
    juce::String blurb() const override { return "Circles packed into the gaps between circles, forever. Each depth of circles answers its own part of the spectrum; the whole thing turns with the drones."; }
    void reset() override { spin = 0; }
    static C reflect (const C& a, const C& b, const C& c, const C& opp)
    {
        // the other circle touching a, b and c (Descartes' theorem, flipped across the one we came from)
        C d;
        d.k = 2 * (a.k + b.k + c.k) - opp.k;
        d.z = (2.0f * (a.k * a.z + b.k * b.z + c.k * c.z) - opp.k * opp.z) / d.k;
        return d;
    }
    void fill (const C& a, const C& b, const C& c, const C& opp, int depth, float minK)
    {
        const C d = reflect (a, b, c, opp);
        if (! (d.k > 0) || d.k > minK || circles.size() > 8000) return;
        circles.push_back ({ d, depth });
        fill (a, b, d, c, depth + 1, minK);
        fill (a, c, d, b, depth + 1, minK);
        fill (b, c, d, a, depth + 1, minK);
    }
    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        spin += (float) f.dt * (0.04f + 0.2f * f.drone + 0.3f * zl (f, 7, 11) * k);
        const float R = r.getHeight() * 0.47f * (1 + 0.04f * zl (f, 0, 2) * k);
        const float rin = R / (1 + 2 / std::sqrt (3.0f));          // three equal circles inside the big one
        circles.clear();
        C outer { -1 / R, { 0, 0 } }, in[3];
        for (int i = 0; i < 3; ++i) in[i] = { 1 / rin, std::polar (R - rin, spin + (float) i * kTau / 3) };
        circles.push_back ({ outer, 0 });
        for (auto& c : in) circles.push_back ({ c, 1 });
        const float minK = 1 / pxw (r, 2.5f);
        fill (in[0], in[1], in[2], outer, 2, minK);
        fill (outer, in[0], in[1], in[2], 2, minK);
        fill (outer, in[1], in[2], in[0], 2, minK);
        fill (outer, in[0], in[2], in[1], 2, minK);
        const auto c = r.getCentre();
        for (auto& [cc, depth] : circles)
        {
            const float rad = 1 / std::abs (cc.k);
            const int z0 = juce::jmin (kZones - 4, depth * 4);
            const float lv = zl (f, z0, z0 + 3);
            g.setColour (p.fgA (0.35f + 0.65f * lv * k));
            const float x = c.x + cc.z.real(), y = c.y + cc.z.imag();
            g.drawEllipse (x - rad, y - rad, rad * 2, rad * 2, pxw (r, depth < 2 ? 2.4f : 1.4f));
        }
    }
};

struct TSquareStyle : Viz
{
    float t = 0;
    juce::String name() const override { return "T-Square"; }
    juce::String blurb() const override { return "The T-square fractal: a square with a half-size square on every corner, and so on down. Each level pulses and turns with its own band."; }
    void reset() override { t = 0; }
    void square (Graphics& g, Point<float> c, float s, float rot, int depth, const Frame& f, float k, Rectangle<float> r, const Palette& p)
    {
        const int z0 = juce::jmin (kZones - 5, (4 - depth) * 6);
        const float lv = zl (f, z0, z0 + 4);
        const float rr = rot + 0.06f * (4 - depth) * std::sin (t * 0.6f + (float) depth) * (0.5f + lv * k);
        Path sq;
        sq.addRectangle (-s, -s, s * 2, s * 2);
        sq.applyTransform (juce::AffineTransform::rotation (rr).translated (c.x, c.y));
        g.setColour (p.fgA (0.04f + 0.12f * lv * k));
        g.fillPath (sq);
        g.setColour (p.fgA (0.45f + 0.55f * lv * k));
        g.strokePath (sq, PathStrokeType (pxw (r, 1.4f + depth * 0.5f)));
        if (depth == 0) return;
        for (int i = 0; i < 4; ++i)
        {
            const float a = rr + kPi * 0.25f + (float) i * kPi * 0.5f;
            const Point<float> corner (c.x + std::cos (a) * s * 1.41421f, c.y + std::sin (a) * s * 1.41421f);
            square (g, corner, s * 0.5f * (1 + 0.06f * lv * k), rr, depth - 1, f, k, r, p);
        }
    }
    void draw (Graphics& g, Rectangle<float> r, const Frame& f, const Palette& p, float k) override
    {
        t += (float) f.dt * (0.5f + f.energy * k);
        square (g, r.getCentre(), r.getHeight() * 0.22f * (1 + 0.08f * zl (f, 0, 2) * k), t * 0.03f, 4, f, k, r, p);
    }
};
}

void addFractalStyles (std::vector<std::unique_ptr<Viz>>& v)
{
    v.push_back (std::make_unique<NewtonBasins>());
    v.push_back (std::make_unique<NovaStyle>());
    v.push_back (std::make_unique<PhoenixStyle>());
    v.push_back (std::make_unique<TricornStyle>());
    v.push_back (std::make_unique<MultibrotStyle>());
    v.push_back (std::make_unique<MagnetStyle>());
    v.push_back (std::make_unique<LyapunovStyle>());
    v.push_back (std::make_unique<StalksStyle>());
    v.push_back (std::make_unique<BulbStyle>());
    v.push_back (std::make_unique<BoxStyle>());
    v.push_back (std::make_unique<MengerStyle>());
    v.push_back (std::make_unique<ApolloStyle>());
    v.push_back (std::make_unique<TetraStyle>());
    v.push_back (std::make_unique<FernStyle>());
    v.push_back (std::make_unique<HopalongStyle>());
    v.push_back (std::make_unique<ThomasStyle>());
    v.push_back (std::make_unique<BuddhaStyle>());
    v.push_back (std::make_unique<HilbertStyle>());
    v.push_back (std::make_unique<LevyStyle>());
    v.push_back (std::make_unique<GasketStyle>());
    v.push_back (std::make_unique<TSquareStyle>());
}

bool isFractalStyle (const juce::String& name)
{
    static const juce::StringArray names {
        // 51-80
        "Julia Bloom", "Mandelbrot Dive", "Burning Ship", "Kaliset", "Clifford Silk", "Fractal Flame", "Strange Attractor",
        "Fractal Bloom", "Sierpinski Pulse", "Snowflake", "Dragon Fold", "Pythagoras Tree", "Fold Crystal",
        // 81 on
        "Newton Basins", "Nova", "Phoenix Feathers", "Tricorn", "Multibrot Morph", "Magnet", "Lyapunov Zircon", "Pickover Stalks",
        "Mandelbulb", "Mandelbox", "Menger Tunnels", "Apollonian Foam", "Sierpinski Tetra", "Barnsley Fern", "Hopalong",
        "Thomas Drift", "Buddhabrot", "Hilbert Curve", "Levy Lightning", "Apollonian Gasket", "T-Square" };
    return names.contains (name);
}
}
