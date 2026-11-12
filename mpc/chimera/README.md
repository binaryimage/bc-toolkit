# Chimera for MPC

binaryimage's Chimera effect pedal as an insert effect for Akai MPC standalone hardware. The DSP
is the desktop Chimera's own code. Only the master EQ was rewritten, as plain biquads with the
same curves, because the desktop one used JUCE.

**Chain:** Juno-60-style chorus, then a triple delay (A/B/C, with Digital, Tape, Analog, Lo-Fi,
Mod, Reverse and Ping-Pong types, serial or parallel, tempo sync), then the reverb (Room, Hall,
Plate, Spring, Shimmer, Cloud, Bloom, Swell, with Freeze). That goes into a mixer with level, pan,
mute and solo for the dry, chorus, delay and reverb channels, then the 5-band EQ and Output.
**Chain = Serial** makes each effect also feed the next one, like pedals in a row.

**Pages:** CHORUS, DLY A/B, DLY C, REVERB, MIXER, EQ. A delay's Time knob only shows when its
Sync is set to Free.

**Reverb Special** does something different for each type: Early reflections (Room and Hall),
Density (Plate), Dwell (Spring), Shimmer amount, Diffusion (Cloud), Bloom, and Rise (Swell).

**CPU:** this is heavier than a simple delay, because the reverb is an 8-line FDN. Expect
roughly 10-20 % of one core on a Live III. Check it on the device.

MIT licence. Built with Akai's MPC Plugin SDK (MIT).
