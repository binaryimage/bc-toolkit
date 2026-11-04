#include "Analysis.h"

namespace bcv
{
Analyzer::Analyzer()
{
    window.resize (2048);
    for (int i = 0; i < 2048; ++i) window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * i / 2047.0f);
    work.resize (4096);
    reset();
}

void Analyzer::reset()
{
    prev.fill (0); smooth.fill (0); fluxHistory.clear();
    lastBeatT = -10; lastT = -1; beat = energy = bass = mid = treble = 0;
    zFloor.fill (1); zCeil.fill (0); zPrev.fill (0); zFluxAvg.fill (0.02f); zHit.fill (0); zLevel.fill (0); zLastHit.fill (-10);
    gFloor.fill (1); gCeil.fill (0);
    noisiness = brightness = width = pan = pitchMove = pitchRaw = drone = attack = density = sparse = crunch = tail = silence = change = drop = prevRms = fallRate = 0;
    tail = 0.5f; lastCentroid = -1; section = 0; lastChangeT = lastDropT = -10;
    hitTimes.clear(); energyHist.clear(); profileHist.clear();
}

void Analyzer::makeEdges (double sr)
{
    edgeRate = sr;
    const double lo = 30, hi = juce::jmin (16000.0, sr * 0.45);
    for (int b = 0; b <= kBands; ++b)
    {
        double hz = lo * std::pow (hi / lo, (double) b / kBands);
        edges[(size_t) b] = juce::jlimit (1, 1023, (int) std::round (hz / sr * 2048));
    }
    for (int b = 1; b <= kBands; ++b) if (edges[(size_t) b] <= edges[(size_t) b - 1]) edges[(size_t) b] = juce::jmin (1024, edges[(size_t) b - 1] + 1);
}

/** follow a value's recent floor and ceiling and return where it sits between them (0..1) */
static float autoLevel (float v, float& floor, float& ceil, float dt)
{
    if (v > ceil) ceil = v; else ceil -= (ceil - v) * juce::jmin (1.0f, dt * 0.15f);
    if (v < floor) floor = v; else floor += (v - floor) * juce::jmin (1.0f, dt * 0.08f);
    float lv = juce::jlimit (0.0f, 1.0f, (v - floor) / juce::jmax (0.12f, ceil - floor));
    if (v < 0.05f) lv *= v / 0.05f;   // don't blow up the noise floor of a silent range
    return lv;
}

static float follow (float cur, float target, float rate, float dt) { return cur + (target - cur) * juce::jmin (1.0f, dt * rate); }

void Analyzer::analyse (const juce::AudioBuffer<float>& buf, double sr, double t, double fps, Frame& f)
{
    if (sr != edgeRate) makeEdges (sr);
    if (lastT >= 0 && (t < lastT || t - lastT > 1.0)) reset();   // jumped: start fresh
    const double dt = lastT < 0 ? 1.0 / fps : juce::jmax (1e-3, t - lastT);
    const float fdt = (float) dt;
    lastT = t;
    f.t = t; f.dt = dt;
    const int n = buf.getNumSamples(), chans = buf.getNumChannels();
    const int end = (int) (t * sr);
    auto sample = [&] (int ch, int i) -> float { return (i >= 0 && i < n && chans > 0) ? buf.getSample (juce::jmin (ch, chans - 1), i) : 0.0f; };

    // waveform: the last kWave samples (+ stereo picture)
    float pk = 0; double sq = 0, sqL = 0, sqR = 0, sqM = 0, sqS = 0;
    for (int i = 0; i < kWave; ++i)
    {
        float l = sample (0, end - kWave + i), r = sample (1, end - kWave + i);
        f.L[(size_t) i] = l; f.R[(size_t) i] = r;
        pk = juce::jmax (pk, std::abs (l), std::abs (r));
        sq += 0.5 * (l * l + r * r);
        sqL += l * l; sqR += r * r;
        sqM += 0.25 * (l + r) * (l + r); sqS += 0.25 * (l - r) * (l - r);
    }
    const float rmsA = (float) std::sqrt (sq / kWave);
    f.peak = juce::jlimit (0.0f, 1.0f, pk);
    f.rms = juce::jlimit (0.0f, 1.0f, rmsA * 2.5f);

    // spectrum of the 2048 samples centred just before t
    std::fill (work.begin(), work.end(), 0.0f);
    for (int i = 0; i < 2048; ++i) work[(size_t) i] = 0.5f * (sample (0, end - 2048 + i) + sample (1, end - 2048 + i)) * window[(size_t) i];
    fft.performFrequencyOnlyForwardTransform (work.data());

    std::array<float, kBands> raw {};
    for (int b = 0; b < kBands; ++b)
    {
        float m = 0;
        for (int k = edges[(size_t) b]; k < juce::jmax (edges[(size_t) b] + 1, edges[(size_t) b + 1]); ++k) m = juce::jmax (m, work[(size_t) k]);
        float db = juce::Decibels::gainToDecibels (m / 512.0f, -100.0f);
        db += 3.0f * std::log2 (1.0f + b * 0.12f);           // gentle tilt so the top end isn't always flat on the floor
        raw[(size_t) b] = juce::jlimit (0.0f, 1.0f, (db + 72.0f) / 66.0f);
    }

    // ---- 32 ranges: level, auto-level, sudden jumps
    std::array<float, kZones> zr {};
    f.zoneOnsets = 0;
    for (int z = 0; z < kZones; ++z)
    {
        float v = 0;
        for (int b = z * 4; b < z * 4 + 4; ++b) v = juce::jmax (v, raw[(size_t) b]);
        zr[(size_t) z] = v;
        float lv = autoLevel (v, zFloor[(size_t) z], zCeil[(size_t) z], fdt);
        zLevel[(size_t) z] = lv > zLevel[(size_t) z] ? lv : follow (zLevel[(size_t) z], lv, 7.0f, fdt);
        const float jump = juce::jmax (0.0f, v - zPrev[(size_t) z]);
        zPrev[(size_t) z] = v;
        const bool hit = jump > zFluxAvg[(size_t) z] * 2.2f + 0.035f && t - zLastHit[(size_t) z] > 0.1 && v > 0.08f;
        zFluxAvg[(size_t) z] = follow (zFluxAvg[(size_t) z], jump, 1.0f, fdt);
        if (hit) { zHit[(size_t) z] = 1; zLastHit[(size_t) z] = t; f.zoneOnsets |= 1u << z; }
        else zHit[(size_t) z] *= (float) std::exp (-dt * 9.0);
        f.zones[(size_t) z] = zLevel[(size_t) z];
        f.zoneHit[(size_t) z] = zHit[(size_t) z];
    }

    // ---- the 128 bands: half raw, half levelled against their range, so every style reacts more
    float flux = 0;
    const float attackK = 0.6f, release = (float) std::exp (-dt * 6.0);
    for (int b = 0; b < kBands; ++b)
    {
        const int z = b / 4;
        float lv = juce::jlimit (0.0f, 1.0f, (raw[(size_t) b] - zFloor[(size_t) z]) / juce::jmax (0.12f, zCeil[(size_t) z] - zFloor[(size_t) z]));
        if (raw[(size_t) b] < 0.05f) lv *= raw[(size_t) b] / 0.05f;
        float v = 0.5f * raw[(size_t) b] + 0.5f * lv;
        f.bands[(size_t) b] = v;
        flux += juce::jmax (0.0f, v - prev[(size_t) b]);
        prev[(size_t) b] = v;
        smooth[(size_t) b] = v > smooth[(size_t) b] ? smooth[(size_t) b] + (v - smooth[(size_t) b]) * attackK : smooth[(size_t) b] * release + v * (1 - release);
    }
    f.smooth = smooth;
    auto group = [&] (int a, int z) { float s = 0; for (int b = a; b < z; ++b) s += smooth[(size_t) b]; return s / (z - a); };
    const float gk = (float) std::exp (-dt * 10.0);
    float gr[3] = { group (0, 26), group (26, 90), group (90, kBands) };
    float* gs[3] = { &bass, &mid, &treble };
    for (int i = 0; i < 3; ++i)
    {
        float lv = autoLevel (gr[i], gFloor[(size_t) i], gCeil[(size_t) i], fdt);
        float target = 0.4f * gr[i] + 0.6f * lv;
        *gs[i] = target > *gs[i] ? target : *gs[i] * gk + target * (1 - gk);   // instant up, quick fall
    }
    f.bass = bass; f.mid = mid; f.treble = treble;
    f.flux = juce::jlimit (0.0f, 1.0f, flux / 12.0f);

    // ---- beats: flux well above its recent average, or a hard jump in the low ranges
    fluxHistory.push_back (flux);
    const size_t keep = (size_t) juce::jmax (8, (int) (fps * 1.5));
    if (fluxHistory.size() > keep) fluxHistory.erase (fluxHistory.begin(), fluxHistory.begin() + (long) (fluxHistory.size() - keep));
    float mean = 0; for (float x : fluxHistory) mean += x; mean /= (float) fluxHistory.size();
    const bool lowHit = (f.zoneOnsets & 0xffu) != 0 && zr[2] + zr[3] + zr[4] > 0.6f;
    f.onset = (flux > mean * 1.4f + 0.5f || lowHit) && t - lastBeatT > 0.15 && f.rms > 0.02f;
    if (f.onset) { lastBeatT = t; beat = 1; }
    else beat *= (float) std::exp (-dt * 7.0);
    f.beat = beat;
    const float ke = (float) std::exp (-dt * 1.5);
    energy = energy * ke + f.rms * (1 - ke);
    f.energy = juce::jlimit (0.0f, 1.0f, energy * 1.4f);

    // ---- character of the sound
    {
        // noisiness: spectral flatness, 1 kHz .. 12 kHz
        const int b0 = juce::jmax (1, (int) (1000.0 / sr * 2048)), b1 = juce::jmin (1023, (int) (12000.0 / sr * 2048));
        double lg = 0, ar = 0; int cnt = 0;
        for (int k = b0; k < b1; ++k) { double m = work[(size_t) k] + 1e-6; lg += std::log (m); ar += m; ++cnt; }
        float flat = cnt > 0 ? (float) (std::exp (lg / cnt) / (ar / cnt)) : 0;
        double all = 0; for (int k = 1; k < 1024; ++k) all += work[(size_t) k];
        const float upShare = all > 1e-6 ? (float) (ar / all) : 0;          // how much of the sound lives up there at all
        noisiness = follow (noisiness, juce::jlimit (0.0f, 1.0f, (flat - 0.1f) / 0.5f) * juce::jlimit (0.0f, 1.0f, upShare * 4) * juce::jlimit (0.0f, 1.0f, rmsA * 20), 4.0f, fdt);

        // brightness: spectral centroid, 200 Hz .. 8 kHz on a log scale
        double num = 0, den = 0;
        for (int k = 2; k < 1024; ++k) { double hz = k * sr / 2048.0; num += hz * work[(size_t) k]; den += work[(size_t) k]; }
        const float centroid = den > 1e-6 ? (float) (num / den) : 200;
        const float logc = std::log2 (juce::jmax (50.0f, centroid));
        brightness = follow (brightness, juce::jlimit (0.0f, 1.0f, (logc - std::log2 (200.0f)) / (std::log2 (8000.0f) - std::log2 (200.0f))), 3.0f, fdt);

        // pitch movement: how fast the centre of the sound is climbing or falling (octaves per second)
        if (rmsA > 0.01f)
        {
            // follow a slow track of the centre, then take how fast that track moves (hits and spikes cancel out)
            const float before = lastCentroid;
            lastCentroid = follow (lastCentroid < 0 ? logc : lastCentroid, logc, 2.0f, fdt);
            if (before > 0) pitchRaw = follow (pitchRaw, (lastCentroid - before) / fdt / 1.2f, 1.5f, fdt);   // average first, clamp after
            pitchMove = juce::jlimit (-1.0f, 1.0f, pitchRaw);
        }
        else { pitchRaw = follow (pitchRaw, 0, 3.0f, fdt); pitchMove = juce::jlimit (-1.0f, 1.0f, pitchRaw); }

        // stereo
        const float m = (float) std::sqrt (sqM / kWave), s = (float) std::sqrt (sqS / kWave);
        width = follow (width, juce::jlimit (0.0f, 1.0f, s / (m + s + 1e-6f) * 2.0f), 3.0f, fdt);
        const float l = (float) std::sqrt (sqL / kWave), r = (float) std::sqrt (sqR / kWave);
        pan = follow (pan, juce::jlimit (-1.0f, 1.0f, (r - l) / (l + r + 1e-6f) * 2.0f), 3.0f, fdt);

        // drone: something steady and present for a while
        const float steady = juce::jlimit (0.0f, 1.0f, 1 - f.flux * 6) * juce::jlimit (0.0f, 1.0f, rmsA * 15);
        drone = steady > drone ? follow (drone, steady, 0.4f, fdt) : follow (drone, steady, 3.0f, fdt);

        // attack: how sharply the level just rose
        const float rise = (rmsA - prevRms) / fdt;
        attack = juce::jmax (juce::jlimit (0.0f, 1.0f, rise / 3.0f), attack * (float) std::exp (-dt * 1.5));

        // tail: when it falls, how slowly
        if (rmsA < prevRms && prevRms > 0.01f)
        {
            float rate = (prevRms - rmsA) / fdt / prevRms;
            fallRate = follow (fallRate, rate, 4.0f, fdt);
            tail = follow (tail, juce::jlimit (0.0f, 1.0f, 1 - fallRate / 6.0f), 1.0f, fdt);
        }
        prevRms = rmsA;

        // crunch: squashed peaks (low crest factor) at a decent level
        const float crest = pk / (rmsA + 1e-6f);
        // (pure tones have a low crest factor too, so it also needs the harmonics that distortion adds)
        crunch = follow (crunch, juce::jlimit (0.0f, 1.0f, (4.0f - crest) / 2.5f) * juce::jlimit (0.0f, 1.0f, rmsA * 10)
                                     * juce::jlimit (0.0f, 1.0f, (brightness - 0.04f) * 5 + noisiness), 3.0f, fdt);

        // busy vs sparse: moments with any hit, per second
        if (f.zoneOnsets != 0) hitTimes.push_back (t);
        while (! hitTimes.empty() && hitTimes.front() < t - 1.0) hitTimes.pop_front();
        density = follow (density, juce::jlimit (0.0f, 1.0f, (float) hitTimes.size() / 8.0f), 3.0f, fdt);
        sparse = follow (sparse, juce::jlimit (0.0f, 1.0f, 1 - density * 1.5f) * juce::jlimit (0.0f, 1.0f, 1 - f.energy * 2), 1.0f, fdt);

        // silence
        silence = juce::jlimit (0.0f, 1.0f, silence + (rmsA < 0.003f ? fdt * 0.8f : -fdt * 4.0f));
    }

    // ---- swells, drops and section changes
    {
        const size_t eKeep = (size_t) juce::jmax (12, (int) (fps * 6));
        energyHist.push_back (rmsA);
        while (energyHist.size() > eKeep) energyHist.pop_front();
        auto avg = [&] (double fromSec, double toSec) {   // mean rms between fromSec and toSec ago
            int a = (int) (fromSec * fps), z = (int) (toSec * fps), N = (int) energyHist.size();
            double s = 0; int c = 0;
            for (int i = juce::jmax (0, N - 1 - z); i <= N - 1 - a && i < N; ++i) { s += energyHist[(size_t) i]; ++c; }
            return c > 0 ? (float) (s / c) : 0.0f;
        };
        const float recent = avg (0, 1.5), older = avg (3, 4.5);
        f.swell = energyHist.size() > (size_t) (fps * 4.5) ? juce::jlimit (0.0f, 1.0f, (recent - older) / (older + 0.03f) * 1.2f) : 0.0f;
        const float shortE = avg (0, 0.5), priorE = avg (0.5, 2.5);
        if (energyHist.size() > (size_t) (fps * 2.5) && shortE > priorE * 1.8f + 0.02f && shortE > 0.06f && t - lastDropT > 4)
        {
            drop = 1; lastDropT = t;
            if (t - lastChangeT > 2) { change = 1; ++section; lastChangeT = t; }
        }
        drop *= (float) std::exp (-dt * 1.2);

        const size_t pKeep = (size_t) juce::jmax (16, (int) (fps * 8));
        profileHist.push_back (zr);
        while (profileHist.size() > pKeep) profileHist.pop_front();
        if (profileHist.size() >= (size_t) (fps * 6) && t - lastChangeT > 4)
        {
            std::array<float, kZones> shortP {}, longP {};
            // compare the last second with what came before it, but only since the last change
            const int N = (int) profileHist.size(), s1 = (int) fps, l0 = (int) (fps * 2);
            const int since = juce::jmax (0, N - (int) ((t - lastChangeT) * fps));
            const int count = N - l0 - since;
            if (count >= (int) (fps * 3))
            {
                for (int i = N - s1; i < N; ++i) for (int z = 0; z < kZones; ++z) shortP[(size_t) z] += profileHist[(size_t) i][(size_t) z] / s1;
                for (int i = since; i < N - l0; ++i) for (int z = 0; z < kZones; ++z) longP[(size_t) z] += profileHist[(size_t) i][(size_t) z] / count;
                float dist = 0;
                for (int z = 0; z < kZones; ++z) dist += std::abs (shortP[(size_t) z] - longP[(size_t) z]);
                if (dist / kZones > 0.11f) { change = 1; ++section; lastChangeT = t; }
            }
        }
        change *= (float) std::exp (-dt * 1.5);
    }

    f.noisiness = noisiness; f.brightness = brightness; f.width = width; f.pan = pan; f.pitchMove = pitchMove;
    f.drone = drone; f.attack = attack; f.density = density; f.sparse = sparse; f.crunch = crunch; f.tail = tail;
    f.silence = silence; f.change = change; f.drop = drop; f.section = section;
    f.index = (int64_t) std::llround (t * fps);
}
}
