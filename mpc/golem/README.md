# Golem

An ARP 2600-style semi-modular synth for the Akai MPC, by binaryimage, with a 1601-style step
sequencer and **30 colour-coded patch cables you plug in by touch**.

64 built-in programs, level-matched: sync screams, 4012 and 4072 filter screamers, basses, S&H
bleeps, ring-mod bells and clanks, 2600 drums, drones, and 16 sequencer patterns.

## Install

1. Your MPC needs **MPC OS 3.10.1 or later**.
2. On the MPC: **Preferences > Enhancements (Beta)**, turn on **Enable Enhancements** and
   **External Plugin Support**.
3. Copy the `binaryimage - VST - Golem` folder into a folder called `VST2 Plugins` at the root of
   a drive, then restart the MPC. Make a plugin track and pick Golem.

## The synth

Like a real 2600, Golem makes sound with nothing patched, because the main connections are wired
behind the panel:

- **3 VCOs** with every waveform out at once. VCO1 is FM'd by VCO2's sine (**VCO1 FM**). VCO2 is
  FM'd by VCO1's square (**VCO2 FM**), its pulse width swept by VCO3 (**VCO2 PWM**), and it can be
  hard-**Synced** to VCO1. VCO3 is FM'd by noise (**VCO3 FM**), can drop to **LF** (an LFO) and can
  leave the keyboard (**VCO3 KBD**).
- **Mixer**: VCO1/2/3 (pick the wave for each), **Noise** (white, pink or low) and the **Ring Mod**.
- **VCF**: four-pole ladder, switchable between the **4012** (the early, fat, Moog-style one)
  and the **4072** (ARP's own, brighter and nastier). **Resonance** goes all the way to
  self-oscillation. **VCF KBD** at 100% tracks the keyboard 1:1. **VCF Env** comes from the ADSR,
  and **VCF FM** is audio-rate FM from VCO2's sine. **Drive** pushes the filter.
- **ADSR** and **AR** envelopes, **VCA Env** picks which one opens the VCA, **VCA Init** holds it
  open for drones.
- **S&H** (noise or a VCO, free or synced to tempo) into a **Lag**, the **spring reverb** (boingy
  on purpose), **Pan**.
- **Mono** (last note) or **Duo**: VCO1 plays your lowest held note, VCO2 and VCO3 the highest,
  through one filter, like the 2600's duophonic keyboard. **Glide** is off unless you turn it up.

## Patching (PATCH page)

- **Tap an output jack, then an input jack.** The cable goes into the next free slot, and both
  jacks light up in that cable's colour with its number in the middle.
- **Tap a lit input jack** (with no output picked) to pull its cable out.
- An output that feeds more than one cable gets a **striped ring**. Each input takes one cable.
  Patching a used input moves that cable over to the new source.
- **Unplug All** clears every cable.
- Each cable has an **amount** (-100% to +100%) on the **AMT** pages, with the same colour chip
  and number. The **CBL** pages show every cable's From and To as menus, if you'd rather patch by
  knob.
- Plugging into **VCF IN**, **RING IN**, **S&H IN** or **LAG IN** breaks that normal, like a real
  jack. Every other input adds to what's already there.
- Pitch inputs: KBD CV or Seq A into a pitch input at +100% tracks 1:1. Gate inputs (ADSR GATE,
  AR GATE, S&H CLK, SEQ CLK, SEQ RST) fire when the signal rises past half way.

## Sequencer (SEQ pages)

Modelled on ARP's 1601: 16 steps with three rows of sliders. **Row A** is pitch, **rows B and C**
are spare CV you can patch anywhere (they're the SEQ A/B/C output jacks), and so is the step
**CLOCK**.

- **Seq Mode**: **Sync** follows the MPC's clock while it plays (and you can play Golem normally
  while it's stopped), **Free** runs on its own clock at **Seq Free Hz**, **Key** runs while you
  hold a key.
- **Seq Rate** (1/32 to 1/2), **Seq Length** (1 to 16), **Seq Dir** (Forward, Backward,
  Pendulum, Random), **Quantize** (Off for free CV, or Chromatic, Minor, Phrygian), **Seq Range**
  (1 to 4 octaves), **Seq Gate** (note length), **Seq Transpose** (hold a key to transpose the
  pattern from C3).
- **STEPS** page: each step is **Play**, **Rest**, **Skip** (skipped completely, so the pattern
  gets shorter) or **Tie** (holds into the next step without retriggering).

## Notes

- Touch patching works by the plugin changing its own cable settings when you tap. If the jacks
  don't light up right away on your MPC, every cable can still be set from the CBL pages.
- CPU: about 1.5% of a desktop core. It's a mono/duo synth, so it's light.
- All DSP is original binaryimage code (MIT).
