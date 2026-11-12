// DahakaVA.cpp - Dahaka's virtual-analog head on its own, for MPC standalone. By binaryimage.
//
// SPDX-License-Identifier: MIT
//
// Two VA oscillators (saw / pulse / triangle / sine) with up to 7-voice unison and detune,
// VA2 hard sync, a square sub an octave down, analog drift, into Dahaka's drive + state-variable
// filter, three envelopes, two LFOs and a 6-slot mod matrix. DSP is in dahaka_core.h.

#define DAHAKA_ENGINE 2
#define DAHAKA_NAME "Dahaka VA"
#define DAHAKA_UID "BbZT"   // must match "uid" in plugin.json
#include "dahaka_plugin.h"

class DahakaVA : public dk::DahakaPlugin
{
public:
    DahakaVA()
    {
        using dk::Preset;
        // Waves: 0 Saw 1 Pulse 2 Triangle 3 Sine.
        // Mod (desktop enums): src 1 LFO1 2 LFO2 3 ModEnv 4 FltEnv 5 Vel 6 Wheel 7 AT 8 Key 9 Random;
        // dst 1 Pitch 11 Cutoff 12 Reso 13 Amp 14 Noise 15 Pulse Width 16 Detune 17 VA1 Lvl 18 VA2 Lvl 19 VA2 Pitch
        const std::vector<Preset> presets = {
            {"Init", {}},
            #include "ported_presets.inc"
            #include "va_presets.inc"
        };
        const std::vector<dk::PresetValue> levels = {
            #include "va_levels.inc"
        };
        addPresets(presets, levels);
    }
};

MPC_PLUGIN_ENTRY(DahakaVA)
