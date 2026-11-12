// va_presets.inc - new Dahaka VA programs (dark analog, EBM, heavy). Included inside the preset list.
// Times are seconds, levels 0-1, fltCutoff Hz, Fine in cents. Glide stays off.
// Prefixes: BA bass, RF riff/heavy, LD lead (not pretty), PD pad, DR drone, PL pluck/sequence, FX effect.

// ===== BASS =====
{"BA EBM Stomp", {{"voiceMode", 1}, {"va1Wave", 0}, {"va1Level", 0.8f}, {"va2Wave", 1}, {"va2Pw", 0.35f}, {"va2Fine", 8.0f}, {"va2Level", 0.5f}, {"vaDrift", 0.1f},
  {"fltMode", 1}, {"fltCutoff", 420.0f}, {"fltReso", 0.25f}, {"fltEnv", 0.55f}, {"fltVel", 0.5f}, {"fEnvA", 0.001f}, {"fEnvD", 0.15f}, {"fEnvS", 0.12f}, {"fEnvR", 0.05f},
  {"aEnvA", 0.001f}, {"aEnvD", 0.16f}, {"aEnvS", 0.4f}, {"aEnvR", 0.03f}}},
{"BA Sub Pressure", {{"voiceMode", 1}, {"va1Wave", 2}, {"va1Level", 0.7f}, {"va2Level", 0.0f}, {"vaSub", 0.8f}, {"vaDrift", 0.05f},
  {"fltMode", 1}, {"fltCutoff", 220.0f}, {"fltReso", 0.05f}, {"fltEnv", 0.25f}, {"fEnvD", 0.12f}, {"fEnvS", 0.0f},
  {"aEnvA", 0.001f}, {"aEnvD", 0.5f}, {"aEnvS", 0.85f}, {"aEnvR", 0.08f}, {"mod1Src", 6}, {"mod1Dst", 11}, {"mod1Amt", 0.25f}}},
{"BA Chug Engine", {{"voiceMode", 1}, {"va1Wave", 0}, {"va1Level", 0.8f}, {"va2Wave", 1}, {"va2Semi", -12}, {"va2Level", 0.6f}, {"vaSub", 0.3f},
  {"fltMode", 1}, {"fltCutoff", 300.0f}, {"fltReso", 0.2f}, {"fltDrive", 0.7f}, {"fltEnv", 0.6f}, {"fltVel", 0.6f}, {"fEnvD", 0.12f}, {"fEnvS", 0.05f},
  {"aEnvA", 0.001f}, {"aEnvD", 0.18f}, {"aEnvS", 0.65f}, {"aEnvR", 0.04f}, {"mod1Src", 6}, {"mod1Dst", 11}, {"mod1Amt", 0.35f}}},
{"BA Tritone Rot", {{"voiceMode", 1}, {"va1Wave", 0}, {"va1Level", 0.8f}, {"va2Wave", 0}, {"va2Semi", 6}, {"va2Fine", 0.0f}, {"va2Level", 0.45f},
  {"fltMode", 1}, {"fltCutoff", 380.0f}, {"fltReso", 0.25f}, {"fltDrive", 0.6f}, {"fltEnv", 0.45f}, {"fEnvD", 0.2f}, {"fEnvS", 0.2f},
  {"aEnvA", 0.001f}, {"aEnvS", 0.9f}, {"aEnvR", 0.06f}}},
{"BA Offbeat Click", {{"voiceMode", 1}, {"va1Wave", 0}, {"va1Level", 0.9f}, {"va2Level", 0.0f}, {"fltMode", 1}, {"fltCutoff", 260.0f}, {"fltReso", 0.15f},
  {"fltEnv", 0.7f}, {"fltKey", 0.6f}, {"fEnvA", 0.001f}, {"fEnvD", 0.07f}, {"fEnvS", 0.0f}, {"fEnvR", 0.03f},
  {"aEnvA", 0.001f}, {"aEnvD", 0.12f}, {"aEnvS", 0.0f}, {"aEnvR", 0.03f}, {"ampVel", 0.3f}}},
{"BA Acid Stain", {{"voiceMode", 1}, {"va1Wave", 0}, {"va1Level", 0.85f}, {"va2Level", 0.0f}, {"fltMode", 1}, {"fltCutoff", 300.0f}, {"fltReso", 0.78f},
  {"fltDrive", 0.55f}, {"fltEnv", 0.65f}, {"fltKey", 0.3f}, {"fEnvA", 0.001f}, {"fEnvD", 0.18f}, {"fEnvS", 0.0f},
  {"aEnvA", 0.001f}, {"aEnvD", 0.3f}, {"aEnvS", 0.8f}, {"aEnvR", 0.05f}, {"mod1Src", 7}, {"mod1Dst", 11}, {"mod1Amt", 0.4f}, {"mod2Src", 5}, {"mod2Dst", 12}, {"mod2Amt", 0.2f}}},

// ===== RIFF / HEAVY =====
{"RF Supersaw Furnace", {{"va1Wave", 0}, {"va1Unison", 7}, {"va1Detune", 0.35f}, {"va1Level", 0.7f}, {"va2Wave", 0}, {"va2Semi", -12}, {"va2Unison", 5}, {"va2Detune", 0.3f}, {"va2Level", 0.5f},
  {"fltMode", 1}, {"fltCutoff", 900.0f}, {"fltReso", 0.15f}, {"fltDrive", 0.7f}, {"aEnvA", 0.002f}, {"aEnvS", 0.95f}, {"aEnvR", 0.2f},
  {"mod1Src", 6}, {"mod1Dst", 11}, {"mod1Amt", 0.5f}}},
{"RF Drop Wall", {{"va1Wave", 0}, {"va1Unison", 3}, {"va1Detune", 0.2f}, {"va1Level", 0.75f}, {"va2Wave", 1}, {"va2Pw", 0.4f}, {"va2Fine", 9.0f}, {"va2Level", 0.55f},
  {"fltMode", 1}, {"fltCutoff", 550.0f}, {"fltReso", 0.15f}, {"fltDrive", 0.8f}, {"fltEnv", 0.35f}, {"fltVel", 0.7f}, {"fEnvD", 0.12f}, {"fEnvS", 0.15f},
  {"aEnvA", 0.001f}, {"aEnvD", 0.18f}, {"aEnvS", 0.45f}, {"aEnvR", 0.05f}, {"mod1Src", 6}, {"mod1Dst", 11}, {"mod1Amt", 0.45f}}},
{"RF Fifth Stack", {{"voiceMode", 1}, {"va1Wave", 0}, {"va1Level", 0.75f}, {"va2Wave", 0}, {"va2Semi", 7}, {"va2Fine", 3.0f}, {"va2Level", 0.65f}, {"vaSub", 0.3f},
  {"fltMode", 1}, {"fltCutoff", 1000.0f}, {"fltDrive", 0.65f}, {"aEnvA", 0.001f}, {"aEnvS", 0.9f}, {"aEnvR", 0.1f}, {"mod1Src", 6}, {"mod1Dst", 11}, {"mod1Amt", 0.4f}}},
{"RF Sync Saw Snarl", {{"voiceMode", 1}, {"va1Wave", 0}, {"va1Level", 0.3f}, {"va2Wave", 0}, {"va2Semi", 12}, {"va2Level", 0.8f}, {"vaSync", 1},
  {"mEnvA", 0.001f}, {"mEnvD", 0.4f}, {"mEnvS", 0.15f}, {"mod2Src", 3}, {"mod2Dst", 19}, {"mod2Amt", 0.6f}, {"mod3Src", 6}, {"mod3Dst", 19}, {"mod3Amt", 0.5f},
  {"fltMode", 1}, {"fltCutoff", 2500.0f}, {"fltDrive", 0.5f}, {"aEnvA", 0.001f}, {"aEnvS", 0.9f}, {"aEnvR", 0.1f}}},

// ===== LEADS (gnarly, not pretty) =====
{"LD Wire Scream", {{"voiceMode", 2}, {"va1Wave", 1}, {"va1Pw", 0.15f}, {"va1Level", 0.7f}, {"va2Wave", 0}, {"va2Semi", 12}, {"va2Fine", -9.0f}, {"va2Level", 0.4f},
  {"fltMode", 2}, {"fltCutoff", 1800.0f}, {"fltReso", 0.5f}, {"fltDrive", 0.6f}, {"aEnvA", 0.005f}, {"aEnvS", 1.0f}, {"aEnvR", 0.2f},
  {"mod1Src", 7}, {"mod1Dst", 11}, {"mod1Amt", 0.4f}, {"mod2Src", 1}, {"mod2Dst", 15}, {"mod2Amt", 0.3f}, {"lfo1Rate", 5.5f}}},
{"LD Siren Pulse", {{"voiceMode", 1}, {"va1Wave", 1}, {"va1Pw", 0.3f}, {"va1Level", 0.8f}, {"va2Level", 0.0f}, {"lfo2Shape", 1}, {"lfo2Rate", 0.4f},
  {"mod3Src", 2}, {"mod3Dst", 1}, {"mod3Amt", 0.17f}, {"fltMode", 0}, {"fltCutoff", 2400.0f}, {"fltReso", 0.3f}, {"aEnvA", 0.15f}, {"aEnvS", 1.0f}, {"aEnvR", 0.8f}}},

// ===== PADS =====
{"PD Cold War Strings", {{"va1Wave", 0}, {"va1Unison", 5}, {"va1Detune", 0.3f}, {"va1Level", 0.6f}, {"va2Wave", 1}, {"va2Pw", 0.3f}, {"va2Semi", 12}, {"va2Level", 0.35f}, {"vaDrift", 0.5f},
  {"lfo2Rate", 0.2f}, {"mod3Src", 2}, {"mod3Dst", 15}, {"mod3Amt", 0.5f}, {"fltMode", 0}, {"fltCutoff", 2200.0f},
  {"aEnvA", 1.0f}, {"aEnvD", 1.0f}, {"aEnvS", 0.9f}, {"aEnvR", 2.5f}}},
{"PD Bunker Haze", {{"va1Wave", 1}, {"va1Pw", 0.5f}, {"va1Unison", 3}, {"va1Detune", 0.25f}, {"va1Level", 0.6f}, {"va2Wave", 2}, {"va2Semi", -12}, {"va2Level", 0.5f},
  {"noise", 0.04f}, {"vaDrift", 0.6f}, {"lfo1Shape", 1}, {"lfo1Rate", 0.15f}, {"lfo1Retrig", 0}, {"mod2Src", 1}, {"mod2Dst", 15}, {"mod2Amt", 0.6f},
  {"fltMode", 0}, {"fltCutoff", 1100.0f}, {"fltReso", 0.2f}, {"aEnvA", 2.0f}, {"aEnvS", 1.0f}, {"aEnvR", 3.5f}}},
{"PD Phrygian Cluster", {{"va1Wave", 0}, {"va1Unison", 3}, {"va1Detune", 0.2f}, {"va1Level", 0.6f}, {"va2Wave", 0}, {"va2Semi", 1}, {"va2Fine", 0.0f}, {"va2Level", 0.4f},
  {"vaDrift", 0.4f}, {"fltMode", 0}, {"fltCutoff", 1300.0f}, {"aEnvA", 1.8f}, {"aEnvS", 0.95f}, {"aEnvR", 3.0f}, {"mod1Src", 6}, {"mod1Dst", 11}, {"mod1Amt", 0.4f}}},

// ===== DRONE =====
{"DR Reactor Hum", {{"voiceMode", 1}, {"va1Wave", 0}, {"va1Unison", 4}, {"va1Detune", 0.15f}, {"va1Level", 0.7f}, {"va2Wave", 0}, {"va2Semi", -12}, {"va2Fine", 2.0f}, {"va2Level", 0.6f},
  {"vaDrift", 0.8f}, {"lfo2Rate", 0.06f}, {"mod3Src", 2}, {"mod3Dst", 11}, {"mod3Amt", 0.35f}, {"fltMode", 2}, {"fltCutoff", 300.0f}, {"fltReso", 0.7f}, {"fltDrive", 0.6f},
  {"aEnvA", 3.0f}, {"aEnvS", 1.0f}, {"aEnvR", 5.0f}}},
{"DR Geiger Field", {{"voiceMode", 1}, {"va1Level", 0.15f}, {"va2Level", 0.0f}, {"noise", 0.8f}, {"lfo1Shape", 4}, {"lfo1Rate", 9.0f}, {"lfo1Retrig", 0},
  {"mod2Src", 1}, {"mod2Dst", 13}, {"mod2Amt", -0.8f}, {"mod3Src", 1}, {"mod3Dst", 11}, {"mod3Amt", 0.3f},
  {"fltMode", 2}, {"fltCutoff", 3000.0f}, {"fltReso", 0.6f}, {"aEnvA", 0.5f}, {"aEnvS", 1.0f}, {"aEnvR", 2.0f}}},

// ===== PLUCK / SEQUENCE =====
{"PL Machine Tick", {{"va1Wave", 1}, {"va1Pw", 0.3f}, {"va1Level", 0.8f}, {"va2Level", 0.0f}, {"fltMode", 1}, {"fltCutoff", 400.0f}, {"fltReso", 0.2f},
  {"fltEnv", 0.8f}, {"fltVel", 0.7f}, {"fEnvD", 0.09f}, {"fEnvS", 0.0f}, {"aEnvA", 0.001f}, {"aEnvD", 0.16f}, {"aEnvS", 0.0f}, {"aEnvR", 0.08f}}},
{"PL Berlin Rust", {{"va1Wave", 0}, {"va1Level", 0.8f}, {"va2Wave", 1}, {"va2Semi", 12}, {"va2Level", 0.3f}, {"vaDrift", 0.3f},
  {"fltMode", 1}, {"fltCutoff", 700.0f}, {"fltReso", 0.45f}, {"fltEnv", 0.5f}, {"fEnvD", 0.2f}, {"fEnvS", 0.0f},
  {"aEnvA", 0.001f}, {"aEnvD", 0.35f}, {"aEnvS", 0.0f}, {"aEnvR", 0.2f}, {"mod1Src", 9}, {"mod1Dst", 11}, {"mod1Amt", 0.15f}}},

// ===== FX =====
{"FX Air Raid", {{"voiceMode", 1}, {"va1Wave", 0}, {"va1Level", 0.7f}, {"va2Wave", 0}, {"va2Semi", 7}, {"va2Level", 0.5f}, {"lfo2Shape", 0}, {"lfo2Rate", 0.3f},
  {"mod3Src", 2}, {"mod3Dst", 1}, {"mod3Amt", 1.0f}, {"fltMode", 0}, {"fltCutoff", 2500.0f}, {"fltReso", 0.3f}, {"aEnvA", 0.3f}, {"aEnvS", 1.0f}, {"aEnvR", 1.5f}}},
