// The maths of the fractal styles (styles 81 on): plain C++, no JUCE, so each one can be checked on its own.
// Every kernel turns a point (x across, y down; y from -1 to 1, x scaled the same) into a brightness 0..1.
// Brightness is also where a pixel sits in a palette, so the kernels spread their detail over the whole range.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace bcv::fractal
{
inline float clamp01 (float v) { return v > 0 ? (v < 1 ? v : 1) : 0; }   // NaN comes out as 0
inline float mixf (float a, float b, float t) { return a + (b - a) * t; }
inline float smooth01 (float t) { t = clamp01 (t); return t * t * (3 - 2 * t); }

struct V3
{
    float x = 0, y = 0, z = 0;
    V3 operator+ (V3 o) const { return { x + o.x, y + o.y, z + o.z }; }
    V3 operator- (V3 o) const { return { x - o.x, y - o.y, z - o.z }; }
    V3 operator* (float s) const { return { x * s, y * s, z * s }; }
    float dot (V3 o) const { return x * o.x + y * o.y + z * o.z; }
    float len() const { return std::sqrt (x * x + y * y + z * z); }
    V3 norm() const { float l = len(); return l > 1e-9f ? *this * (1.0f / l) : V3 { 0, 0, 1 }; }
};
inline V3 rotY (V3 p, float a) { float c = std::cos (a), s = std::sin (a); return { p.x * c + p.z * s, p.y, -p.x * s + p.z * c }; }
inline V3 rotX (V3 p, float a) { float c = std::cos (a), s = std::sin (a); return { p.x, p.y * c - p.z * s, p.y * s + p.z * c }; }
inline V3 rotZ (V3 p, float a) { float c = std::cos (a), s = std::sin (a); return { p.x * c - p.y * s, p.x * s + p.y * c, p.z }; }

/** smooth iteration count, 0..1 over maxIt, with a soft gamma so the edges get most of the range */
inline float smoothIter (int i, float r2, int maxIt, float power = 2)
{
    const float sm = (float) i + 1 - std::log (std::log (std::max (r2, 1.0001f)) * 0.5f) / std::log (power);
    return clamp01 (sm / (float) maxIt);
}

// ============================================================ 2D, escape time

/** Newton's method on z^3 = w: three basins (one brightness band each), shaded by how fast they settle. */
struct Newton
{
    float cr = 1, ci = 0;      // w, the cube the roots come from (turning it turns the roots)
    float relax = 1;           // 1 = plain Newton; above 1 it overshoots and the basins boil
    float zoom = 1.6f, rot = 0, cx = 0, cy = 0;
    float rootsR[3] {}, rootsI[3] {};
    void prepare()
    {
        const float m = std::cbrt (std::sqrt (cr * cr + ci * ci)), a = std::atan2 (ci, cr) / 3;
        for (int k = 0; k < 3; ++k) { rootsR[k] = m * std::cos (a + k * 2.0943951f); rootsI[k] = m * std::sin (a + k * 2.0943951f); }
    }
    float operator() (float x, float y) const
    {
        const float c = std::cos (rot), s = std::sin (rot);
        float zr = (x * c - y * s) * zoom + cx, zi = (x * s + y * c) * zoom + cy;
        constexpr int maxIt = 40;
        for (int i = 0; i < maxIt; ++i)
        {
            // z -= relax * (z^3 - w) / (3 z^2)
            const float z2r = zr * zr - zi * zi, z2i = 2 * zr * zi;
            const float z3r = z2r * zr - z2i * zi - cr, z3i = z2r * zi + z2i * zr - ci;
            const float dr = 3 * z2r, di = 3 * z2i, dd = dr * dr + di * di + 1e-12f;
            const float qr = (z3r * dr + z3i * di) / dd, qi = (z3i * dr - z3r * di) / dd;
            zr -= relax * qr; zi -= relax * qi;
            for (int k = 0; k < 3; ++k)
            {
                const float ex = zr - rootsR[k], ey = zi - rootsI[k], d2 = ex * ex + ey * ey;
                if (d2 < 1e-4f)
                {
                    // fractional count from how close it got on the last step
                    const float f = (float) i + clamp01 (std::log (d2 * 1e4f + 1e-9f) / -9.2f);
                    const float speed = std::pow (clamp01 (1 - f / 22.0f), 0.8f);
                    return (0.18f + 0.27f * (float) k) + 0.30f * speed;   // three bands; the fast-settling middles glow
                }
            }
        }
        return 0.02f;
    }
};

/** The Nova fractal: relaxed Newton on z^3 - 1, plus the pixel as c (Mandelbrot-style). */
struct Nova
{
    float relax = 1, zoom = 0.9f, cx = -0.3f, cy = 0, rot = 0, seedR = 1, seedI = 0;
    float operator() (float x, float y) const
    {
        const float c = std::cos (rot), s = std::sin (rot);
        const float pr = (x * c - y * s) * zoom + cx, pi = (x * s + y * c) * zoom + cy;
        float zr = seedR, zi = seedI;
        constexpr int maxIt = 48;
        for (int i = 0; i < maxIt; ++i)
        {
            const float z2r = zr * zr - zi * zi, z2i = 2 * zr * zi;
            const float z3r = z2r * zr - z2i * zi - 1, z3i = z2r * zi + z2i * zr;
            const float dr = 3 * z2r, di = 3 * z2i, dd = dr * dr + di * di + 1e-12f;
            const float qr = (z3r * dr + z3i * di) / dd, qi = (z3i * dr - z3r * di) / dd;
            const float nr = zr - relax * qr + pr, ni = zi - relax * qi + pi;
            const float step = (nr - zr) * (nr - zr) + (ni - zi) * (ni - zi);
            zr = nr; zi = ni;
            if (step < 1e-6f) return std::pow (clamp01 ((float) i / 30.0f), 0.55f) * 0.95f;   // settled: lit by how long it took
            if (zr * zr + zi * zi > 1e4f) return 0.08f + 0.2f * (float) i / maxIt;         // flew off
        }
        return 1.0f;
    }
};

/** Phoenix Julia: each step also remembers the one before it, which bends the set into feathers. */
struct Phoenix
{
    float cr = 0.5667f, ci = 0, pr = -0.5f, pi = 0, zoom = 1.35f, rot = 1.5708f;
    float operator() (float x, float y) const
    {
        const float c = std::cos (rot), s = std::sin (rot);
        float zr = (x * c - y * s) * zoom, zi = (x * s + y * c) * zoom, yr = 0, yi = 0;
        constexpr int maxIt = 64;
        int i = 0;
        for (; i < maxIt && zr * zr + zi * zi < 16; ++i)
        {
            const float nr = zr * zr - zi * zi + cr + pr * yr - pi * yi;
            const float ni = 2 * zr * zi + ci + pr * yi + pi * yr;
            yr = zr; yi = zi; zr = nr; zi = ni;
        }
        if (i == maxIt) return 0.92f - 0.25f * clamp01 (std::sqrt (zr * zr + zi * zi));
        return std::pow (smoothIter (i, zr * zr + zi * zi, maxIt), 0.45f) * 0.85f;
    }
};

/** The Tricorn (Mandelbar): z -> conj(z)^2 + c. Three-fold, with odd broken edges. */
struct Tricorn
{
    float zoom = 1.7f, cx = -0.25f, cy = 0, rot = 0, bend = 0;
    float operator() (float x, float y) const
    {
        const float c = std::cos (rot), s = std::sin (rot);
        const float ar = (x * c - y * s) * zoom + cx, ai = (x * s + y * c) * zoom + cy;
        float zr = 0, zi = 0;
        constexpr int maxIt = 70;
        int i = 0;
        float trap = 1e9f;
        for (; i < maxIt && zr * zr + zi * zi < 64; ++i)
        {
            const float nr = zr * zr - zi * zi + ar, ni = -2 * zr * zi + ai + bend * zr;
            zr = nr; zi = ni;
            trap = std::min (trap, std::abs (zr * zr + zi * zi - 0.25f));
        }
        if (i == maxIt) return 0.25f * std::exp (-trap * 8);
        return std::pow (smoothIter (i, zr * zr + zi * zi, maxIt), 0.4f);
    }
};

/** Multibrot: z -> z^d + c with a power that slides (3 lobes, 4, 5...), so the set grows and loses arms. */
struct Multibrot
{
    float power = 3, zoom = 1.45f, rot = 0, cx = 0, cy = 0;
    float operator() (float x, float y) const
    {
        const float c = std::cos (rot), s = std::sin (rot);
        const float ar = (x * c - y * s) * zoom + cx, ai = (x * s + y * c) * zoom + cy;
        float zr = 0, zi = 0;
        constexpr int maxIt = 44;
        int i = 0;
        for (; i < maxIt; ++i)
        {
            const float r2 = zr * zr + zi * zi;
            if (r2 > 64) break;
            const float rp = std::pow (r2, power * 0.5f), th = std::atan2 (zi, zr) * power;
            zr = rp * std::cos (th) + ar; zi = rp * std::sin (th) + ai;
        }
        if (i == maxIt) return 0.0f;
        return std::pow (smoothIter (i, zr * zr + zi * zi, maxIt, power), 0.5f);
    }
};

/** Magnet type I, from the physics of magnets: escapes, or settles at 1. */
struct Magnet
{
    float zoom = 2.6f, cx = 1.4f, cy = 0, rot = 0;
    float operator() (float x, float y) const
    {
        const float c = std::cos (rot), s = std::sin (rot);
        const float ar = (x * c - y * s) * zoom + cx, ai = (x * s + y * c) * zoom + cy;
        float zr = 0, zi = 0;
        constexpr int maxIt = 60;
        for (int i = 0; i < maxIt; ++i)
        {
            // ((z^2 + c - 1) / (2z + c - 2))^2
            const float nr = zr * zr - zi * zi + ar - 1, ni = 2 * zr * zi + ai;
            const float dr = 2 * zr + ar - 2, di = 2 * zi + ai, dd = dr * dr + di * di + 1e-12f;
            const float qr = (nr * dr + ni * di) / dd, qi = (ni * dr - nr * di) / dd;
            zr = qr * qr - qi * qi; zi = 2 * qr * qi;
            if (zr * zr + zi * zi > 1e4f) return 0.1f + 0.5f * std::pow ((float) i / maxIt, 0.5f);
            const float ex = zr - 1, ey = zi;
            if (ex * ex + ey * ey < 1e-5f) return 0.25f + 0.75f * std::pow (clamp01 ((float) i / 24.0f), 0.7f);
        }
        return 0.0f;
    }
};

/** Lyapunov fractal: the logistic map driven by a pattern of two rates (one per axis). Stable = lit, chaos = dark. */
struct Lyapunov
{
    float a0 = 3.4f, b0 = 3.4f, span = 0.6f, rot = 0;
    unsigned pattern = 0b0110;   // 1 = use b, read from bit 0 up
    int patternLen = 4;
    float operator() (float x, float y) const
    {
        const float c = std::cos (rot), s = std::sin (rot);
        const float ra = std::clamp (a0 + (x * c - y * s) * span, 0.01f, 4.0f), rb = std::clamp (b0 + (x * s + y * c) * span, 0.01f, 4.0f);
        float v = 0.5f, sum = 0;
        constexpr int warm = 24, n = 72;
        for (int i = 0; i < warm + n; ++i)
        {
            const float r = ((pattern >> (i % patternLen)) & 1u) ? rb : ra;
            v = r * v * (1 - v);
            if (i >= warm) sum += std::log (std::abs (r * (1 - 2 * v)) + 1e-6f);
        }
        const float lambda = sum / n;
        if (lambda < 0) return 0.25f + 0.75f * clamp01 (std::pow (-lambda * 0.9f, 0.6f));
        return 0.22f * std::exp (-lambda * 4);
    }
};

/** Pickover stalks: a Julia set lit by how close each orbit passes to the two axes. */
struct Stalks
{
    float cr = -0.75f, ci = 0.12f, zoom = 1.5f, rot = 0, width = 30;
    float operator() (float x, float y) const
    {
        const float c = std::cos (rot), s = std::sin (rot);
        float zr = (x * c - y * s) * zoom, zi = (x * s + y * c) * zoom;
        float trap = 1e9f;
        constexpr int maxIt = 40;
        int i = 0;
        for (; i < maxIt && zr * zr + zi * zi < 64; ++i)
        {
            const float nr = zr * zr - zi * zi + cr; zi = 2 * zr * zi + ci; zr = nr;
            trap = std::min (trap, std::min (std::abs (zr), std::abs (zi)));
        }
        const float stalk = std::exp (-trap * width);
        return clamp01 (0.08f + 0.92f * stalk) * (i == maxIt ? 0.75f : 1.0f);
    }
};

// ============================================================ 3D, ray marched

/** A camera looking at the origin from a point turning round it. */
struct Camera
{
    V3 eye { 0, 0, -3 }, fwd { 0, 0, 1 }, right { 1, 0, 0 }, up { 0, 1, 0 };
    float lens = 1.6f;
    void look (V3 from, V3 at, float roll = 0)
    {
        eye = from; fwd = (at - from).norm();
        V3 wup { std::sin (roll), std::cos (roll), 0 };
        right = V3 { wup.y * fwd.z - wup.z * fwd.y, wup.z * fwd.x - wup.x * fwd.z, wup.x * fwd.y - wup.y * fwd.x }.norm();
        up = { fwd.y * right.z - fwd.z * right.y, fwd.z * right.x - fwd.x * right.z, fwd.x * right.y - fwd.y * right.x };
    }
    V3 ray (float x, float y) const { return (fwd * lens + right * x - up * y).norm(); }
};

/** Marches a ray into a distance field and shades what it hits: light from one side, darker in the cracks,
    a glow round the edges for the ones it just misses, and the orbit trap mixed in so palettes get bands. */
template <class DE>
float march (const DE& de, const Camera& cam, float x, float y, int steps, float maxDist, float glowAmt, V3 light, float trapMix, float hitScale = 0.0025f)
{
    const V3 d = cam.ray (x, y);
    float t = 0, closest = 1e9f, trap = 0;
    int i = 0;
    for (; i < steps; ++i)
    {
        const V3 p = cam.eye + d * t;
        const float h = de (p, trap);
        closest = std::min (closest, h / std::max (t, 0.05f));
        if (h < hitScale * std::max (t, 0.2f))
        {
            constexpr float e = 0.002f;
            float dummy;
            const V3 n = V3 { de (p + V3 { e, 0, 0 }, dummy) - de (p - V3 { e, 0, 0 }, dummy),
                              de (p + V3 { 0, e, 0 }, dummy) - de (p - V3 { 0, e, 0 }, dummy),
                              de (p + V3 { 0, 0, e }, dummy) - de (p - V3 { 0, 0, e }, dummy) }.norm();
            const float diff = std::max (0.0f, n.dot (light)) * 0.7f + 0.3f;
            const float ao = 1 - (float) i / (float) steps;
            const float fog = 1 / (1 + t * t * 0.04f);
            const float base = diff * (0.35f + 0.65f * ao * ao) * fog;
            return clamp01 (mixf (base, base * (0.45f + 0.75f * clamp01 (trap)), trapMix));
        }
        t += h * 0.9f;
        if (t > maxDist) break;
    }
    return clamp01 (glowAmt * std::exp (-closest * 18.0f) * 0.55f);
}

/** The Mandelbulb, power 8, without trig (Inigo Quilez's polynomial form). trap = how far out the orbit got. */
struct Mandelbulb
{
    float morph = 0;   // nudges the formula off the round bulb
    float operator() (V3 p, float& trap) const
    {
        V3 w = p;
        float m = w.dot (w), dz = 1;
        trap = m;
        for (int i = 0; i < 4; ++i)
        {
            const float m2 = m * m, m4 = m2 * m2;
            dz = 8 * std::sqrt (m4 * m2 * m) * dz + 1;
            const float x = w.x, x2 = x * x, x4 = x2 * x2;
            const float y = w.y, y2 = y * y, y4 = y2 * y2;
            const float z = w.z, z2 = z * z, z4 = z2 * z2;
            const float k3 = x2 + z2, k2 = 1 / std::sqrt (k3 * k3 * k3 * k3 * k3 * k3 * k3 + 1e-20f);
            const float k1 = x4 + y4 + z4 - 6 * y2 * z2 - 6 * x2 * y2 + 2 * z2 * x2;
            const float k4 = x2 - y2 + z2;
            w.x = p.x + 64 * x * y * z * (x2 - z2) * k4 * (x4 - 6 * x2 * z2 + z4) * k1 * k2;
            w.y = p.y + -16 * y2 * k3 * k4 * k4 + k1 * k1 + morph * y;
            w.z = p.z + -8 * y * k4 * (x4 * x4 - 28 * x4 * x2 * z2 + 70 * x4 * z4 - 28 * x2 * z2 * z4 + z4 * z4) * k1 * k2;
            m = w.dot (w);
            trap = std::min (trap, m);
            if (m > 256) break;
        }
        trap = std::sqrt (trap);
        return 0.25f * std::log (m) * std::sqrt (m) / dz;
    }
};

/** The Mandelbox: box folds and sphere folds. The scale changes everything. */
struct Mandelbox
{
    float scale = -1.8f, minR2 = 0.25f;
    float operator() (V3 p, float& trap) const
    {
        V3 z = p;
        float dr = 1;
        trap = 1e9f;
        for (int i = 0; i < 9; ++i)
        {
            z.x = std::clamp (z.x, -1.0f, 1.0f) * 2 - z.x;
            z.y = std::clamp (z.y, -1.0f, 1.0f) * 2 - z.y;
            z.z = std::clamp (z.z, -1.0f, 1.0f) * 2 - z.z;
            const float r2 = z.dot (z);
            if (r2 < minR2) { const float k = 1 / minR2; z = z * k; dr *= k; }
            else if (r2 < 1) { const float k = 1 / r2; z = z * k; dr *= k; }
            z = z * scale + p;
            dr = dr * std::abs (scale) + 1;
            trap = std::min (trap, r2);
        }
        trap = clamp01 (std::sqrt (trap));
        return z.len() / std::abs (dr);
    }
};

/** A Menger sponge that goes on forever in every direction, for flying down its tunnels (Inigo Quilez's form).
    The tunnels run along the axes through every point where two of the coordinates are even. */
struct Menger
{
    float twist = 0;
    float operator() (V3 p, float& trap) const
    {
        float d = -1e9f, s = 1;
        trap = 0;
        for (int m = 0; m < 4; ++m)
        {
            if (twist != 0 && m == 2) p = rotZ (p, twist);
            auto md = [] (float v) { return v - 2 * std::floor (v * 0.5f) - 1; };
            const float ax = md (p.x * s), ay = md (p.y * s), az = md (p.z * s);
            s *= 3;
            const float rx = std::abs (1 - 3 * std::abs (ax)), ry = std::abs (1 - 3 * std::abs (ay)), rz = std::abs (1 - 3 * std::abs (az));
            const float da = std::max (rx, ry), db = std::max (ry, rz), dc = std::max (rz, rx);
            const float c = (std::min ({ da, db, dc }) - 1) / s;
            if (c > d) { d = c; trap = (float) m / 3.0f; }
        }
        return d;
    }
};

/** Apollonian-style inversion fractal (a sphere packing): fold, invert, repeat. */
struct Apollonian
{
    float k = 1.15f;
    float operator() (V3 p, float& trap) const
    {
        float scale = 1;
        trap = 1e9f;
        for (int i = 0; i < 8; ++i)
        {
            auto wrap = [] (float v) { return v - 2 * std::floor (v * 0.5f + 0.5f); };
            p = { wrap (p.x), wrap (p.y), wrap (p.z) };
            const float r2 = p.dot (p);
            trap = std::min (trap, r2);
            const float s = k / std::max (r2, 1e-6f);
            p = p * s;
            scale *= s;
        }
        trap = clamp01 (std::sqrt (trap));
        return 0.25f * std::abs (p.y) / scale;
    }
};

/** A Sierpinski tetrahedron whose folds turn as it goes (a kaleidoscopic IFS). */
struct Tetra
{
    float a1 = 0, a2 = 0, scale = 2;
    float operator() (V3 p, float& trap) const
    {
        trap = 0;
        int n = 0;
        for (; n < 9; ++n)
        {
            if (p.x + p.y < 0) { const float t = -p.y; p.y = -p.x; p.x = t; }
            if (p.x + p.z < 0) { const float t = -p.z; p.z = -p.x; p.x = t; }
            if (p.y + p.z < 0) { const float t = -p.z; p.z = -p.y; p.y = t; }
            p = rotY (rotX (p, a1), a2);
            p = p * scale - V3 { 1, 1, 1 } * (scale - 1);
            trap += std::abs (p.x - p.y) < 0.5f ? 0.12f : 0;
        }
        trap = clamp01 (trap);
        return p.len() * std::pow (scale, (float) -n);
    }
};

/** Where a distance field's shape is and how big: samples a grid and averages the points on or in it. */
template <class DE>
V3 centreOf (const DE& de, float extent, int n, float& radius)
{
    V3 sum; int hits = 0; float trap;
    std::vector<V3> pts;
    for (int i = 0; i < n; ++i) for (int j = 0; j < n; ++j) for (int k = 0; k < n; ++k)
    {
        const V3 p { (i + 0.5f) / n * 2 * extent - extent, (j + 0.5f) / n * 2 * extent - extent, (k + 0.5f) / n * 2 * extent - extent };
        if (de (p, trap) < extent / n) { sum = sum + p; ++hits; pts.push_back (p); }
    }
    if (hits == 0) { radius = 1; return {}; }
    const V3 c = sum * (1.0f / hits);
    radius = 0;
    for (auto& p : pts) radius = std::max (radius, (p - c).len());
    return c;
}

// ============================================================ points (density)

/** Brightness grid for things drawn from many points: plot, fade, then read with a log curve. */
struct Density
{
    int W = 0, H = 0;
    std::vector<float> d;
    void size (int w, int h) { if (w != W || h != H) { W = w; H = h; d.assign ((size_t) w * (size_t) h, 0.0f); } }
    void fade (float k) { for (auto& v : d) v *= k; }
    void clear() { std::fill (d.begin(), d.end(), 0.0f); }
    /** x, y in the kernels' space: y from -1 to 1 */
    void plot (float x, float y, float amt)
    {
        const float hh = H * 0.5f;
        const int ix = (int) (x * hh + W * 0.5f), iy = (int) (y * hh + hh);
        if (ix >= 0 && iy >= 0 && ix < W && iy < H) d[(size_t) iy * (size_t) W + (size_t) ix] += amt;
    }
    /** gain against the typical lit pixel, so sparse and dense pictures both come out well exposed */
    float normaliser (float gain) const
    {
        double sum = 0; int lit = 0;
        for (auto v : d) if (v > 0.01f) { sum += v; ++lit; }
        const float typical = lit > 0 ? (float) (sum / lit) : 1.0f;
        return 1.0f / std::log1p (typical * gain * 3);
    }
    /** the brightness 99% of the lit pixels stay under; for pictures with a few very hot spots */
    float percentile (float q) const
    {
        std::vector<float> lit; lit.reserve (d.size() / 4);
        for (auto v : d) if (v > 0) lit.push_back (v);
        if (lit.empty()) return 1;
        const size_t k = (size_t) (q * (float) (lit.size() - 1));
        std::nth_element (lit.begin(), lit.begin() + (long) k, lit.end());
        return std::max (lit[k], 1e-6f);
    }
    float at (int x, int y, float gain, float norm) const { return std::sqrt (clamp01 (std::log1p (d[(size_t) y * (size_t) W + (size_t) x] * gain) * norm)); }
};

/** Barnsley's fern from its four maps; bend leans it, curl rolls the fronds. */
struct Fern
{
    float bend = 0, curl = 0, x = 0, y = 0;
    uint32_t rng = 12345;
    float rnd() { rng = rng * 1664525u + 1013904223u; return (float) (rng >> 8) / 16777216.0f; }
    void run (Density& den, int n, float sc, float ox, float oy, float amt)
    {
        const float c = std::cos (curl), s = std::sin (curl);
        for (int i = 0; i < n; ++i)
        {
            const float r = rnd();
            float nx, ny;
            if (r < 0.01f) { nx = 0; ny = 0.16f * y; }
            else if (r < 0.86f)
            {
                // the main map, turned by curl (rolls the fronds) and sheared by bend (leans the stem)
                const float ma = 0.85f, mb = 0.04f + bend, mc = -0.04f - bend * 0.5f, md = 0.85f;
                const float ra = c * ma - s * mc, rb = c * mb - s * md, rc = s * ma + c * mc, rd = s * mb + c * md;
                nx = ra * x + rb * y; ny = rc * x + rd * y + 1.6f;
            }
            else if (r < 0.93f) { nx = 0.2f * x - 0.26f * y; ny = 0.23f * x + 0.22f * y + 1.6f; }
            else { nx = -0.15f * x + 0.28f * y; ny = 0.26f * x + 0.24f * y + 0.44f; }
            x = nx; y = ny;
            if (i > 10) den.plot (ox + x * sc, oy - y * sc, amt);
        }
    }
};

/** Martin's Hopalong attractor: a field of dots in rings and petals. */
struct Hopalong
{
    float a = 2.0f, b = 0.5f, c = 1.2f, x = 0, y = 0;
    void run (Density& den, int n, float sc, float rot)
    {
        const float cr = std::cos (rot), sr = std::sin (rot);
        x = y = 0;
        for (int i = 0; i < n; ++i)
        {
            const float nx = y - (x < 0 ? -1.0f : 1.0f) * std::sqrt (std::abs (b * x - c)), ny = a - x;
            x = nx; y = ny;
            den.plot ((x * cr - y * sr) * sc, (x * sr + y * cr) * sc, 1);
        }
    }
};

/** Thomas's cyclically symmetric attractor: many particles drifting through it, seen turning in 3D. */
struct Thomas
{
    float b = 0.19f;
    std::vector<V3> pts;
    uint32_t rng = 99;
    float rnd() { rng = rng * 1664525u + 1013904223u; return (float) (rng >> 8) / 16777216.0f; }
    void seed (int n) { pts.resize ((size_t) n); for (auto& p : pts) p = { rnd() * 6 - 3, rnd() * 6 - 3, rnd() * 6 - 3 }; }
    void step (float h)
    {
        for (auto& p : pts)
        {
            const V3 d { std::sin (p.y) - b * p.x, std::sin (p.z) - b * p.y, std::sin (p.x) - b * p.z };
            p = p + d * h;
        }
    }
    void draw (Density& den, float yaw, float pitch, float sc)
    {
        for (auto& p : pts)
        {
            const V3 q = rotX (rotY (p, yaw), pitch);
            const float persp = 1 / (1 + q.z * 0.06f);
            den.plot (q.x * sc * persp, q.y * sc * persp, persp);
        }
    }
};

/** The Buddhabrot: the paths of points that escape the Mandelbrot set, piled up. */
struct Buddha
{
    uint32_t rng = 7;
    float rnd() { rng = rng * 1664525u + 1013904223u; return (float) (rng >> 8) / 16777216.0f; }
    std::vector<float> orbit;
    void run (Density& den, int samples, int maxIt, float sc, float rot, float wobble)
    {
        const float cr = std::cos (rot), sr = std::sin (rot);
        orbit.resize ((size_t) maxIt * 2);
        for (int s = 0; s < samples; ++s)
        {
            const float ar = rnd() * 3.2f - 2.1f, ai = rnd() * 3.0f - 1.5f;
            // skip the two big bulbs: they never escape
            const float q = (ar - 0.25f) * (ar - 0.25f) + ai * ai;
            if (q * (q + (ar - 0.25f)) < 0.25f * ai * ai || (ar + 1) * (ar + 1) + ai * ai < 0.0625f) continue;
            float zr = 0, zi = 0;
            int i = 0;
            for (; i < maxIt; ++i)
            {
                const float nr = zr * zr - zi * zi + ar, ni = 2 * zr * zi + ai + wobble * zr;
                zr = nr; zi = ni;
                orbit[(size_t) i * 2] = zr; orbit[(size_t) i * 2 + 1] = zi;
                if (zr * zr + zi * zi > 4) break;
            }
            if (i == maxIt || i < 14) continue;
            for (int j = 0; j < i; ++j)
            {
                // turned so the "body" stands upright
                const float px = orbit[(size_t) j * 2 + 1], py = -orbit[(size_t) j * 2] - 0.4f;
                den.plot ((px * cr - py * sr) * sc, (px * sr + py * cr) * sc, 1);
            }
        }
    }
};
}
