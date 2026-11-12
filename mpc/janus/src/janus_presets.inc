// janus_presets.inc - the 64 built-in programs, included inside Janus::addPrograms().
//
// program(name, common overrides, Scene 1 overrides, Scene 2 overrides, copy S1 into S2 first)
// Values are plain values (Hz, ms, %, semitones, choice index). Anything not listed keeps its default.
// Prefixes: BA bass, RF riff/heavy lead, PD pad, DR drone/texture, PC percussion, PL pluck/sequence, FX effect.
// Every program moves somewhere on the mod wheel (Scene 1 -> Scene 2). Glide stays off everywhere.

program("Init", {}, {}, {}, true);

// ===== BASS =====================================================================================
program("BA Morph Grind",
    {{kVoiceMode, 1}, {kDistDrive, 35}, {kDistMix, 35}, {kDistTone, 4500}},
    {{sO1Wave, 0}, {sO2Wave, 1}, {sO2PW, 35}, {sO2Semi, -12}, {sO2Fine, 4}, {sO2Level, 85},
     {sFType, 0}, {sCutoff, 160}, {sReso, 30}, {sFEnv, 55}, {sFDrive, 55}, {sKeyTrk, 40},
     {sFA, 1}, {sFD, 220}, {sFS, 10}, {sFR, 120}, {sAA, 1}, {sAD, 400}, {sAS, 90}, {sAR, 90}, {sHPF, 28}},
    {{sSync, 1}, {sO1Pitch, 17}, {sCutoff, 420}, {sReso, 45}, {sFType, 1}, {sFDrive, 80}, {sFEnv, 45}, {sFeedback, 25}},
    true);

program("BA Sub Pressure",
    {{kVolume, 2.2f}, {kVoiceMode, 1}},
    {{sO1Wave, 2}, {sO2Wave, 3}, {sO2Semi, -12}, {sO2Level, 70}, {sFType, 0}, {sCutoff, 220}, {sReso, 5}, {sFEnv, 25},
     {sFD, 140}, {sFS, 0}, {sAA, 1}, {sAD, 500}, {sAS, 85}, {sAR, 80}, {sPegAmt, 12}, {sPegDec, 18}},
    {{sO1Wave, 0}, {sCutoff, 480}, {sFDrive, 75}, {sReso, 25}},
    true);

program("BA Chug Engine",
    {{kVoiceMode, 1}, {kVelFilt, 60}, {kDistDrive, 40}, {kDistMix, 30}, {kDistTone, 3800}},
    {{sO1Wave, 0}, {sO2Wave, 1}, {sO2Semi, -12}, {sO2Level, 75}, {sFType, 0}, {sCutoff, 300}, {sReso, 20}, {sFEnv, 60},
     {sFDrive, 70}, {sFD, 120}, {sFS, 5}, {sFR, 60}, {sAD, 180}, {sAS, 70}, {sAR, 40}, {sHPF, 35}},
    {{sCutoff, 950}, {sReso, 35}, {sFDrive, 92}, {sFeedback, 20}},
    true);

program("BA Phrygian Pulse",
    {{kVolume, 0.8f}, {kVoiceMode, 1}, {kLfo1Rate, 0.7f}},
    {{sO1Wave, 1}, {sO1PW, 20}, {sO2Wave, 1}, {sO2PW, 30}, {sO2Semi, -12}, {sO2Level, 70}, {sL1PWM, 30},
     {sFType, 1}, {sCutoff, 380}, {sReso, 30}, {sFEnv, 45}, {sFD, 260}, {sFS, 20}, {sAD, 300}, {sAS, 80}, {sAR, 70}},
    {{sO1PW, 8}, {sRing, 45}, {sCutoff, 700}, {sReso, 45}},
    true);

program("BA FM Knuckle",
    {{kVolume, -1.9f}, {kVoiceMode, 1}, {kDistDrive, 30}, {kDistMix, 25}},
    {{sO1Wave, 3}, {sO2Wave, 3}, {sO2Semi, 0}, {sO2Fine, 0}, {sO2Level, 20}, {sFM, 40}, {sFType, 0}, {sCutoff, 600},
     {sReso, 10}, {sFEnv, 50}, {sFD, 160}, {sFS, 15}, {sAD, 350}, {sAS, 80}, {sAR, 60}, {sPegAmt, 7}, {sPegDec, 20}},
    {{sFM, 85}, {sO2Semi, 7}, {sCutoff, 1100}},
    true);

program("BA Acid Stain",
    {{kVoiceMode, 1}, {kATFilt, 40}, {kDistDrive, 50}, {kDistMix, 40}, {kDlyMix, 12}, {kDlyDiv, 5}},
    {{sO1Wave, 0}, {sO2Level, 0}, {sFType, 1}, {sCutoff, 260}, {sReso, 75}, {sFEnv, 70}, {sFDrive, 60}, {sKeyTrk, 30},
     {sFD, 180}, {sFS, 0}, {sFR, 80}, {sAD, 300}, {sAS, 75}, {sAR, 50}},
    {{sFType, 3}, {sReso, 85}, {sCutoff, 600}, {sO1Wave, 1}, {sO1PW, 30}},
    true);

program("BA Feedback Gut",
    {{kVoiceMode, 1}},
    {{sO1Wave, 0}, {sO2Wave, 1}, {sO2Semi, -12}, {sFeedback, 60}, {sFType, 0}, {sCutoff, 200}, {sReso, 40}, {sFEnv, 45},
     {sFDrive, 60}, {sFD, 300}, {sFS, 20}, {sAD, 400}, {sAS, 85}, {sAR, 90}, {sHPF, 30}},
    {{sFeedback, 95}, {sCutoff, 520}, {sReso, 55}},
    true);

program("BA Ring Gravel",
    {{kVolume, -1.8f}, {kVoiceMode, 1}, {kDistDrive, 25}, {kDistMix, 30}},
    {{sO1Wave, 0}, {sO2Wave, 1}, {sO2Semi, 0}, {sO2Fine, 3}, {sO2Level, 50}, {sRing, 40}, {sFType, 2}, {sCutoff, 400},
     {sReso, 25}, {sFEnv, 40}, {sFD, 200}, {sFS, 25}, {sAD, 300}, {sAS, 85}, {sAR, 70}},
    {{sRing, 90}, {sHPF, 120}, {sCutoff, 900}},
    true);

program("BA Unison Slab",
    {{kVoiceMode, 2}, {kUniVoices, 4}, {kUniDetune, 15}, {kSpread, 60}, {kDistDrive, 30}, {kDistMix, 25}},
    {{sO1Wave, 0}, {sO2Wave, 0}, {sO2Semi, -12}, {sFType, 0}, {sCutoff, 700}, {sReso, 15}, {sFEnv, 30}, {sFDrive, 50},
     {sFD, 400}, {sFS, 35}, {sAD, 400}, {sAS, 90}, {sAR, 80}, {sHPF, 40}},
    {{sCutoff, 1600}, {sReso, 25}, {sFeedback, 25}},
    true);

program("BA Sync Snarl",
    {{kVoiceMode, 1}, {kDistDrive, 35}, {kDistMix, 35}},
    {{sSync, 1}, {sO1Pitch, 12}, {sO1Wave, 0}, {sO2Wave, 0}, {sO2Semi, 0}, {sO2Level, 40}, {sPegAmt, 12}, {sPegDec, 120},
     {sPegO1, 1}, {sFType, 0}, {sCutoff, 700}, {sReso, 20}, {sFEnv, 35}, {sFD, 250}, {sFS, 30}, {sAD, 300}, {sAS, 85}, {sAR, 60}},
    {{sO1Pitch, 24}, {sCutoff, 1300}, {sReso, 35}},
    true);

program("BA Tritone Grind",
    {{kVoiceMode, 1}, {kDistDrive, 45}, {kDistMix, 35}, {kDistTone, 3500}},
    {{sO1Wave, 0}, {sO2Wave, 0}, {sO2Semi, 6}, {sO2Fine, 0}, {sO2Level, 60}, {sFType, 0}, {sCutoff, 350}, {sReso, 25},
     {sFEnv, 45}, {sFDrive, 70}, {sFD, 200}, {sFS, 20}, {sAD, 400}, {sAS, 85}, {sAR, 60}, {sHPF, 35}},
    {{sO2Semi, -6}, {sCutoff, 800}, {sFeedback, 30}},
    true);

program("BA Deep Hum",
    {{kVolume, 3.5f}, {kVoiceMode, 1}},
    {{sO1Wave, 3}, {sO2Wave, 2}, {sO2Semi, 12}, {sO2Fine, 0}, {sO2Level, 25}, {sFType, 0}, {sCutoff, 180}, {sReso, 0},
     {sFEnv, 15}, {sFD, 300}, {sFS, 40}, {sAA, 3}, {sAD, 500}, {sAS, 90}, {sAR, 120}},
    {{sO1Wave, 0}, {sCutoff, 350}, {sReso, 20}, {sFDrive, 60}},
    true);

program("BA Noise Fist",
    {{kVolume, 5.4f}, {kVoiceMode, 1}, {kVelFilt, 50}},
    {{sO1Wave, 0}, {sO2Level, 40}, {sO2Semi, -12}, {sNoise, 40}, {sFType, 1}, {sCutoff, 500}, {sReso, 20}, {sFEnv, 60},
     {sFD, 90}, {sFS, 0}, {sAD, 200}, {sAS, 60}, {sAR, 50}, {sPegAmt, -12}, {sPegDec, 25}},
    {{sNoise, 70}, {sFType, 3}, {sCutoff, 900}, {sReso, 40}},
    true);

program("BA Machine Offbeat",
    {{kVolume, 5.0f}, {kVoiceMode, 1}, {kVelAmp, 30}},
    {{sO1Wave, 0}, {sO2Level, 0}, {sFType, 0}, {sCutoff, 250}, {sReso, 15}, {sFEnv, 75}, {sKeyTrk, 60}, {sFD, 70},
     {sFS, 0}, {sFR, 30}, {sAA, 1}, {sAD, 120}, {sAS, 0}, {sAR, 30}, {sHPF, 30}},
    {{sCutoff, 600}, {sReso, 45}, {sFDrive, 60}},
    true);

program("BA EBM Stomp",
    {{kVolume, 3.4f}, {kVoiceMode, 1}, {kVelFilt, 60}},
    {{sO1Wave, 0}, {sO2Wave, 1}, {sO2Fine, 10}, {sO2Level, 70}, {sFType, 0}, {sCutoff, 420}, {sReso, 20}, {sFEnv, 55},
     {sFD, 150}, {sFS, 15}, {sFR, 40}, {sAD, 160}, {sAS, 40}, {sAR, 30}},
    {{sFType, 2}, {sCutoff, 900}, {sReso, 30}, {sFDrive, 60}},
    true);

program("BA Crushed Pulse",
    {{kVoiceMode, 1}, {kDistDrive, 60}, {kDistMix, 60}, {kDistTone, 3000}},
    {{sO1Wave, 1}, {sO1PW, 10}, {sO2Wave, 1}, {sO2PW, 15}, {sO2Semi, -12}, {sFType, 2}, {sCutoff, 1200}, {sReso, 10},
     {sFEnv, 30}, {sFD, 200}, {sFS, 30}, {sAD, 300}, {sAS, 80}, {sAR, 60}, {sHPF, 50}},
    {{sO1PW, 50}, {sFM, 30}, {sCutoff, 700}},
    true);

// ===== RIFF / HEAVY LEAD ========================================================================
program("RF Furnace Unison",
    {{kVolume, -10.6f}, {kVoiceMode, 2}, {kUniVoices, 6}, {kUniDetune, 22}, {kSpread, 90}, {kDistDrive, 45}, {kDistMix, 40}},
    {{sO1Wave, 0}, {sO2Wave, 0}, {sO2Semi, -12}, {sFType, 0}, {sCutoff, 700}, {sReso, 20}, {sFDrive, 70},
     {sFEnv, 35}, {sFD, 600}, {sFS, 40}, {sAD, 600}, {sAS, 95}, {sAR, 180}, {sHPF, 40}},
    {{sCutoff, 2600}, {sReso, 30}, {sFeedback, 35}},
    true);

program("RF Sync Wire",
    {{kVoiceMode, 1}, {kDlyMix, 22}, {kDlyFdbk, 40}, {kRevMix, 18}, {kDistDrive, 25}, {kDistMix, 25}},
    {{sSync, 1}, {sO1Pitch, 7}, {sO1Wave, 0}, {sO2Wave, 0}, {sO2Level, 0}, {sPegAmt, 24}, {sPegDec, 260}, {sPegO1, 1},
     {sFType, 2}, {sCutoff, 3200}, {sReso, 25}, {sFEnv, 20}, {sAA, 3}, {sAD, 500}, {sAS, 80}, {sAR, 200}},
    {{sO1Pitch, 31}, {sRing, 35}, {sCutoff, 1800}, {sReso, 50}, {sFType, 3}},
    true);

// Scene 1 is the palm-muted chug, Scene 2 lets the chord ring open.
program("RF Drop Wall",
    {{kVoiceMode, 0}, {kSpread, 70}, {kVelFilt, 70}, {kDistDrive, 70}, {kDistMix, 70}, {kDistTone, 3500}},
    {{sO1Wave, 0}, {sO2Wave, 1}, {sO2PW, 40}, {sO2Fine, 9}, {sFType, 0}, {sCutoff, 500}, {sReso, 15}, {sFEnv, 35},
     {sFDrive, 80}, {sFD, 120}, {sFS, 10}, {sAD, 160}, {sAS, 40}, {sAR, 50}, {sHPF, 60}},
    {{sCutoff, 1500}, {sFD, 400}, {sFS, 50}, {sAD, 600}, {sAS, 85}, {sAR, 120}},
    true);

// One finger = a power chord (osc 2 sits a fifth up). S2 goes to the octave and opens up.
program("RF Fifth Stack",
    {{kVoiceMode, 1}, {kDistDrive, 55}, {kDistMix, 50}, {kDistTone, 4000}},
    {{sO1Wave, 0}, {sO2Wave, 0}, {sO2Semi, 7}, {sO2Fine, 3}, {sO2Level, 90}, {sFType, 1}, {sCutoff, 900}, {sReso, 15},
     {sFEnv, 30}, {sFDrive, 60}, {sFD, 300}, {sFS, 40}, {sAD, 400}, {sAS, 85}, {sAR, 90}, {sHPF, 50}},
    {{sO2Semi, 12}, {sCutoff, 2200}, {sFeedback, 20}},
    true);

program("RF Grinder Mono",
    {{kVoiceMode, 1}, {kDistDrive, 60}, {kDistMix, 55}},
    {{sO1Wave, 0}, {sO2Wave, 1}, {sO2Semi, -12}, {sRing, 20}, {sFType, 0}, {sCutoff, 1100}, {sReso, 30}, {sFEnv, 25},
     {sFDrive, 90}, {sFD, 300}, {sFS, 50}, {sAD, 300}, {sAS, 90}, {sAR, 80}, {sHPF, 60}},
    {{sFeedback, 55}, {sFType, 3}, {sCutoff, 1400}, {sReso, 50}},
    true);

program("RF Scream Sync",
    {{kVoiceMode, 1}, {kDistDrive, 50}, {kDistMix, 45}, {kDlyMix, 18}, {kDlyFdbk, 35}},
    {{sSync, 1}, {sO1Pitch, 19}, {sO1Wave, 0}, {sO2Level, 0}, {sFType, 2}, {sCutoff, 2600}, {sReso, 20}, {sFEnv, 15},
     {sAD, 500}, {sAS, 90}, {sAR, 150}, {sHPF, 120}},
    {{sO1Pitch, 36}, {sReso, 40}, {sCutoff, 1800}},
    true);

program("RF Buzzsaw Unison",
    {{kVoiceMode, 2}, {kUniVoices, 6}, {kUniDetune, 30}, {kSpread, 100}, {kDistDrive, 40}, {kDistMix, 40}},
    {{sO1Wave, 1}, {sO1PW, 30}, {sO2Wave, 1}, {sO2PW, 30}, {sO2Semi, -12}, {sFType, 0}, {sCutoff, 2000}, {sReso, 10},
     {sFEnv, 20}, {sFDrive, 50}, {sAD, 400}, {sAS, 95}, {sAR, 100}, {sHPF, 150}},
    {{sO1PW, 10}, {sO2PW, 12}, {sFeedback, 40}},
    true);

// Mono siren: LFO 2 bends the pitch; S2 collapses osc 2 onto a minor second and rings it.
program("RF Tritone Siren",
    {{kVoiceMode, 1}, {kLfo2Wave, 1}, {kLfo2Rate, 0.5f}, {kRevMix, 25}, {kDistDrive, 30}, {kDistMix, 30}},
    {{sO1Wave, 0}, {sO2Wave, 1}, {sO2Semi, 6}, {sO2Fine, 0}, {sFType, 2}, {sCutoff, 1800}, {sReso, 50}, {sFEnv, 10},
     {sAA, 40}, {sAD, 500}, {sAS, 95}, {sAR, 400}, {sL2Pitch, 2}},
    {{sO2Semi, 1}, {sRing, 50}, {sCutoff, 1200}},
    true);

program("RF Half-Time Slam",
    {{kVoiceMode, 0}, {kSpread, 60}, {kDistDrive, 60}, {kDistMix, 50}, {kRevMix, 25}, {kRevSize, 70}},
    {{sO1Wave, 0}, {sO2Wave, 0}, {sO2Semi, -12}, {sO2Fine, 5}, {sNoise, 10}, {sFType, 0}, {sCutoff, 600}, {sReso, 15},
     {sFEnv, 70}, {sFDrive, 70}, {sFD, 600}, {sFS, 20}, {sAA, 1}, {sAD, 900}, {sAS, 40}, {sAR, 400}, {sHPF, 40}},
    {{sCutoff, 1500}, {sFeedback, 40}},
    true);

program("RF Rotten Horn",
    {{kVoiceMode, 0}, {kSpread, 50}, {kDistDrive, 30}, {kDistMix, 30}},
    {{sO1Wave, 0}, {sO2Wave, 0}, {sO2Fine, 12}, {sFType, 0}, {sCutoff, 500}, {sReso, 15}, {sFEnv, 50}, {sFA, 60},
     {sFD, 500}, {sFS, 40}, {sAA, 30}, {sAD, 500}, {sAS, 85}, {sAR, 200}},
    {{sFType, 1}, {sReso, 40}, {sFDrive, 80}, {sCutoff, 900}},
    true);

// ===== PADS ====================================================================================
program("PD Cinder",
    {{kVoiceMode, 0}, {kSpread, 80}, {kModType, 0}, {kModMix, 45}, {kModDepth, 60}, {kRevMix, 40}, {kRevSize, 85}, {kLfo2Rate, 0.12f}},
    {{sO1Wave, 0}, {sO2Wave, 0}, {sO2Fine, 11}, {sFType, 2}, {sCutoff, 650}, {sReso, 15}, {sFEnv, 25},
     {sFA, 1800}, {sFD, 3000}, {sFS, 40}, {sFR, 2500}, {sAA, 1500}, {sAD, 2000}, {sAS, 90}, {sAR, 3000}, {sL2Filt, 25}},
    {{sO2Semi, 7}, {sRing, 40}, {sFType, 3}, {sCutoff, 1400}, {sReso, 45}, {sHPF, 180}, {sNoise, 12}, {sL1PWM, 30}, {sO1Wave, 1}},
    true);

program("PD Grey Choir",
    {{kVoiceMode, 0}, {kSpread, 80}, {kModMix, 40}, {kRevMix, 45}, {kRevSize, 85}, {kLfo2Rate, 0.2f}},
    {{sO1Wave, 0}, {sO2Wave, 0}, {sO2Fine, 8}, {sNoise, 15}, {sFType, 3}, {sCutoff, 900}, {sReso, 55}, {sFEnv, 10},
     {sAA, 1400}, {sAD, 2000}, {sAS, 90}, {sAR, 2500}, {sL2Filt, 20}},
    {{sO2Semi, 7}, {sCutoff, 1600}, {sReso, 65}},
    true);

program("PD Fallout Haze",
    {{kVoiceMode, 0}, {kSpread, 90}, {kLfo1Wave, 0}, {kLfo1Rate, 0.3f}, {kModMix, 60}, {kRevMix, 50}, {kRevSize, 80}},
    {{sO1Wave, 1}, {sO2Wave, 1}, {sO2Fine, 13}, {sL1PWM, 40}, {sFType, 2}, {sCutoff, 1100}, {sReso, 15}, {sFEnv, 10},
     {sAA, 1800}, {sAD, 2000}, {sAS, 90}, {sAR, 3500}},
    {{sRing, 30}, {sHPF, 300}, {sCutoff, 1800}},
    true);

program("PD Cold War Strings",
    {{kVoiceMode, 0}, {kSpread, 100}, {kModMix, 70}, {kModDepth, 70}, {kRevMix, 35}},
    {{sO1Wave, 0}, {sO2Wave, 0}, {sO2Fine, 14}, {sFType, 2}, {sCutoff, 2400}, {sReso, 10}, {sFEnv, 0}, {sHPF, 120},
     {sAA, 900}, {sAD, 2000}, {sAS, 90}, {sAR, 2500}},
    {{sFType, 0}, {sCutoff, 700}, {sFEnv, 20}, {sFA, 3000}, {sFD, 4000}},
    true);

program("PD Bunker Glow",
    {{kVoiceMode, 0}, {kSpread, 60}, {kLfo2Rate, 0.1f}, {kRevMix, 40}, {kRevSize, 75}},
    {{sO1Wave, 2}, {sO2Wave, 0}, {sO2Semi, -12}, {sO2Level, 60}, {sFType, 0}, {sCutoff, 450}, {sReso, 30}, {sFEnv, 15},
     {sAA, 2500}, {sAD, 2000}, {sAS, 95}, {sAR, 3000}, {sL2Filt, 40}},
    {{sFeedback, 40}, {sCutoff, 900}},
    true);

// Osc 2 a half step up: a flat-II cluster in every note.
program("PD Phrygian Mist",
    {{kVoiceMode, 0}, {kSpread, 80}, {kModMix, 40}, {kRevMix, 50}, {kRevSize, 90}},
    {{sO1Wave, 0}, {sO2Wave, 0}, {sO2Semi, 1}, {sO2Fine, 0}, {sO2Level, 60}, {sFType, 2}, {sCutoff, 900}, {sReso, 15},
     {sFEnv, 10}, {sAA, 2000}, {sAD, 2000}, {sAS, 90}, {sAR, 3000}},
    {{sO2Semi, 6}, {sCutoff, 1300}, {sReso, 30}},
    true);

program("PD Glass Decay",
    {{kVolume, 1.4f}, {kVoiceMode, 0}, {kSpread, 90}, {kDlyMix, 30}, {kDlyFdbk, 55}, {kRevMix, 60}, {kRevSize, 95}},
    {{sO1Wave, 3}, {sO2Wave, 2}, {sO2Semi, 19}, {sO2Fine, 0}, {sRing, 50}, {sO1Level, 60}, {sO2Level, 40}, {sFType, 3},
     {sCutoff, 2400}, {sReso, 30}, {sFEnv, 0}, {sHPF, 200}, {sAA, 1200}, {sAD, 2500}, {sAS, 80}, {sAR, 3500}},
    {{sFM, 40}, {sCutoff, 1500}},
    true);

program("PD Radiation Bloom",
    {{kVoiceMode, 2}, {kUniVoices, 6}, {kUniDetune, 18}, {kSpread, 100}, {kModMix, 50}, {kRevMix, 45}},
    {{sO1Wave, 0}, {sO2Wave, 0}, {sO2Semi, 12}, {sO2Level, 40}, {sFType, 2}, {sCutoff, 1500}, {sReso, 15}, {sFEnv, 10},
     {sAA, 2500}, {sAD, 2000}, {sAS, 95}, {sAR, 3000}},
    {{sFType, 3}, {sReso, 50}, {sCutoff, 1100}},
    true);

program("PD Sub Cloud",
    {{kVoiceMode, 0}, {kSpread, 50}, {kLfo1Rate, 0.25f}, {kRevMix, 70}, {kRevSize, 95}, {kRevDamp, 70}},
    {{sO1Wave, 3}, {sO2Wave, 2}, {sO2Semi, -12}, {sFType, 0}, {sCutoff, 600}, {sReso, 10}, {sFEnv, 0},
     {sAA, 3000}, {sAD, 3000}, {sAS, 100}, {sAR, 5000}, {sL1Amp, 20}},
    {{sNoise, 20}, {sHPF, 80}, {sCutoff, 1000}},
    true);

program("PD Wire Tension",
    {{kVoiceMode, 0}, {kSpread, 80}, {kRevMix, 40}, {kModMix, 30}},
    {{sO1Wave, 1}, {sO1PW, 8}, {sO2Wave, 0}, {sO2Semi, 12}, {sO2Fine, 20}, {sO2Level, 50}, {sHPF, 400}, {sFType, 2},
     {sCutoff, 3000}, {sReso, 35}, {sFEnv, 0}, {sAA, 1500}, {sAD, 2000}, {sAS, 90}, {sAR, 2500}},
    {{sSync, 1}, {sO1Pitch, 14}, {sCutoff, 2000}},
    true);

// ===== DRONES / TEXTURES ========================================================================
program("DR Feedback",
    {{kVoiceMode, 1}, {kLfo1Rate, 0.09f}, {kRevMix, 35}, {kRevSize, 90}, {kDlyMix, 15}},
    {{sO1Wave, 0}, {sO2Wave, 1}, {sO2Semi, -5}, {sFeedback, 80}, {sNoise, 18}, {sFType, 1}, {sCutoff, 380}, {sReso, 62},
     {sFDrive, 60}, {sFEnv, 10}, {sAA, 900}, {sAS, 100}, {sAR, 3000}, {sL1Filt, 35}, {sL2Pitch, 0.08f}},
    {{sFType, 4}, {sCutoff, 900}, {sReso, 70}, {sFeedback, 95}},
    true);

program("DR Reactor Hum",
    {{kVolume, -1.7f}, {kVoiceMode, 1}, {kLfo2Rate, 0.07f}, {kRevMix, 40}, {kRevSize, 85}},
    {{sO1Wave, 0}, {sO2Wave, 0}, {sO2Semi, -12}, {sO2Fine, 2}, {sFeedback, 50}, {sFType, 1}, {sCutoff, 120}, {sReso, 70},
     {sFDrive, 80}, {sFEnv, 0}, {sAA, 1500}, {sAS, 100}, {sAR, 4000}, {sL2Filt, 30}},
    {{sCutoff, 400}, {sReso, 60}},
    true);

program("DR Geiger Field",
    {{kVoiceMode, 0}, {kLfo1Wave, 4}, {kLfo1Rate, 9.0f}, {kSpread, 80}, {kDlyMix, 25}, {kRevMix, 40}},
    {{sO1Level, 20}, {sO2Level, 0}, {sNoise, 80}, {sFType, 3}, {sCutoff, 3000}, {sReso, 60}, {sFEnv, 0},
     {sAA, 500}, {sAS, 100}, {sAR, 2000}, {sL1Amp, 70}, {sL1Filt, 40}},
    {{sCutoff, 800}, {sReso, 75}},
    true);

program("DR Tritone Engine",
    {{kVolume, -11.9f}, {kVoiceMode, 0}, {kLfo2Rate, 0.07f}, {kSpread, 70}, {kRevMix, 40}},
    {{sO1Wave, 0}, {sO2Wave, 0}, {sO2Semi, 6}, {sO2Fine, 0}, {sFeedback, 40}, {sFType, 0}, {sCutoff, 600}, {sReso, 30},
     {sFDrive, 60}, {sFEnv, 0}, {sAA, 1200}, {sAS, 100}, {sAR, 3000}, {sL2Filt, 50}},
    {{sRing, 70}, {sCutoff, 1000}},
    true);

program("DR Black Ice",
    {{kVoiceMode, 0}, {kSpread, 90}, {kRevMix, 80}, {kRevSize, 95}, {kRevDamp, 30}},
    {{sO1Wave, 3}, {sO2Wave, 3}, {sO2Semi, 19}, {sO2Fine, 0}, {sO2Level, 30}, {sFM, 25}, {sFType, 2}, {sCutoff, 5000},
     {sReso, 5}, {sFEnv, 0}, {sHPF, 200}, {sAA, 2000}, {sAS, 100}, {sAR, 5000}},
    {{sFM, 70}, {sCutoff, 2500}},
    true);

program("DR Turbine",
    {{kVoiceMode, 2}, {kUniVoices, 5}, {kUniDetune, 40}, {kSpread, 100}, {kLfo1Wave, 1}, {kLfo1Rate, 0.15f}, {kRevMix, 35}},
    {{sO1Wave, 0}, {sO2Wave, 0}, {sO2Semi, -12}, {sFType, 3}, {sCutoff, 700}, {sReso, 50}, {sFEnv, 0},
     {sAA, 1500}, {sAS, 100}, {sAR, 3000}, {sL1Filt, 60}},
    {{sCutoff, 2000}, {sReso, 60}},
    true);

program("DR Abyss Choir",
    {{kVoiceMode, 0}, {kLfo2Rate, 0.13f}, {kSpread, 80}, {kRevMix, 60}, {kRevSize, 95}},
    {{sO1Wave, 0}, {sO1Level, 50}, {sO2Level, 0}, {sNoise, 50}, {sFType, 2}, {sCutoff, 400}, {sReso, 80}, {sFEnv, 0},
     {sAA, 2500}, {sAS, 100}, {sAR, 4000}, {sL2Filt, 30}},
    {{sFType, 3}, {sCutoff, 1200}, {sReso, 85}},
    true);

program("DR Fuse Box",
    {{kVoiceMode, 0}, {kLfo1Wave, 4}, {kLfo1Rate, 6.0f}, {kSpread, 60}, {kDistDrive, 40}, {kDistMix, 35}},
    {{sO1Wave, 1}, {sO2Wave, 1}, {sO2Semi, 0}, {sO2Fine, 50}, {sRing, 60}, {sHPF, 200}, {sFType, 2}, {sCutoff, 2000},
     {sReso, 20}, {sFEnv, 0}, {sAA, 400}, {sAS, 100}, {sAR, 1500}, {sL1Pitch, 0.2f}},
    {{sFM, 50}, {sCutoff, 1200}, {sReso, 45}},
    true);

// ===== PERCUSSION ==============================================================================
program("PC Ring Clank",
    {{kVolume, 0.3f}, {kVoiceMode, 0}, {kRevMix, 20}, {kRevSize, 40}, {kVelFilt, 60}},
    {{sO1Wave, 3}, {sO2Wave, 1}, {sO2Semi, 7}, {sO2Fine, 31}, {sFM, 55}, {sRing, 70}, {sO1Level, 40}, {sO2Level, 20},
     {sFType, 3}, {sCutoff, 1900}, {sReso, 55}, {sFEnv, 30}, {sFD, 120}, {sFS, 0}, {sAA, 1}, {sAD, 260}, {sAS, 0}, {sAR, 220},
     {sPegAmt, -12}, {sPegDec, 40}, {sNoise, 10}},
    {{sO2Semi, 13}, {sFM, 85}, {sCutoff, 900}, {sAD, 700}},
    true);

program("PC Anvil",
    {{kVolume, -0.5f}, {kVoiceMode, 0}, {kRevMix, 25}, {kRevSize, 50}},
    {{sO1Wave, 3}, {sO2Wave, 1}, {sO2Semi, 13}, {sO2Fine, 30}, {sFM, 70}, {sRing, 50}, {sO1Level, 50}, {sO2Level, 20},
     {sFType, 3}, {sCutoff, 2200}, {sReso, 40}, {sFEnv, 0}, {sAA, 1}, {sAD, 400}, {sAS, 0}, {sAR, 400}, {sPegAmt, -24}, {sPegDec, 15}},
    {{sO2Semi, 5}, {sCutoff, 1200}},
    true);

program("PC Kick Synth",
    {{kVolume, 4.5f}, {kVoiceMode, 1}, {kVelAmp, 40}},
    {{sO1Wave, 3}, {sO2Level, 0}, {sFType, 0}, {sCutoff, 300}, {sReso, 0}, {sFEnv, 20}, {sFD, 80}, {sFS, 0}, {sFDrive, 60},
     {sAA, 1}, {sAD, 450}, {sAS, 0}, {sAR, 100}, {sPegAmt, 36}, {sPegDec, 35}},
    {{sPegAmt, 48}, {sPegDec, 60}, {sFDrive, 95}, {sCutoff, 500}},
    true);

program("PC Snare Crack",
    {{kVolume, 2.7f}, {kVoiceMode, 0}, {kRevMix, 20}, {kRevSize, 35}},
    {{sO1Wave, 2}, {sO1Level, 40}, {sO2Level, 0}, {sNoise, 90}, {sFType, 2}, {sCutoff, 6000}, {sReso, 10}, {sFEnv, 0},
     {sHPF, 300}, {sAA, 1}, {sAD, 180}, {sAS, 0}, {sAR, 150}, {sPegAmt, 12}, {sPegDec, 20}},
    {{sHPF, 900}, {sReso, 30}, {sAD, 300}},
    true);

program("PC Hat Metal",
    {{kVolume, 2.2f}, {kVoiceMode, 0}},
    {{sO1Wave, 1}, {sO2Wave, 1}, {sO2Semi, 7}, {sO2Fine, 37}, {sRing, 80}, {sNoise, 60}, {sO1Level, 30}, {sO2Level, 30},
     {sFType, 4}, {sCutoff, 6000}, {sReso, 10}, {sFEnv, 0}, {sAA, 1}, {sAD, 60}, {sAS, 0}, {sAR, 40}},
    {{sAD, 300}, {sAR, 250}},
    true);

program("PC Pipe Hit",
    {{kVolume, 5.2f}, {kVoiceMode, 0}, {kRevMix, 30}, {kRevSize, 60}},
    {{sO1Wave, 2}, {sO2Level, 0}, {sFType, 3}, {sCutoff, 600}, {sReso, 80}, {sFEnv, 0}, {sAA, 1}, {sAD, 250}, {sAS, 0},
     {sAR, 250}, {sPegAmt, -7}, {sPegDec, 30}},
    {{sCutoff, 1500}, {sReso, 85}},
    true);

program("PC Chain Rattle",
    {{kVolume, 0.7f}, {kVoiceMode, 0}, {kLfo1Wave, 3}, {kLfo1Rate, 18.0f}, {kRevMix, 25}},
    {{sO1Wave, 0}, {sO1Level, 50}, {sO2Level, 0}, {sNoise, 50}, {sFType, 3}, {sCutoff, 1800}, {sReso, 50}, {sFEnv, 0},
     {sAA, 1}, {sAD, 400}, {sAS, 0}, {sAR, 300}, {sL1Amp, 100}},
    {{sCutoff, 3500}},
    true);

program("PC Steam Blast",
    {{kVoiceMode, 0}, {kRevMix, 25}},
    {{sO1Level, 0}, {sO2Level, 0}, {sNoise, 100}, {sFType, 2}, {sCutoff, 300}, {sReso, 20}, {sFEnv, 90}, {sFA, 20},
     {sFD, 300}, {sFS, 0}, {sAA, 1}, {sAD, 600}, {sAS, 0}, {sAR, 300}},
    {{sFType, 3}, {sReso, 60}},
    true);

// ===== PLUCK / SEQUENCE ========================================================================
program("PL Machine Tick",
    {{kVoiceMode, 0}, {kVelFilt, 70}, {kSpread, 50}},
    {{sO1Wave, 1}, {sO1PW, 30}, {sO2Level, 0}, {sFType, 0}, {sCutoff, 400}, {sReso, 20}, {sFEnv, 80}, {sFD, 90}, {sFS, 0},
     {sAA, 1}, {sAD, 160}, {sAS, 0}, {sAR, 80}},
    {{sReso, 60}, {sCutoff, 900}},
    true);

program("PL Bone Wire",
    {{kVoiceMode, 0}, {kDlyMix, 25}, {kDlyDiv, 9}, {kDlyFdbk, 35}, {kSpread, 60}},
    {{sO1Wave, 0}, {sO2Wave, 2}, {sO2Semi, 12}, {sO2Level, 50}, {sFType, 1}, {sCutoff, 600}, {sReso, 45}, {sFEnv, 70},
     {sFD, 140}, {sFS, 0}, {sAA, 1}, {sAD, 250}, {sAS, 0}, {sAR, 150}},
    {{sFM, 40}, {sCutoff, 900}},
    true);

program("PL FM Splinter",
    {{kVoiceMode, 0}, {kSpread, 60}, {kRevMix, 20}},
    {{sO1Wave, 3}, {sO2Wave, 3}, {sO2Semi, 24}, {sO2Fine, 0}, {sO2Level, 10}, {sFM, 60}, {sFType, 2}, {sCutoff, 3000},
     {sReso, 10}, {sFEnv, 50}, {sFD, 100}, {sFS, 0}, {sAA, 1}, {sAD, 220}, {sAS, 0}, {sAR, 150}},
    {{sFM, 95}, {sO2Semi, 19}},
    true);

program("PL Psy Gate",
    {{kVolume, 3.7f}, {kVoiceMode, 1}, {kDlyMix, 20}, {kDlyDiv, 4}, {kDlyFdbk, 30}},
    {{sO1Wave, 0}, {sO2Level, 0}, {sFType, 0}, {sCutoff, 500}, {sReso, 20}, {sFEnv, 60}, {sFD, 60}, {sFS, 0},
     {sAA, 1}, {sAD, 90}, {sAS, 0}, {sAR, 30}},
    {{sCutoff, 1400}, {sReso, 40}},
    true);

program("PL Phrygian Mallet",
    {{kVoiceMode, 0}, {kRevMix, 30}, {kSpread, 60}},
    {{sO1Wave, 2}, {sO2Wave, 3}, {sO2Semi, 12}, {sO2Fine, 0}, {sO2Level, 50}, {sRing, 20}, {sFType, 2}, {sCutoff, 2200},
     {sReso, 20}, {sFEnv, 30}, {sFD, 200}, {sFS, 0}, {sAA, 1}, {sAD, 500}, {sAS, 0}, {sAR, 400}},
    {{sRing, 60}, {sO2Semi, 13}},
    true);

program("PL Corroded Keys",
    {{kVoiceMode, 0}, {kSpread, 60}, {kDistDrive, 30}, {kDistMix, 30}, {kModMix, 30}},
    {{sO1Wave, 0}, {sO2Wave, 1}, {sO2Fine, 6}, {sFType, 2}, {sCutoff, 1400}, {sReso, 15}, {sFEnv, 40}, {sFD, 300},
     {sFS, 20}, {sAA, 1}, {sAD, 900}, {sAS, 0}, {sAR, 300}},
    {{sRing, 25}, {sHPF, 150}, {sCutoff, 1000}},
    true);

// ===== FX =====================================================================================
program("FX Air Raid",
    {{kVolume, 0.1f}, {kVoiceMode, 1}, {kLfo2Wave, 0}, {kLfo2Rate, 0.3f}, {kRevMix, 40}, {kRevSize, 90}},
    {{sO1Wave, 0}, {sO2Wave, 0}, {sO2Semi, 7}, {sFType, 2}, {sCutoff, 2500}, {sReso, 30}, {sFEnv, 0},
     {sAA, 300}, {sAS, 100}, {sAR, 1500}, {sL2Pitch, 12}},
    {{sO2Semi, 6}, {sRing, 40}},
    true);

program("FX Radio Ghost",
    {{kVolume, -12.5f}, {kVoiceMode, 0}, {kLfo1Wave, 4}, {kLfo1Rate, 12.0f}, {kDlyMix, 30}, {kDlyFdbk, 50}, {kRevMix, 35}},
    {{sO1Wave, 1}, {sO1Level, 60}, {sO2Level, 0}, {sNoise, 30}, {sFType, 3}, {sCutoff, 1800}, {sReso, 70}, {sFEnv, 0},
     {sAA, 100}, {sAS, 100}, {sAR, 1500}, {sL1Filt, 80}, {sL1Pitch, 3}},
    {{sCutoff, 700}},
    true);

program("FX Dive Bomb",
    {{kVoiceMode, 1}, {kDistDrive, 60}, {kDistMix, 55}, {kRevMix, 30}},
    {{sO1Wave, 0}, {sO2Wave, 0}, {sO2Semi, 7}, {sFType, 0}, {sCutoff, 3000}, {sReso, 20}, {sFEnv, 0},
     {sAS, 100}, {sAR, 800}, {sPegAmt, -48}, {sPegDec, 2500}},
    {{sPegAmt, 48}},
    true);

program("FX Zap Coil",
    {{kVoiceMode, 0}, {kDlyMix, 25}, {kDlyFdbk, 45}, {kDlyDiv, 4}},
    {{sSync, 1}, {sO1Wave, 3}, {sO2Level, 0}, {sPegAmt, 48}, {sPegDec, 60}, {sPegO1, 1}, {sFType, 2}, {sCutoff, 5000},
     {sFEnv, 0}, {sAA, 1}, {sAD, 200}, {sAS, 0}, {sAR, 150}},
    {{sPegAmt, -48}, {sO1Wave, 0}},
    true);

// The feedback loop builds as the filter env opens over four seconds. S2 runs the sweep downward.
program("FX Collapse",
    {{kVoiceMode, 1}, {kRevMix, 70}, {kRevSize, 95}},
    {{sO1Wave, 0}, {sNoise, 40}, {sFeedback, 100}, {sFType, 4}, {sCutoff, 40}, {sReso, 40}, {sFEnv, 80}, {sFA, 4000},
     {sFS, 100}, {sFR, 3000}, {sAA, 2000}, {sAS, 100}, {sAR, 4000}},
    {{sFType, 0}, {sCutoff, 8000}, {sFEnv, -80}},
    true);
