# Behemoth

An analog-style drum machine for the Akai MPC, by binaryimage: a 12-piece kit with every drum on
its own pad, plus the **Mother**, a DFAM-style voice with an 8-step pitch and velocity
sequencer. It has **8 stereo outputs**, so any drum can go to its own mixer channel.

32 built-in kits, level-matched: EBM, industrial, metal clank, Ministry-style, boom bap, 808-ish,
distorted techno, ritual toms, and 10 Mother loop kits.

## Install

1. Your MPC needs **MPC OS 3.10.1 or later**.
2. On the MPC: **Preferences > Enhancements (Beta)**, turn on **Enable Enhancements** and
   **External Plugin Support**.
3. Copy the `binaryimage - VST - Behemoth` folder into a folder called `VST2 Plugins` at the root
   of a drive, then restart the MPC. Make a plugin track and pick Behemoth.

## Pads

| Pad | Note | Sound | Pad | Note | Sound |
|---|---|---|---|---|---|
| 1 | C1 | Kick | 9 | G#1 | Closed Hat (chokes the open hat) |
| 2 | C#1 | Kick 2 (long, subby) | 10 | A1 | Open Hat |
| 3 | D1 | Snare | 11 | A#1 | Cymbal |
| 4 | D#1 | Clap | 12 | B1 | FM Drum |
| 5 | E1 | Rim | 13 | C2 | Mother: hit and step the sequence |
| 6 | F1 | Low Tom | 14 | C#2 | Mother: start the sequence |
| 7 | F#1 | Mid Tom | 15 | D2 | Mother: stop the sequence |
| 8 | G1 | High Tom | 16 | D#2 | Mother: back to step 1 |

The layout repeats every 16 notes up the keyboard. Velocity sets the accent (**Accent** on MAIN
sets how much).

## Every drum

Tune, Decay, Level, Pan, Drive, an **Out** (Main or Out 2-8), and two character knobs:
Kick (Punch, Click), Kick 2 (Sweep, Sub), Snare (Snappy, Tone), Clap (Spread, Tone), Rim (Click,
Ring), Toms (Bend, Noise), Hats and Cymbal (Tone, Metal: 808-style metal vs noise), FM Drum (FM,
Ratio).

## The Mother

Two VCOs (triangle or square), VCO1>VCO2 FM, hard sync and noise, into a ladder filter (low or
high pass), with decay envelopes on pitch, filter and amp. The 8-step sequencer has a **pitch**
and a **velocity** per step. A step at 0 velocity is a rest. Velocity can open the filter (**Seq
Vel>VCF**), and the pitch can go to both VCOs or one of them.

**Seq Mode**: **Pads** (default: pads 14/15 start and stop it, locked to the MPC's tempo),
**Transport** (runs whenever the MPC plays), or **Off** (pad 13 only). **Seq Rate** goes from
1/32 to 1/4, plus **Swing**.

## Outputs and routing

Behemoth has 16 output channels: Main plus Out 2 to Out 8 (stereo pairs). Akai's SDK says the MPC
can send each pair to its own mixer channel, so a drum on its own output gets its own effects and
fader.

- **Routing** (MAIN page): **All Main** (default, needs no setup), **Kick/Snare** (kicks on Out 2,
  snare/clap/rim on Out 3), **Groups** (kicks 2, snare/clap/rim 3, toms 4, hats/cymbal 5, FM 6,
  Mother 7), **Every Drum** (kick 2, kick 2 on 3, snare 4, clap+rim 5, toms 6, hats/cymbal 7,
  FM 8), or **Custom** (each drum's own Out setting).
- If the MPC gives the plugin fewer outputs than a drum asks for, that drum plays on Main, so
  nothing ever goes silent.
- I couldn't test the multi-out routing on hardware, and Akai's own example only uses two pairs.
  Look for the plugin's extra outputs in the MPC's mixer and track output settings.

## Bus

**Grit** (saturation) and **Glue** (gentle compression) run on each output pair. **Volume**.

## Notes

- CPU: about 1.5% of a desktop core with the full kit and the Mother playing.
- All DSP is original binaryimage code (MIT).
