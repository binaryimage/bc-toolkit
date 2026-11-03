#include "Theory.h"
#include "Dsp.h"
#include <random>

namespace tk::theory
{
const std::vector<Scale>& scales()
{
    static const std::vector<Scale> s = {
        { "Phrygian", { 0, 1, 3, 5, 7, 8, 10 } },
        { "Natural minor", { 0, 2, 3, 5, 7, 8, 10 } },
        { "Harmonic minor", { 0, 2, 3, 5, 7, 8, 11 } },
        { "Phrygian dominant", { 0, 1, 4, 5, 7, 8, 10 } },
        { "Locrian", { 0, 1, 3, 5, 6, 8, 10 } },
        { "Hungarian minor", { 0, 2, 3, 6, 7, 8, 11 } },
        { "Double harmonic", { 0, 1, 4, 5, 7, 8, 11 } },
        { "Dorian", { 0, 2, 3, 5, 7, 9, 10 } },
        { "Neapolitan minor", { 0, 1, 3, 5, 7, 8, 11 } },
        { "Aeolian b5", { 0, 2, 3, 5, 6, 8, 10 } },
    };
    return s;
}

std::vector<int> chordInScale (int rootPc, const Scale& sc, int degree, int octave, bool seventh)
{
    std::vector<int> notes;
    const int n = (int) sc.steps.size();
    for (int k = 0; k < (seventh ? 4 : 3); ++k)
    {
        int idx = degree + 2 * k;
        int pitch = sc.steps[(size_t) (idx % n)] + 12 * (idx / n);
        notes.push_back (12 * (octave + 1) + rootPc + pitch);
    }
    return notes;
}

static juce::String chordName (int rootPc, const Scale& sc, int degree)
{
    auto ch = chordInScale (rootPc, sc, degree, 3, false);
    int third = ch[1] - ch[0], fifth = ch[2] - ch[0];
    juce::String q = third == 3 ? (fifth == 6 ? "dim" : "m") : (fifth == 8 ? "aug" : "");
    return pitchClass (ch[0]) + q;
}

Riff makeRiff (const RiffOptions& o)
{
    Riff r;
    r.bpm = o.bpm; r.bars = o.bars;
    const auto& sc = scales()[(size_t) juce::jlimit (0, (int) scales().size() - 1, o.scale)];
    std::mt19937 rng (o.seed);
    // dark-leaning weights per degree: tonic, flat-two, minor four, flat-six and flat-seven pull hardest
    const double weight[7] = { 2.0, 2.4, 0.6, 1.8, 1.0, 2.6, 1.6 };
    int prev = 0;
    for (int b = 0; b < o.bars; ++b)
    {
        int deg = 0;
        if (b > 0 && b < o.bars - 1)
        {
            std::vector<double> w (weight, weight + 7);
            w[(size_t) prev] *= 0.15;                         // rarely repeat
            if (sc.steps.size() > 4 && sc.steps[4] != 7) w[4] *= 0.4; // diminished fifth chord: use sparingly
            std::discrete_distribution<int> pick (w.begin(), w.end());
            deg = pick (rng);
        }
        else if (b == o.bars - 1 && o.bars > 1)
        {
            std::discrete_distribution<int> pick ({ 1.0, 2.0, 0.0, 0.5, 1.2, 1.5, 1.4 }); // turnaround back to i
            deg = pick (rng);
            if (deg == prev) deg = (deg + 1) % 7;
        }
        r.degrees.push_back (deg);
        r.chordNames.add (chordName (o.root, sc, deg));
        prev = deg;
    }
    // chords with smooth voice leading around C3..C4
    std::vector<int> last;
    for (int b = 0; b < o.bars; ++b)
    {
        auto ch = chordInScale (o.root, sc, r.degrees[(size_t) b], 3, o.sevenths);
        if (! last.empty())
        {
            // try inversions and pick the one closest to the previous chord
            std::vector<int> best = ch; int bestCost = 1 << 30;
            for (int inv = 0; inv < (int) ch.size(); ++inv)
                for (int shift = -12; shift <= 0; shift += 12)
                {
                    std::vector<int> v = ch;
                    for (int k = 0; k < inv; ++k) v[(size_t) k] += 12;
                    for (auto& x : v) x += shift;
                    std::sort (v.begin(), v.end());
                    int cost = 0;
                    for (size_t k = 0; k < v.size() && k < last.size(); ++k) cost += std::abs (v[k] - last[k]);
                    if (v.front() < 45) cost += 20;
                    if (cost < bestCost) { bestCost = cost; best = v; }
                }
            ch = best;
        }
        last = ch;
        for (int n : ch) r.chords.push_back ({ b * 4.0, 3.9, n, 82, 1 });
        // bass
        int bassNote = 12 * 3 + o.root + sc.steps[(size_t) r.degrees[(size_t) b] % sc.steps.size()] - 12;
        if (bassNote < 33) bassNote += 12;
        switch (o.bassStyle)
        {
            case 0: // EBM: 16ths, octave flick on the offbeats
                for (int s = 0; s < 16; ++s) r.bass.push_back ({ b * 4.0 + s * 0.25, 0.2, bassNote + ((s % 4 == 2) ? 12 : 0), s % 4 == 0 ? 120 : 92, 2 });
                break;
            case 1: // pulse 8ths
                for (int s = 0; s < 8; ++s) r.bass.push_back ({ b * 4.0 + s * 0.5, 0.42, bassNote, s % 2 == 0 ? 115 : 90, 2 });
                break;
            case 2: // long notes
                r.bass.push_back ({ b * 4.0, 3.9, bassNote, 100, 2 });
                break;
            default: // boom-bap: hits on 1, the and-of-2, and 3-and
                for (double t : { 0.0, 1.5, 2.5, 3.25 }) r.bass.push_back ({ b * 4.0 + t, 0.45, bassNote, 105, 2 });
        }
        // lead: chord tones and scale steps in 8ths, sparse, with the occasional flat-two rub
        std::uniform_real_distribution<double> u (0, 1);
        for (int s = 0; s < 8; ++s)
        {
            if (u (rng) < 0.38) continue;
            int pool[5] = { ch[0] + 12, ch[1] + 12, ch[2 % ch.size()] + 12, 12 * 5 + o.root + sc.steps[1], 12 * 5 + o.root + sc.steps[(size_t) (s % sc.steps.size())] };
            int note = pool[(int) (u (rng) * 5) % 5];
            r.lead.push_back ({ b * 4.0 + s * 0.5, u (rng) < 0.3 ? 0.95 : 0.45, note, 70 + (int) (u (rng) * 40), 3 });
        }
    }
    return r;
}
}
