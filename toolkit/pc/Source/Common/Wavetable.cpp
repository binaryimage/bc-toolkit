#include "Wavetable.h"
#include "Dsp.h"
#include <cmath>
#include <random>

namespace tk::wt
{
const char* const kShapeNames[6] = { "Sine", "Triangle", "Saw", "Square", "Pulse", "Noise" };

static void fftInPlace (std::vector<float>& re, std::vector<float>& im, bool inverse)
{
    const int n = (int) re.size();
    for (int i = 1, j = 0; i < n; ++i)
    {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { std::swap (re[(size_t) i], re[(size_t) j]); std::swap (im[(size_t) i], im[(size_t) j]); }
    }
    for (int len = 2; len <= n; len <<= 1)
    {
        double ang = (inverse ? 2 : -2) * juce::MathConstants<double>::pi / len;
        double wr = std::cos (ang), wi = std::sin (ang);
        for (int i = 0; i < n; i += len)
        {
            double cr = 1, ci = 0;
            for (int k = 0; k < len / 2; ++k)
            {
                size_t a = (size_t) (i + k), b = a + (size_t) len / 2;
                double xr = re[b] * cr - im[b] * ci, xi = re[b] * ci + im[b] * cr;
                re[b] = (float) (re[a] - xr); im[b] = (float) (im[a] - xi);
                re[a] = (float) (re[a] + xr); im[a] = (float) (im[a] + xi);
                double nr = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = nr;
            }
        }
    }
    if (inverse) for (int i = 0; i < n; ++i) { re[(size_t) i] /= n; im[(size_t) i] /= n; }
}

Frame shape (int kind)
{
    Frame w ((size_t) N);
    std::mt19937 rng (7);
    std::uniform_real_distribution<float> u (-0.8f, 0.8f);
    for (int i = 0; i < N; ++i)
    {
        double ph = i / (double) N, v;
        switch (kind)
        {
            case 1: v = ph < 0.25 ? 4 * ph : ph < 0.75 ? 2 - 4 * ph : 4 * ph - 4; break;
            case 2: v = 1 - 2 * ph; break;
            case 3: v = ph < 0.5 ? 0.9 : -0.9; break;
            case 4: v = ph < 0.2 ? 0.9 : -0.9; break;
            case 5: v = u (rng); break;
            default: v = std::sin (2 * juce::MathConstants<double>::pi * ph) * 0.95;
        }
        w[(size_t) i] = (float) v;
    }
    if (kind >= 4) removeDc (w);
    if (kind == 5) bandlimit (w, 96);
    return w;
}

void removeDc (Frame& w)
{
    double m = 0;
    for (float v : w) m += v;
    m /= (double) w.size();
    for (auto& v : w) v = (float) (v - m);
}

void normalize (Frame& w)
{
    float pk = 0;
    for (float v : w) pk = std::max (pk, std::abs (v));
    if (pk > 1e-6f) for (auto& v : w) v = v / pk * 0.98f;
}

void bandlimit (std::vector<float>& w, int h)
{
    const int n = (int) w.size();
    std::vector<float> re (w), im ((size_t) n, 0.0f);
    fftInPlace (re, im, false);
    re[0] = im[0] = 0;
    for (int k = h + 1; k <= n - h - 1; ++k) re[(size_t) k] = im[(size_t) k] = 0;
    fftInPlace (re, im, true);
    w = re;
}

void smooth (Frame& w)
{
    std::vector<float> re (w), im ((size_t) N, 0.0f);
    fftInPlace (re, im, false);
    for (int k = 1; k < N / 2; ++k)
    {
        float g = (float) (1.0 / (1.0 + std::pow (k / 40.0, 4)));
        re[(size_t) k] *= g; im[(size_t) k] *= g; re[(size_t) (N - k)] *= g; im[(size_t) (N - k)] *= g;
    }
    re[(size_t) N / 2] = im[(size_t) N / 2] = 0;
    fftInPlace (re, im, true);
    w = re;
}

void fold (Frame& w) { for (auto& v : w) v = (float) std::sin (v * juce::MathConstants<double>::pi * 0.9); }
void mirror (Frame& w) { for (int i = 0; i < N / 2; ++i) w[(size_t) (N / 2 + i)] = -w[(size_t) i]; }
void crush (Frame& w) { for (auto& v : w) v = std::round (v * 7) / 7.0f; }

Frame resampleCycle (const float* src, int len, int n)
{
    Frame o ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        double x = i * (double) len / n;
        int a = (int) std::floor (x);
        float t = (float) (x - a);
        float p0 = src[(a - 1 + len) % len], p1 = src[a % len], p2 = src[(a + 1) % len], p3 = src[(a + 2) % len];
        o[(size_t) i] = 0.5f * (2 * p1 + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t * t + (-p0 + 3 * p1 - 3 * p2 + p3) * t * t * t);
    }
    return o;
}

std::vector<Frame> render (const std::vector<Frame>& keys, int total)
{
    std::vector<Frame> out ((size_t) total, Frame ((size_t) N, 0.0f));
    int k = (int) keys.size();
    if (k == 0) return out;
    for (int f = 0; f < total; ++f)
    {
        if (k == 1) { out[(size_t) f] = keys[0]; continue; }
        double pos = total == 1 ? 0 : f * (k - 1) / (double) (total - 1);
        int a = std::min (k - 2, (int) std::floor (pos));
        float fr = (float) (pos - a);
        for (int i = 0; i < N; ++i) out[(size_t) f][(size_t) i] = keys[(size_t) a][(size_t) i] + (keys[(size_t) a + 1][(size_t) i] - keys[(size_t) a][(size_t) i]) * fr;
    }
    return out;
}

juce::MemoryBlock wav (const std::vector<Frame>& frames, int pts, int bits)
{
    std::vector<std::vector<float>> fs;
    float pk = 0;
    for (auto w : frames)
    {
        removeDc (w);
        if (pts < N)
        {
            bandlimit (w, pts / 2 - 1);
            std::vector<float> d ((size_t) pts);
            int stride = N / pts;
            for (int i = 0; i < pts; ++i) d[(size_t) i] = w[(size_t) (i * stride)];
            w = d;
        }
        for (float v : w) pk = std::max (pk, std::abs (v));
        fs.push_back (w);
    }
    float gain = pk > 1e-6f ? 0.98f / pk : 1.0f;
    int samples = (int) fs.size() * pts, bpsm = bits / 8, data = samples * bpsm;
    juce::MemoryOutputStream o;
    o.write ("RIFF", 4); o.writeInt (36 + data); o.write ("WAVE", 4);
    o.write ("fmt ", 4); o.writeInt (16); o.writeShort (1); o.writeShort (1); o.writeInt (44100); o.writeInt (44100 * bpsm); o.writeShort ((short) bpsm); o.writeShort ((short) bits);
    o.write ("data", 4); o.writeInt (data);
    for (auto& w : fs)
        for (float v : w)
        {
            double x = juce::jlimit (-1.0, 1.0, (double) v * gain);
            if (bits == 24) { int s = (int) std::lround (x * 8388607); o.writeByte ((char) (s & 0xff)); o.writeByte ((char) ((s >> 8) & 0xff)); o.writeByte ((char) ((s >> 16) & 0xff)); }
            else o.writeShort ((short) std::lround (x * 32767));
        }
    return o.getMemoryBlock();
}

static int detectFrame (const std::vector<float>& s)
{
    std::vector<float> re ((size_t) N, 0.0f), im ((size_t) N, 0.0f);
    for (int i = 0; i < std::min ((int) s.size(), N); ++i) re[(size_t) i] = s[(size_t) i];
    fftInPlace (re, im, false);
    int best = N; double bestMag = -1;
    for (int k = 1; k <= 8; k <<= 1)
    {
        int len = N / k;
        if (s.size() % (size_t) len != 0) continue;
        double m = std::hypot (re[(size_t) k], im[(size_t) k]);
        if (m > bestMag * 1.5) { bestMag = m; best = len; }
    }
    return best;
}

std::vector<Frame> framesFromSamples (const std::vector<float>& s)
{
    std::vector<Frame> out;
    int n = (int) s.size();
    int frameLen = (n >= 256 && n % 256 == 0 && n > 8192) || (n % N == 0 && n > N) ? detectFrame (s)
                 : (n <= 8192 && n >= 16) ? n : -1;
    if (frameLen < 0) return out;
    int frames = n / frameLen, keep = std::min (frames, 64);
    for (int k = 0; k < keep; ++k)
    {
        int f = keep == 1 ? 0 : (int) std::lround (k * (frames - 1) / (double) (keep - 1));
        Frame w = frameLen == N ? Frame (s.begin() + f * frameLen, s.begin() + (f + 1) * frameLen) : resampleCycle (s.data() + f * frameLen, frameLen, N);
        removeDc (w);
        out.push_back (w);
    }
    return out;
}

std::vector<Frame> framesFromRecording (const juce::AudioBuffer<float>& b, double sr, int count, double* detected)
{
    std::vector<Frame> out;
    auto mono = mixToMono (b);
    const float* x = mono.getReadPointer (0);
    int len = mono.getNumSamples();
    // overall pitch from a few windows (median)
    std::vector<double> hz;
    for (int pos = 0; pos + 4096 < len; pos += std::max (4096, len / 12))
    {
        double h = yinPitch (x + pos, 4096, sr);
        if (h > 0) hz.push_back (h);
    }
    if (hz.empty()) return out;
    std::sort (hz.begin(), hz.end());
    double f0 = hz[hz.size() / 2];
    if (detected != nullptr) *detected = f0;
    double period = sr / f0;
    int first = std::max (0, firstAbove (mono, 0.02f));
    int last = std::max (first + 1, lastAbove (mono, 0.02f) - (int) (period * 3));
    for (int k = 0; k < count; ++k)
    {
        int pos = first + (int) ((last - first) * (count == 1 ? 0.3 : k / (double) (count - 1)));
        // align to a rising zero crossing
        int start = pos;
        for (int i = pos; i < std::min (len - 1, pos + (int) period); ++i) if (x[i] < 0 && x[i + 1] >= 0) { start = i + 1; break; }
        if (start + (int) period * 2 >= len) break;
        // average two consecutive cycles for a cleaner frame
        Frame w ((size_t) N, 0.0f);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < N; ++i)
            {
                double p = start + (c + i / (double) N) * period;
                int a = (int) p; float t = (float) (p - a);
                w[(size_t) i] += (x[a] + (x[std::min (len - 1, a + 1)] - x[a]) * t) * 0.5f;
            }
        removeDc (w);
        normalize (w);
        out.push_back (w);
    }
    return out;
}
}
