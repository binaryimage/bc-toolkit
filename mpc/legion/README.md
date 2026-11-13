# Legion

A choir synth for the Akai MPC, by binaryimage. *My name is Legion, for we are many.*

Every note you play is a small crowd of singers. Each singer is slightly off pitch, drifts on
their own, has their own vibrato speed and breath, like real people standing side by side. Their
voices go through five formant filters that shape the vowel ("mmm", "ooh", "oh", "aah", "eh",
"ee"), using published formant measurements for bass, tenor, alto and soprano singers. **Morph**
crossfades from the choir into a synth built from the same singers, for augmented, half-human
sounds.

128 built-in programs, level-matched: classic choirs, dark and horror choirs, augmented
choir-synths, whispers, solo voices, weird FX, choir basses and stabs.

## Install

1. Your MPC needs **MPC OS 3.10.1 or later**.
2. On the MPC: **Preferences > Enhancements (Beta)**, turn on **Enable Enhancements** and
   **External Plugin Support**.
3. Copy the `binaryimage - VST - Legion` folder into a folder called `VST2 Plugins` at the root
   of a drive, then restart the MPC. Make a plugin track and pick Legion.

## Pages

**CHOIR**
- **Section**: Bass, Tenor, Alto, Soprano, or **Auto** (picks the section by the note you play,
  like a real choir splitting the parts).
- **Vowel**: Mm, Uu, Oo, Ah, Eh, Ee, blending smoothly in between.
- **Open**: each note starts as a closed "mmm" and opens to the vowel as it swells.
- **Formant**: moves the vowel shapes up or down (±12 st). Down for giants and demons, up for
  children and chipmunks.
- **Singers** (1 to 8 per note), **Spread** (how out of tune they are with each other),
  **Drift** (slow wandering), **Breath** (100% = whispers), **Brightness**, **Consonant** (a
  breath or hiss at the start of each note), **Volume**, **Octave**.

**MOTION**: **Vowel LFO / Vowel Rate** (the vowel wanders), **Vibrato**, **Vib Rate**,
**Vib Delay** (vibrato fades in after the note starts), **Wheel Dest** (mod wheel to Vowel,
Morph, Vibrato, Formant or Open), **Bend Range**, **Glide** (Off by default), **Velocity**.

**SYNTH**: **Morph** (choir to synth), **Synth Wave** (Saw, Pulse, Hollow), **Cutoff**,
**Reso**, **Flt Env** (the filter follows the amp envelope), **Sub** (sine an octave down).

**ENV**: **Attack, Decay, Sustain, Release**. The sustain pedal (CC64) works.

**FX**: **Ensemble**, **Width**, **Tone**, **Echo** (ping-pong, synced to the MPC's tempo),
**Echo Time**, **Echo Fbk**, **Hall**, **Hall Size**.

## Notes

- 8 notes of polyphony.
- CPU: about 2-4% of a desktop core with 8 notes and 8 singers each. On the MPC that's a real
  chunk of a core, so if you stack several instances, lower **Singers** (4 sounds nearly as
  big) or bounce parts to audio. On the older 32-bit MPCs (Live, One, X) start with 4 singers.
- The formant data is the classic public singer formant table. Everything else is original
  binaryimage code (MIT).
