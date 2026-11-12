// DahakaWave.cpp - Dahaka's wavetable head on its own, for MPC standalone. By binaryimage.
//
// SPDX-License-Identifier: MIT
//
// Two wavetable oscillators (Hydrasynth-style): 8 tables x 32 frames, band-limited per octave,
// each with a mutator (FM-Lin, Sync, Phase Warp, Wavestack), into Dahaka's drive + state-variable
// filter, three envelopes, two LFOs and a 6-slot mod matrix. DSP is in dahaka_core.h.
//
// The tables are built once when the first instance loads (about a second on the MPC).

#define DAHAKA_ENGINE 1
#define DAHAKA_NAME "Dahaka Wave"
#define DAHAKA_UID "BNl7"   // must match "uid" in plugin.json
#include "dahaka_plugin.h"

class DahakaWave : public dk::DahakaPlugin
{
public:
    DahakaWave()
    {
        using dk::Preset;
        // Tables: 0 Classic 1 Harmonic Sweep 2 Vox Formant 3 PWM 4 Sync Sweep 5 Sine Fold 6 Odd/Even 7 Prime Bells
        // Mutators: 0 Off 1 FM-Lin 2 Sync 3 Phase Warp 4 Wavestack
        // Mod (desktop enums): src 1 LFO1 2 LFO2 3 ModEnv 4 FltEnv 5 Vel 6 Wheel 7 AT 8 Key 9 Random;
        // dst 1 Pitch 5 WT1 Pos 6 WT2 Pos 7 WT1 Mut 8 WT2 Mut 9 WT1 Lvl 10 WT2 Lvl 11 Cutoff 12 Reso 13 Amp 14 Noise
        const std::vector<Preset> presets = {
            {"Init", {}},
            #include "ported_presets.inc"
            #include "wt_presets.inc"
        };
        const std::vector<dk::PresetValue> levels = {
            #include "wt_levels.inc"
        };
        addPresets(presets, levels);
    }
};

MPC_PLUGIN_ENTRY(DahakaWave)
