# Dahaka FM for MPC

4-operator FM (Leviasynth-style): 8 algorithms, operator feedback, 6 operator waves, per-operator envelopes. This is one of the three heads of binaryimage's Dahaka synth, split out as its own MPC
instrument. The other two are Dahaka FM, Dahaka Wave and Dahaka VA, minus this one.

The sound engine is the desktop Dahaka's own voice code (`src/dahaka_core.h`) with JUCE taken
out. The parameter IDs are the same as the desktop's, so the desktop presets carried over.

**What every head shares:** noise, drive into a state-variable filter (LP12, LP24, Band, HP12,
Notch), filter, amp and mod envelopes, two LFOs (6 shapes), a 6-slot mod matrix, and 12-voice
poly, mono or legato. Glide is off by default.

**Left out:** Dahaka's arpeggiator and FX rack (put Chimera after it instead), and the serial
cross-engine routing, since each head has one engine. The desktop renders at 2x oversampling;
this runs at the host rate to save CPU.

**Presets:** 32, level-matched. Prefixes: BA bass, RF riff/heavy, PD pad, DR drone, PL or KY
pluck/keys, PC perc, BL bells, VX vocal, LD lead, FX. The un-prefixed ones are the desktop
Dahaka's own presets, keeping just this engine's part.

**Output:** the voice is mono, like the desktop synth before its FX. It's the same on both
channels, so pan it on the MPC mixer or put Chimera after it for width.

MIT licence. Built with Akai's MPC Plugin SDK (MIT).
