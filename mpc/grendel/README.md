# Grendel

A tape abomination for the Akai MPC, by binaryimage. Put it on any track or return and wreck the
sound like a cursed tape deck: drive, wow, flutter, dropouts, chewed tape, crushed bits and hiss,
plus a **Tape Stop** button, a **Rewind** button and a **Possessed** knob that throws all of it at
you at random (or on the beat, with **Grid**).

50 built-in programs, all level-matched so switching between them doesn't jump in volume.

## Install

1. Your MPC needs **MPC OS 3.10.1 or later**.
2. On the MPC: **Preferences > Enhancements (Beta)**, turn on **Enable Enhancements** and
   **External Plugin Support**.
3. Copy the `binaryimage - VST - Grendel` folder into a folder called `VST2 Plugins` at the root
   of a drive (SD card, USB stick or internal drive), then restart the MPC.
4. Grendel shows up as an insert effect under binaryimage.

## Pages

**GRENDEL** (performance)
- **Tape Stop**: hold it on and the tape slows to a halt over **Stop Time**. Turn it off and it
  spins back up over **Start Time**.
- **Rewind**: drags the tape backwards, faster and faster. Let go and it snaps back to live.
- **Possessed**: random events on a second play head: brakes, backwards grabs, warbles, chewed
  jumps and wow spikes. At 0 nothing happens; at 100 it's out of control.
- **Grid**: Free fires events at random times; 1/16 to 1/1 puts them on the beat.
- **Tape Eat**: chewed-tape glitches: jumps back, jerky speed and chatter.
- **Dropouts**: the oxide flakes off, so the level dips and the top goes away for a moment.
- **Mix / Output**

**TAPE** (the deck itself)
- **Drive**: tape saturation. **Age**: head bump, dull top, thin bottom. **Tone**: dark to bright.
- **Hiss**: tape noise.
- **Wow / Wow Rate**: slow, seasick pitch drift. **Flutter**: fast shimmer. **Width**: how
  differently left and right wobble (0 = mono deck).
- **Crush**: sample-rate and bit reduction.

## Notes

- The tape runs 12 ms behind the input. The dry signal is delayed to match, so Mix never combs.
- Tape Stop and Rewind are buttons you can map to a Q-Link or pad, or automate.
- CPU: about 1% of a desktop core per instance; expect a few percent on the MPC.

## Build

```
python tools/build.py plugins/Grendel
python tools/package.py plugins/Grendel
```

All DSP is original binaryimage code (MIT).
