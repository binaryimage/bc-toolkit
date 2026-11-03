# BC Toolkit for PC

BC Toolkit is a companion app for a hardware synth studio built around Ableton Live and an MPC. It monitors every input at once, records from any source, samples synths into playable instruments, backs up patches, and provides tools for sampling, sound design and composition.

This guide covers installation, the first-time setup, and each page of the app.

## Installation

There are two installers. Install one or both.

- **BC-Toolkit-Setup.exe** installs the Toolkit app and the **BC Link** VST3 plug-in.
- **BC-Split-Setup.exe** installs **BC Split**, which lets several applications use the same audio interface at the same time. See the BC Split section below.

Windows may warn that the installer comes from an unknown publisher. Select **More info**, then **Run anyway**.

After installing, open Ableton Live and go to **Settings > Plug-Ins**. Make sure **Use VST3 Plug-in System Folders** is on, then select **Rescan** so that BC Link appears in the browser.

To uninstall either product, use **Windows Settings > Apps**.

## First-time setup

1. Open **Settings** in Toolkit and choose your audio interface. Turn on the inputs your instruments are connected to.
2. Open **Meters**. Each input has its own channel strip. Double-click a strip's name to rename it after the instrument it carries (for example, Hydrasynth or 2600). Names are saved.
3. Decide how Toolkit will receive audio while Ableton is running. See the next section.

Note names follow the Ableton and MPC convention: middle C (MIDI note 60) is C3.

## Audio sources

Every page that records or analyses audio can use any of the following sources.

| Source | What it receives | When to use it |
| --- | --- | --- |
| Interface inputs | The instruments connected to your audio interface | When Toolkit has the interface to itself, or when using BC Split |
| Ableton tracks (BC Link) | The audio of any Ableton track, named after the track | When Ableton has the interface open |
| PC audio | Everything Windows plays through its default output device | For browsers, media players and other Windows audio |

Most ASIO drivers, including the TASCAM Model 12 driver, allow only one application to use the interface at a time. If Ableton has the interface open, use BC Link or BC Split.

### BC Link

BC Link is a VST3 plug-in. Place it at the end of any Ableton track (an instrument track, a return or the Main track). It passes audio through unchanged and sends a copy to Toolkit over the computer's internal network connection (127.0.0.1, port 53317). Each linked track appears in Toolkit under its Ableton track name. If Toolkit is closed, the plug-in waits and reconnects automatically when Toolkit opens.

### PC audio

PC audio captures whatever Windows plays through its default output device. It cannot capture Ableton while Ableton uses an ASIO driver; use BC Link for that.

## BC Split

BC Split is an additional ASIO driver that shares one audio interface between several applications. Each application that selects it receives every input of the interface, and the output of each application is mixed together. This allows, for example, Ableton, Toolkit and Rocksmith to run at the same time.

### Setting up applications

| Application | Setting |
| --- | --- |
| Ableton Live | Settings > Audio > Driver Type: ASIO > Audio Device: BC Split |
| Toolkit | Settings > Device type: ASIO > BC Split |
| Rocksmith (RS_ASIO) | In RS_ASIO.ini, set Driver=BC Split for the output and the input. Split includes a 32-bit driver for Rocksmith. |

### How it works

When the first application opens Split, the **Split Host** starts and appears in the system tray. The host opens the real interface and shares it. It closes automatically 20 seconds after the last application stops using it.

To open the Split window, select **BC Split** in the Start menu or select the tray icon. The window shows:

- the interface in use, its sample rate and buffer size
- each application connected to Split, and how often it has fallen behind
- input level meters
- **Interface panel**, which opens the interface's own control panel
- **Reload**, which reopens the interface after you change its buffer size in the interface's control panel

### Sample rate and buffer size

- All applications run at the interface's sample rate and buffer size. Keep every application at 48 kHz.
- An application that is the only one connected may change the sample rate. While other applications are connected, the sample rate stays fixed.
- Split delays each application's output by two buffers so that every application has time to deliver its audio. At a buffer of 256 samples this adds about 11 ms. Split reports the delay to each application, so recordings in Ableton stay aligned.

### If you hear clicks or gaps

An application that cannot keep up causes short gaps in its own audio. When this happens, the Split window names the application. To fix it:

1. Select **Interface panel** and raise the buffer size (for example, from 128 to 256 or 512).
2. Select **Reload** in the Split window.

For the lowest latency and the most reliable playback while recording in Ableton, you can instead give Ableton the interface directly and feed Toolkit with BC Link.

## Meters

Meters shows every source at once. Each channel strip shows its level, the detected note and its tuning in cents, short-term loudness, and a **HUM** indicator when mains hum (50 or 60 Hz and its harmonics) is present.

Select a strip to show it in the large display, which has five views:

- **Tuner**: the note and its deviation in cents, relative to the A4 reference set beside it.
- **Scope**: a stable, triggered waveform.
- **3D spectrum**: a waterfall of the last few seconds of the spectrum.
- **X/Y**: left against right, for checking stereo width and phase.
- **Loudness**: momentary, short-term and integrated loudness (LUFS) and true peak, with common streaming targets. **Reset integrated** starts a new measurement.

## Record

Choose a source, then select **Record** to start immediately, or **Arm** to start automatically when the signal rises above the threshold. Armed recording keeps a short pre-roll so the attack is not lost. **Longest take** sets a time limit.

After recording you can play the take, name it, use **Trim silence**, select **Save WAV** (saved to Music\Toolkit Recordings), or send it to the Chop station, Wavetables or Mangle.

### Jam recorder

Toolkit continuously records every source into a rolling buffer of 2 to 30 minutes. To keep something you have just played, select **Save last 1 min**, **Save last 5 min** or **Save everything**. Each source is saved as its own WAV file, together with a mix, in a dated folder under Music\Toolkit Jams. Silent sources are skipped.

## Auto-sampler

The auto-sampler plays a synth note by note over MIDI, records each note and builds a multisampled instrument.

1. Choose the MIDI output and channel of the synth, and the source its audio returns on.
2. Set the lowest and highest notes, the interval between sampled notes (minor thirds is a good default), the number of velocity layers, the hold time and the release time.
3. Select **Test note** to check the level, then select **Start sampling**.

Toolkit trims each note, applies short fades, and normalizes all notes by the same amount so that velocity layers keep their relative levels. It saves:

- one WAV file per note and velocity (for example, Dark Pad_C#3_v110.wav)
- an SFZ file
- an MPC keygroup program (.xpm)

Files are saved to Music\Toolkit Samples. When **Also copy to the MPC folders** is on, the instrument is also copied to K:\Toolkit Instruments (when the MPC drive is connected) and to Music\MPC Songs\Toolkit Instruments. The level chart shows each note as it is recorded, so a silent or clipped note is easy to spot.

## Patch backup

Patch backup keeps dated copies of your synth patches. It does not edit patches. Backups are stored under Music\Synth Patches, in one folder per synth; you can change this location.

- **Library folder**: the folder where a synth's editor or librarian stores its patches (for example, Hydrasynth Manager, Leviasynth Manager or MiniFreak V). **Find it** searches Documents and AppData for a folder named after the synth. **Snapshot now** copies the folder into a dated backup.
- **Snapshot changed folders when Toolkit opens**: on startup, Toolkit backs up any library folder that has changed since its last snapshot.
- **SysEx dump**: choose the synth's MIDI input, select **Listen**, then start a dump from the synth or its editor. **Save capture** stores exactly what was received as a .syx file.
- **Send a .syx to the synth** sends a dump back to the synth, with a pause between messages. If a transfer stops partway, increase the pause and try again.
- **Put back into library folder** restores a snapshot. Toolkit first takes a safety snapshot of the folder as it is. Close the synth's editor before restoring.

## Gear check

### Warm-up check

Set up to six synths, each with a MIDI output, channel, return source and test note. Toolkit plays each note and reports how many cents sharp or flat each synth is. Run it after the instruments have warmed up for 15 to 20 minutes.

### Latency

Toolkit sends a note eight times and measures how long it takes to return. It reports the typical round trip and the variation. Enter the result in Ableton as the External Instrument's **Hardware Latency** or as the track delay, so recorded parts line up with the grid.

### 2600 tracking

Toolkit plays C notes across the range through the 2600's MIDI-to-CV converter and measures one oscillator at a time. Turn the other two oscillators down in the mixer first. The chart shows the tuning error at each octave. A steady slope means the oscillator's volts-per-octave scale needs adjusting; the result states the direction and the reference C to retune with Fine tune after each adjustment.

## Chop station

Load a loop, or send a take from Record. Toolkit detects the hits and places a slice marker at each one.

- Adjust **Sensitivity** and select **Find hits** again, or use **8 equal** or **16 equal** for even slices.
- Drag a marker to move it. Alt-click to add a marker. Right-click a marker to remove it.
- Play slices on the 16 pads or with the keys 1 to 8 and Q to I.

**Export to MPC** saves the slices as WAV files, an MPC drum program (.xpm) with the first slice on pad A01, and a MIDI file of the original rhythm at the detected tempo. Exports go to K:\Toolkit Chops and Music\MPC Songs\Toolkit Chops.

## Sample library

Add your sample folders and select **Scan**. Toolkit analyses each file and records its length, tempo, key (when the file is tonal enough to detect one), character (dark, mid or bright; punchy or dense) and type (loop, one-shot or phrase). The first scan of a large library takes some time; later scans analyse only new or changed files.

Filter by type, key, character and tempo. A minor key filter also finds the relative major. Select a row to preview it. Drag a row into Ableton or the MPC software, or send it to the Chop station or Mangle.

## Wavetables

A wavetable is built from keyframes; the table morphs smoothly from one keyframe to the next.

- **Draw** a single cycle with the mouse. Hold Shift to draw a straight line. Alternatively, select **Harmonics** and set the levels of the first 32 harmonics.
- **Start from** a basic waveform, then use the **Shape it** tools: smooth, normalize, mirror, fold, crush, invert, reverse and clear. **Undo** reverses the last change.
- **+ Copy** duplicates the current keyframe, **+ Sine** adds a new one, and the arrow buttons reorder them.
- **Import WAV** opens an existing wavetable or single-cycle file. For any other sound, Toolkit detects the pitch and extracts eight cycles from across the file. **Grab from input** does the same with a note held on a connected synth.
- **Drone** and the computer keyboard (A, W, S, E, D, F, T, G, Y, H, U, J and K; Z and X change octave) play the table. **Position** moves through the table, and **Scan** sweeps through it automatically.

**Export** saves to Music\Toolkit Wavetables in one of three formats:

| Format | Specification | Use |
| --- | --- | --- |
| MiniFreak | 512 points per frame, 24-bit mono, up to 64 frames | Import in MiniFreak V, then sync to the synth |
| Pigments, Serum and Vital | 2048 points per frame | Load directly |
| Single cycles | One 2048-point, 16-bit WAV per keyframe | Any instrument that uses single-cycle waveforms |

## Mangle

Mangle offers two processes, which can be used separately or together.

- **Time stretch** lengthens a sound up to 50 times without changing its pitch. **Smear** sets how diffuse the result is.
- **Tape rot** simulates worn tape: **Wow** and **Flutter** (slow and fast pitch variation), **Drift**, **Hiss**, **Dropouts**, **Tone**, **Saturate** and **Crush**.

The presets (Dust cassette, VHS ghost, Deep freeze, Dead battery and Clean stretch) set all controls at once. Select **Mangle it** to process the sound. The result is not saved until you select **Save WAV** (saved to Music\Toolkit Mangled). **Mangle again** processes the result a second time.

## Pattern generator

The pattern generator creates a chord progression, a bass line and a lead line in minor and modal scales, including Phrygian, natural minor, harmonic minor, Phrygian dominant, Locrian and Hungarian minor.

1. Choose the key, scale, length, bass style (16th notes, 8th notes, sustained or hip-hop) and tempo.
2. Select **Generate** until you have a pattern you want to keep.
3. Under **Playback**, choose the built-in preview sound or your synths over MIDI, with a separate output and channel for chords, bass and lead. Bass notes are retriggered individually, without glide.
4. Select **Save .mid** to save a three-track MIDI file to Music\Toolkit Patterns and the MPC folders, or drag the MIDI button directly into Ableton.

## Patch books

The patch books contain 147 patches: 37 for the Behringer 2600, 25 for the Moog Messenger, 29 for the Hydrasynth, 28 for the Leviasynth and 28 for the MiniFreak. Patch sheets for the 2600 show fader positions and the patch cables to connect; the others list settings by section. Values marked "est." are estimates from the original source, so adjust them by ear.

- Search by name or setting, or filter by category.
- **Save sheet as PNG** saves a printable sheet to Music\Toolkit Patch Sheets.
- **Copy to mine** creates an editable copy. **+ My patch** starts a new patch. Enter one setting per line as Section / Parameter = value. For the 2600, numeric values are shown as faders, and a line such as "cable: VCO 1 saw out -> VCF in" adds a patch cable.

## Settings

Settings contains the audio interface and input selection, BC Link status (the Ableton tracks currently linked), PC audio capture, the status of BC Split, the jam recorder length, the tuning reference and the save locations. Select **Rescan MIDI devices** after connecting new MIDI equipment.

## File locations

| Content | Folder |
| --- | --- |
| Recordings | Music\Toolkit Recordings |
| Jam recorder saves | Music\Toolkit Jams |
| Auto-sampler instruments | Music\Toolkit Samples |
| Wavetables | Music\Toolkit Wavetables |
| Mangle results | Music\Toolkit Mangled |
| Pattern generator MIDI files | Music\Toolkit Patterns |
| Patch sheets | Music\Toolkit Patch Sheets |
| Patch backups | Music\Synth Patches (one folder per synth) |
| MPC exports | K:\ (when connected) and Music\MPC Songs: Toolkit Instruments, Toolkit Chops and Toolkit Patterns |

## Troubleshooting

| Problem | Solution |
| --- | --- |
| No inputs appear in Meters | In Settings, choose the audio interface and turn on its input channels. |
| Ableton and Toolkit cannot use the interface at the same time | Select BC Split in both, or use BC Link in Ableton. |
| An application reports that BC Split cannot start | Open BC Split from the Start menu. The status line explains the problem, which is usually another application using the interface directly. Set that application to BC Split as well. |
| Clicks or gaps while using BC Split | Raise the buffer size in Interface panel, then select Reload in the Split window. |
| A linked Ableton track does not appear | Make sure Toolkit is open. BC Link reconnects automatically within a few seconds. |
| PC audio is silent | PC audio records the Windows default output device only. Check the default device in Windows sound settings. |
| A SysEx restore stops partway | Increase the pause between messages and send the file again. |
| The HUM indicator lights up | Try a different power outlet or strip, use the ground-lift switch on the DI box, or add a ground loop isolator to that instrument. |
