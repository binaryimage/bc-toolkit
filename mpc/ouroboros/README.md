# Ouroboros

A chaotic reverser for the Akai MPC, by binaryimage. It records everything you play and, on each
step of the clock, may grab what just went by and throw it back at you **backwards**: at its own
pitch and length, crossfaded in and out so it never clicks.

50 built-in programs, all level-matched.

## Install

1. Your MPC needs **MPC OS 3.10.1 or later**.
2. On the MPC: **Preferences > Enhancements (Beta)**, turn on **Enable Enhancements** and
   **External Plugin Support**.
3. Copy the `binaryimage - VST - Ouroboros` folder into a folder called `VST2 Plugins` at the root
   of a drive, then restart the MPC. Ouroboros shows up as an insert effect under binaryimage.

## Modes

- **Chaos**: every step is a coin toss (**Chaos** sets the odds). Heads you hear the live sound,
  tails you hear the last moment reversed.
- **Backmask**: always reversed, step by step, like a reverse delay. Chaos varies the chunks and
  leaves gaps.
- **Swell**: the reversed chunks fade in, so every hit is announced by its own ghost.
- **Spiral**: reversed chunks feed back into the loop with their pitch shift, so the snake eats
  its own tail and climbs or sinks.

## Controls

**REVERSE**: Mode, **Sync** (Free or 1/32 to 2 bars, locked to the MPC's clock while it plays),
**Free Time** (step length when Sync is Free), Chaos, **Length** (chunk length vs the step),
**Jitter** (random lengths and start points), **Freeze** (stops recording; it keeps chopping
what's in the loop), **Flip** (reverses every step while it's on), Mix, Output.

**SHAPE**: **Pitch** of the reversed chunks (±12 st; high Chaos also throws octave jumps),
**Feedback**, **Fade** (crossfade length), **Width** (how often left and right do different
things), **Ghost** and **Ghost Size** (reversed-reverb swells on every step), **Tone**.

## Notes

- Freeze and Flip are buttons: map them to pads or Q-Links.
- CPU: about 1-2% of a desktop core per instance; expect a few percent on the MPC.

All DSP is original binaryimage code (MIT).
