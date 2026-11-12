# Janus

A 10-voice virtual analogue synth for Akai MPC standalone hardware, by binaryimage.

Janus is built around the ideas that made the Yamaha AN1x special. It is **not** an emulation of
Yamaha's code or chips, and it has no connection to Yamaha. Every patch has two complete
**Scenes**, and you morph between them with the mod wheel or the Morph knob.

## Install

1. The MPC needs **MPC OS 3.10.1 or later**.
2. On the MPC: **Preferences > Enhancements (Beta)**, turn on **Enable Enhancements** and
   **External Plugin Support**.
3. Unzip, and copy the `binaryimage - VST - Janus` folder into a folder called `VST2 Plugins` at the
   root of a USB stick, an SD card or the internal drive.
4. Restart the MPC, make a plugin track, and pick **Janus** under External VST Plugins.

The same folder works on 32-bit MPCs (Live, Live II, One, X, Key 61, Key 37, Force) and 64-bit
ones (Live III, XL).

## How it is laid out

| Page | What is on it |
|---|---|
| MAIN | Morph, Volume, Voice Mode (Poly / Mono / Unison), Wheel Morph, unison voices/detune, stereo spread, glide, velocity and aftertouch amounts, bend range, Copy S1>S2 / S2>S1 |
| S1 OSC / S2 OSC | Osc 1 and 2 (Saw / Pulse / Tri / Sine, PW), Osc 1 pitch, Osc 2 semi/fine, levels, Ring, Noise, Sync, FM, Feedback |
| S1 FILT / S2 FILT | Cutoff, Reso, filter env amount, key track, filter type (LPF24 / LPF18 / LPF12 / BPF / HPF), drive, HPF, filter ADSR |
| S1 AMP / S2 AMP | Amp ADSR, pitch envelope (amount, decay, Osc 1 only), LFO 1 and LFO 2 depths |
| LFO | LFO 1 wave/rate (or tempo sync and division), LFO 2 wave/rate |
| FX | Distortion, chorus/flanger, tempo ping-pong delay, reverb |

The MPC shows five page keys at a time. Press **>** for the rest.

## The signal path

Per voice: Osc 2 is the master; Osc 1 can hard-sync to it, and Osc 2 can frequency-modulate it.
The mixer adds Osc 1, Osc 2, ring mod (Osc 1 x Osc 2), noise, and **feedback** from the voice's
own output. That feeds a saturating 4-pole ladder filter with pole-mixed modes, then a 12 dB
non-resonant high-pass, then the amp. All voices are summed in stereo, then go through
distortion, chorus/flanger, delay and reverb.

## Scenes and morphing

- Every sound parameter on the S1/S2 pages exists once per Scene.
- **Morph** at 0 % plays Scene 1, at 100 % Scene 2. With **Wheel Morph** on, the mod wheel adds
  to it. With it off, the wheel does vibrato instead.
- Knobs glide between the Scenes. Cutoff and times glide on a log scale so sweeps sound even.
  Switches (waveforms, filter type, Sync, PEG O1 Only) flip at the half-way point.
- **Copy S1>S2** copies the whole Scene. Build Scene 1, copy it, then change Scene 2.

## Presets

Init, Morph Grind Bass, Sync Wire, Cinder Pad, Furnace Unison, Ring Clank, Feedback Drone,
Sub Pressure. Every one of them does something with the mod wheel.

## Not in this version

The AN1x's Free EG, arpeggiator and step sequencer are left out. The MPC does the sequencing.

## CPU

On a desktop CPU, ten voices with every effect on use about 2 % of one core. Expect several times
that on the MPC. Check it on the device.

## Build

```
python tools/build.py plugins/Janus      # inside Akai's MPC Plugin SDK
python tools/package.py plugins/Janus
```

MIT licence. Built with Akai's MPC Plugin SDK (MIT).
