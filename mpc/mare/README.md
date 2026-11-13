# Mare

A nightmare delay for the Akai MPC, by binaryimage. Ping-pong echoes where every repeat is worse
than the last: they sink (or climb) in pitch, flip backwards, smear into a blur and rot, getting
darker, grittier and hissier on each trip around the loop.

50 built-in programs, level-matched.

## Install

1. Your MPC needs **MPC OS 3.10.1 or later**.
2. On the MPC: **Preferences > Enhancements (Beta)**, turn on **Enable Enhancements** and
   **External Plugin Support**.
3. Copy the `binaryimage - VST - Mare` folder into a folder called `VST2 Plugins` at the root of
   a drive, then restart the MPC. Mare shows up as an insert effect under binaryimage.

## Controls

**MARE**
- **Sync** (Free, or 1/32 to 1 bar from the MPC's tempo) and **Free Time** (20 ms to 2 s).
- **Feedback**: up to 100% (it saturates instead of exploding). **Freeze**: holds what's in the
  loop forever and stops new sound coming in.
- **Drift**: pitch shift on every repeat (±12 st). Small values like -0.3 make the echoes sink
  slowly; -12 drops each one an octave.
- **Reverse**: the odds that each repeat flips backwards. **Rot**: each repeat gets darker,
  crushed, saturated and hissy. **Smear**: blurs the repeats into a wash.
- **Mix**: the dry signal stays at full level up to 50%, and the echoes reach full level at 50%.
  **Output**.

**TAIL**: **Wobble** and **Wob Rate** (wow on the delay time), **Ping-Pong** (0 = straight
stereo, 100 = bouncing), **Duck** (pushes the echoes down while you play), **Tone**, **Low Cut**.

## Notes

- Changing the time glides like a tape delay rather than jumping.
- CPU: about 1% of a desktop core per instance; expect a few percent on the MPC.

All DSP is original binaryimage code (MIT).
