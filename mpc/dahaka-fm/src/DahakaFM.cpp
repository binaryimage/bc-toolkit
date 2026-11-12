// DahakaFM.cpp - Dahaka's FM head on its own, for MPC standalone. By binaryimage.
//
// SPDX-License-Identifier: MIT
//
// 4-operator FM/PM (Leviasynth-style), 8 algorithms, operator D feedback, 6 operator waves,
// per-operator ADSR and velocity, into Dahaka's drive + state-variable filter, three envelopes,
// two LFOs and a 6-slot mod matrix. All the DSP is in dahaka_core.h (Dahaka's own voice code).

#define DAHAKA_ENGINE 0
#define DAHAKA_NAME "Dahaka FM"
#define DAHAKA_UID "BN5U"   // must match "uid" in plugin.json
#include "dahaka_plugin.h"

class DahakaFM : public dk::DahakaPlugin
{
public:
    DahakaFM()
    {
        using dk::Preset;
        // Mod numbers below are the desktop enums: src 1 LFO1, 2 LFO2, 3 Mod Env, 4 Filter Env,
        // 5 Velocity, 6 Wheel, 7 Aftertouch, 8 Key, 9 Random; dst 1 Pitch, 2 FM Depth,
        // 3 FM Feedback, 4 FM Level, 11 Cutoff, 12 Resonance, 13 Amp, 14 Noise.
        const std::vector<Preset> presets = {
            {"Init", {}},
            #include "ported_presets.inc"
            #include "fm_presets.inc"
        };
        const std::vector<dk::PresetValue> levels = {
            #include "fm_levels.inc"
        };
        addPresets(presets, levels);
    }
};

MPC_PLUGIN_ENTRY(DahakaFM)
