# Fenrir

Brutal distortion for the Akai MPC, by binaryimage. Two drive stages, each with six kinds of
filth, a resonant **Bite** for that industrial snarl, **Starve** for dying-battery sputter,
**Howl** feedback that screams, a noise gate, and a rhythmic **Chop** gate locked to the MPC's
clock.

50 built-in programs, level-matched so the loudness doesn't jump when you switch.

## Install

1. Your MPC needs **MPC OS 3.10.1 or later**.
2. On the MPC: **Preferences > Enhancements (Beta)**, turn on **Enable Enhancements** and
   **External Plugin Support**.
3. Copy the `binaryimage - VST - Fenrir` folder into a folder called `VST2 Plugins` at the root
   of a drive, then restart the MPC. Fenrir shows up as an insert effect under binaryimage.

## Controls

**FENRIR**
- **Drive 1 / Type 1** and **Drive 2 / Type 2** (Drive 2 at 0 = off). Types: **Tube**
  (warm, uneven), **Fuzz** (square and fat), **Rectify** (octave-up ring), **Fold** (wavefolder,
  metallic), **Diode** (hard, lopsided clip), **Crush** (quantised steps).
- **Bite / Bite Freq**: a resonant boost before the dirt, so it snarls at that frequency.
- **Starve**: the bias sags with your playing, so notes spit and choke as they fade.
- **Howl**: feeds the output back through a band-pass. It screams while you play and dies when
  you stop.
- **Tight**: low cut before the dirt (keeps bass from turning to mud). **Gate**: noise gate
  threshold (Off at the bottom). **Mix**, **Output**.

**CHOP/EQ**
- **Chop**: Off, Pulse, Gallop, Upbeat, Machine, Broken, Stutter or Random, at **Chop Rate**
  (1/32 to 1/4, following the MPC's tempo and position). **Chop Depth** and **Chop Shape** (hard
  to soft edges).
- **Low**, **Mid** (scoop or honk), **High Cut**, **Width** (left and right distort differently).

## Notes

- The drive stages run at 2x oversampling to keep the fizz down.
- CPU: about 1% of a desktop core per instance; expect a few percent on the MPC.

All DSP is original binaryimage code (MIT).
