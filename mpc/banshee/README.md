# Banshee

A sorrow chorus for the Akai MPC, by binaryimage. Wide, detuned and never still: up to six
chorus voices that wander off on their own, a pitched **Wail** riding on top, a sweeping vowel
**Cry** that makes the whole thing moan, breath noise that follows your playing, and a dark
reverb behind it all.

50 built-in programs, level-matched.

## Install

1. Your MPC needs **MPC OS 3.10.1 or later**.
2. On the MPC: **Preferences > Enhancements (Beta)**, turn on **Enable Enhancements** and
   **External Plugin Support**.
3. Copy the `binaryimage - VST - Banshee` folder into a folder called `VST2 Plugins` at the root
   of a drive, then restart the MPC. Banshee shows up as an insert effect under binaryimage.

## Controls

**BANSHEE**
- **Voices** (2, 3, 4 or 6), **Rate** and **Depth** (the chorus sweep), **Drift** (each voice
  wanders slowly at random, for that seasick feel).
- **Detune**: real pitch detune in cents (left sharp, right flat), so it stays sour even when
  the sweep is slow.
- **Wail** and **Wail Pitch** (-12 to +24 st): a pitch-shifted ghost with its own sad vibrato.
- **Cry**: a vowel resonance that sweeps between "oo" and "ah".
- **Space**: a dark reverb. **Mix** (dry stays full up to 50%), **Output**.

**SPIRIT**: **Cry Rate**, **Air** (breath noise, only while you play), **Feedback** (flanging,
up to jet-plane), **Width**, **Tone**, **Low Cut**.

## Notes

- CPU: about 2% of a desktop core per instance; expect several percent on the MPC with 6 voices
  and Space on. Use it on a return track if you stack lots of them.

All DSP is original binaryimage code (MIT).
