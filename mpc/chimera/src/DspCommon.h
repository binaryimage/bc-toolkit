#pragma once
#include <cmath>
#include <vector>
#include <algorithm>
#include <cstdint>

namespace chimera
{
constexpr float kPi    = 3.14159265358979f;
constexpr float kTwoPi = 6.28318530717959f;

inline float clampf (float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float lerp (float a, float b, float t)     { return a + (b - a) * t; }
inline float dbToGain (float db)                  { return db <= -59.9f ? 0.0f : std::pow (10.0f, db * 0.05f); }

// Cheap, smooth soft clipper (rational tanh approximation, exact at 0, ~±1 at the rails)
inline float softClip (float x)
{
    x = clampf (x, -3.0f, 3.0f);
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

// Flush denormals in long feedback loops
inline float undenormal (float x) { return std::abs (x) < 1.0e-20f ? 0.0f : x; }

//==============================================================================
// Circular delay buffer with fractional (cubic Hermite) reads.
class DelayLine
{
public:
    void prepare (int maxSamples)
    {
        int size = 1;
        while (size < maxSamples + 4) size <<= 1;
        buffer.assign ((size_t) size, 0.0f);
        mask = size - 1;
        writePos = 0;
    }
    void clear()                     { std::fill (buffer.begin(), buffer.end(), 0.0f); }
    void push (float x)              { buffer[(size_t) writePos] = x; writePos = (writePos + 1) & mask; }
    int  capacity() const            { return mask - 3; }

    // delay in samples, measured from the most recently pushed sample (1 = last sample)
    float read (float delay) const
    {
        delay = clampf (delay, 2.0f, (float) (mask - 3));
        const float pos  = (float) writePos - delay;
        const float fl   = std::floor (pos);
        const float frac = pos - fl;
        const int   i1   = ((int) fl) & mask;
        const int   i0   = (i1 - 1) & mask;
        const int   i2   = (i1 + 1) & mask;
        const int   i3   = (i1 + 2) & mask;
        const float y0 = buffer[(size_t) i0], y1 = buffer[(size_t) i1], y2 = buffer[(size_t) i2], y3 = buffer[(size_t) i3];
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * frac + c2) * frac + c1) * frac + y1;
    }

    float readInt (int delay) const { return buffer[(size_t) ((writePos - delay) & mask)]; }

private:
    std::vector<float> buffer;
    int mask = 0, writePos = 0;
};

//==============================================================================
// One-pole low-pass (also gives a high-pass as input - lowpass)
struct OnePole
{
    float a = 0.0f, z = 0.0f;
    void setCutoff (float hz, float sampleRate) { a = std::exp (-kTwoPi * clampf (hz, 5.0f, sampleRate * 0.49f) / sampleRate); }
    float lp (float x) { z = x + a * (z - x); z = undenormal (z); return z; }
    float hp (float x) { return x - lp (x); }
    void reset() { z = 0.0f; }
};

// Two cascaded one-poles: gentle 12 dB/oct darkening for BBD / tape character
struct TwoPole
{
    OnePole p1, p2;
    void setCutoff (float hz, float sr) { p1.setCutoff (hz, sr); p2.setCutoff (hz, sr); }
    float process (float x) { return p2.lp (p1.lp (x)); }
    void reset() { p1.reset(); p2.reset(); }
};

// Parameter smoother (exponential)
struct Smooth
{
    float value = 0.0f, target = 0.0f, coeff = 0.0f;
    void prepare (float timeMs, float sampleRate) { coeff = std::exp (-1.0f / (0.001f * timeMs * sampleRate)); }
    void snap (float v) { value = target = v; }
    float next() { value = target + coeff * (value - target); return value; }
};

// Schroeder all-pass diffuser on a delay line
struct AllPass
{
    DelayLine line;
    float delaySamples = 1.0f, g = 0.5f;
    void prepare (int maxSamples) { line.prepare (maxSamples); }
    float process (float x, float modOffset = 0.0f)
    {
        const float d = line.read (delaySamples + modOffset);
        const float v = x + g * d;
        line.push (undenormal (v));
        return d - g * v;
    }
    void clear() { line.clear(); }
};

// Small, fast white noise (xorshift)
struct Noise
{
    uint32_t state = 0x12345678u;
    float next()
    {
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        return (float) (int32_t) state * (1.0f / 2147483648.0f);
    }
};
} // namespace chimera
