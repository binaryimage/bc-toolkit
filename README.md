# BC Toolkit for PC

A companion app for a hardware synth studio built around Ableton Live and an MPC, plus **BC Split**, an ASIO driver that lets several applications share one audio interface.

- `toolkit/pc`: the BC Toolkit app (JUCE), the BC Link VST3 and the BC Split host
- `toolkit/pc/split`: the BC Split ASIO driver (64- and 32-bit) and its tests
- `toolkit/pc/installer`: Inno Setup scripts for the two installers

Windows builds, tests, installers and releases are produced by GitHub Actions (`.github/workflows/toolkit-pc.yml`). Download the installers from the Releases page.

The user guide is `toolkit/pc/Resources/tutorial.md`.

Built on JUCE (AGPLv3) and the Steinberg ASIO SDK headers that ship with it (GPLv3). Fonts: IBM Plex (SIL Open Font License).
