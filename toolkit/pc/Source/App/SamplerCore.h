#pragma once
#include "Common/JuceHeader.h"
#include "Common/Files.h"
#include "Common/Dsp.h"

namespace tk
{
    /** Auto-sampler logic, independent of the UI so it can be tested. */
    struct SamplerPlan
    {
        juce::String name = "Instrument";
        int lo = 36, hi = 84, step = 3;
        std::vector<int> velocities { 110 };
        double noteSeconds = 2.0, tailSeconds = 1.5;
        bool normalize = true, trimStart = true;
        juce::File outDir;
        juce::Array<juce::File> copyTo;       // e.g. MPC drive + Music/MPC Songs
        std::vector<int> notes() const
        {
            std::vector<int> n;
            for (int k = lo; k <= hi; k += juce::jmax (1, step)) n.push_back (k);
            if (n.empty() || n.back() != hi) n.push_back (hi);
            return n;
        }
    };

    struct SampledNote { int note, velocity; juce::AudioBuffer<float> audio; double sr; };

    struct SamplerResult { juce::StringArray files; juce::File sfz, xpm; juce::String error; };

    /** Trim, normalise (one gain for everything, keeps dynamics between velocities), write WAV/SFZ/XPM. */
    inline SamplerResult finishSampling (std::vector<SampledNote>& takes, const SamplerPlan& plan)
    {
        SamplerResult res;
        if (takes.empty()) { res.error = "Nothing was recorded."; return res; }
        float globalPeak = 0;
        for (auto& t : takes)
        {
            auto& b = t.audio;
            float pk = peakOf (b);
            if (pk < 1e-4f) continue;
            int start = 0;
            if (plan.trimStart)
            {
                start = juce::jmax (0, firstAbove (b, pk * 0.02f) - (int) (0.002 * t.sr));
            }
            int end = lastAbove (b, pk * 0.001f);
            end = juce::jmin (b.getNumSamples(), juce::jmax (start + 16, end + (int) (0.02 * t.sr)));
            juce::AudioBuffer<float> o (b.getNumChannels(), end - start);
            for (int c = 0; c < o.getNumChannels(); ++c) o.copyFrom (c, 0, b, c, start, end - start);
            // 10 ms fade out so tails never click
            int f = juce::jmin (o.getNumSamples(), (int) (0.01 * t.sr));
            o.applyGainRamp (o.getNumSamples() - f, f, 1.0f, 0.0f);
            b = std::move (o);
            globalPeak = juce::jmax (globalPeak, pk);
        }
        if (globalPeak < 1e-4f) { res.error = "Every take was silent. Check the source and the MIDI output."; return res; }
        float gain = plan.normalize ? 0.95f / globalPeak : 1.0f;
        plan.outDir.createDirectory();
        std::vector<ZoneSample> zones;
        std::vector<int> roots;
        for (auto& t : takes) if (std::find (roots.begin(), roots.end(), t.note) == roots.end()) roots.push_back (t.note);
        std::sort (roots.begin(), roots.end());
        auto vels = plan.velocities;
        std::sort (vels.begin(), vels.end());
        for (auto& t : takes)
        {
            if (peakOf (t.audio) < 1e-5f) continue;
            t.audio.applyGain (gain);
            juce::String fname = safeName (plan.name) + "_" + noteName (t.note).replace ("#", "s") + "_v" + juce::String (t.velocity) + ".wav";
            auto f = plan.outDir.getChildFile (fname);
            if (! writeWav (f, t.audio, t.sr, 24)) { res.error = "Couldn't write " + f.getFullPathName(); return res; }
            res.files.add (f.getFullPathName());
            // key range: halfway to the neighbouring sampled notes
            size_t i = (size_t) (std::find (roots.begin(), roots.end(), t.note) - roots.begin());
            int lo = i == 0 ? 0 : (roots[i - 1] + t.note) / 2 + 1;
            int hi = i + 1 == roots.size() ? 127 : (t.note + roots[i + 1]) / 2;
            size_t vi = (size_t) (std::find (vels.begin(), vels.end(), t.velocity) - vels.begin());
            int vlo = vi == 0 ? 1 : (vels[vi - 1] + t.velocity) / 2 + 1;
            int vhi = vi + 1 == vels.size() ? 127 : (t.velocity + vels[vi + 1]) / 2;
            zones.push_back ({ fname, t.note, lo, hi, vlo, vhi });
        }
        res.sfz = plan.outDir.getChildFile (safeName (plan.name) + ".sfz");
        writeSfz (res.sfz, zones);
        res.xpm = plan.outDir.getChildFile (safeName (plan.name) + ".xpm");
        writeXpmKeygroup (res.xpm, plan.name, zones);
        for (auto& dest : plan.copyTo)
        {
            auto d = dest.getChildFile (safeName (plan.name));
            d.createDirectory();
            for (auto& path : res.files) juce::File (path).copyFileTo (d.getChildFile (juce::File (path).getFileName()));
            res.xpm.copyFileTo (d.getChildFile (res.xpm.getFileName()));
        }
        return res;
    }
}
