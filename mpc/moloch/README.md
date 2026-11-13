# Moloch

A pitch nightmare for the Akai MPC, by binaryimage. Up to three pitch-shifted copies of your
sound. On every step of the clock each one may jump to a new random interval, picked from a
scale and range you choose, and glide there. The output feeds back through a **Climb** shifter,
so the sound can spiral up or sink forever. Nothing here is meant to be polite.

50 built-in programs, level-matched.

## Install

1. Your MPC needs **MPC OS 3.10.1 or later**.
2. On the MPC: **Preferences > Enhancements (Beta)**, turn on **Enable Enhancements** and
   **External Plugin Support**.
3. Copy the `binaryimage - VST - Moloch` folder into a folder called `VST2 Plugins` at the root
   of a drive, then restart the MPC. Moloch shows up as an insert effect under binaryimage.

## Controls

**MOLOCH**
- **Scale**: which intervals the voices may jump to: Chromatic, Octaves, Fifths, Tritones,
  Phrygian, Diminished, or Microtonal (any pitch at all).
- **Range**: how far (up to ±24 semitones). **Chaos**: odds of a jump on each step.
  **Slew**: glide time, as a share of the step.
- **Sync** (Free, or 1/32 to 2 bars on the MPC's clock) and **Free Time**.
- **Hold**: freezes the current pitches. **Bend**: shifts everything up or down (±24 st), great
  for automating dives.
- **Mix**, **Output**.

**SPIRAL**
- **Voices** (1 to 3), **Spread** (stereo), **Detune** (random cents on every jump), **Tone**.
- **Feedback** and **Climb**: each trip around the loop moves the pitch by Climb, so it spirals.
- **Grain**: shifter window. Short is buzzy and metallic, long is smooth and smeared.
  **Smear**: scatters the grains.

## Notes

- CPU: about 1% of a desktop core per instance; expect a few percent on the MPC.

All DSP is original binaryimage code (MIT).
