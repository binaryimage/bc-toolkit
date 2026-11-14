BC Visualizer
=============

Turns a track into a music video for YouTube: 101 audio-reactive styles, your title and
artist name, optional cover art, exported as an MP4 (H.264 video, AAC audio).

Quick start
-----------
1. Open track... (or drag a WAV, AIFF, FLAC, MP3 or OGG onto the window).
2. Pick a style on the left. Press Space to play and preview it with sound.
   Up and Down arrows step through the styles.
3. On the right, set the title (it defaults to the file name) and artist (binaryimage).
4. Optional: Cover art... adds your artwork next to the title and, with Background set
   to "Cover art (dimmed)", behind the visuals too. You can also drop an image on the window.
   Turn off "Cover next to title" to keep it as the background without the small copy by your name.
5. Choose the size (1080p, 1440p or 4K), frame rate and codec, then Export MP4.

Videos are saved to Music\BC Visualizer. You can keep previewing while one renders, and
Cancel stops it. Upload the MP4 to YouTube as it is; no conversion needed.

Patterns (switching styles during the video)
--------------------------------------------
The Patterns tab on the right:
- Single: one style all the way through (the one picked on the left).
- Picked: switches between the styles you pick (Pick styles...), in order or shuffled.
- Random: switches between random styles. Re-roll gives a different order.
- Follow the song switches when the music changes section (never sooner than Shortest,
  never later than Longest) and hard-cuts when it slams back in. Every few secs switches
  on a timer instead. Fade sets how long the crossfades are.
The strip under the preview shows which style plays when; click it to jump there.
What you preview is exactly what gets exported.

Fractals and random colours
---------------------------
- 34 of the styles are fractals (21 new in 2.3: Newton Basins, Nova, Phoenix Feathers,
  Tricorn, Multibrot Morph, Magnet, Lyapunov Zircon, Pickover Stalks, the 3D ones Mandelbulb,
  Mandelbox, Menger Tunnels, Apollonian Foam and Sierpinski Tetra, Barnsley Fern, Hopalong,
  Thomas Drift, Buddhabrot, Hilbert Curve, Levy Lightning, Apollonian Gasket and T-Square).
- In Pick styles, the Fractals button picks all of them in one click. With Shuffle on you get
  random fractals only.
- New colours on every switch (Patterns tab) rolls a brand new palette of 3 to 8 colours at
  every switch, fading in with the style. It overrides the Colour on the Look tab. Re-roll
  changes the colours too; the strip under the preview shows each part's colours.
- The colour list has 12 new long palettes (6-8 colours) made for fractals, from Hellscape
  to Witch Spectrum.
- Colour flow (Look tab): Gentle is the slow drift, Flowing and Racing pour the colours
  through the picture, faster when the music is loud.

Exporting
---------
- Exports use every core. The status line shows the render speed, how long is left, and
  the preview shows the frame being rendered while you're not playing.
- Codec: H.264 plays anywhere. HEVC makes smaller files; it's only offered when this PC has
  an HEVC encoder (a recent graphics card, or Microsoft's HEVC Video Extensions).
- Keep the PC responsive renders on half the cores with short breaks, so Ableton and the
  rest stay smooth. Slower export.

Full screen
-----------
F11 or double-click the preview. Esc, F11 or double-click again to come back.

Tips
----
- Intensity scales how hard the visuals react. Turn it down for ambient tracks and up
  for quiet masters.
- 1440p uploads get YouTube's better encoder even for 1080p viewers, so they look sharper.
- 60 fps looks smoother on fast styles (Warp Field, Tunnel, Glitch) but takes twice as long
  to render.
- Reactions (Off / Subtle / Wild) is a layer on top of every style that listens to 32 frequency ranges
  and to the character of the sound, and gives each its own job:
    sub: the picture breathes        bass hits: punch and shake      low mids: slow sway
    mid hits: kaleidoscope flicker   presence hits: glitch slices     highs: glints
    air: shimmer                     noise and hiss: static           crunch/distortion: colour split
    rising pitch: zoom in            falling pitch: zoom out           panning: slide and lean
    wide stereo: left/right ghosts   busy parts: echoes               drones: slow sway
    reverb tails and gaps: trails    silence: dissolves               swells: everything intensifies
    drops: flash and slam            a new section: the picture flips to a new mirror mode and the
                                     colours jump
  Nothing needs a steady beat; ranges that aren't in a track just don't fire.
- Colour changes the ink: White, Ice, Amber, Red, Phosphor green, Violet, Multi-colour, Ice multi-colour,
  or one of 40 mixed palettes (Toxic Bloom, Blood Moon, Thermal, Oil Slick, Nebula, Hellfire...).
  Multi-colour paints any style in a rainbow that spirals out from the centre and turns with
  the music; Ice multi-colour does the same in cold cyans, blues and violets. Try them on Moth (the newest style), Kaleidoscope or Mandala.
  Background "Paper" gives you black ink on off-white.
