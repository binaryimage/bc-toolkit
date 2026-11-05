BC Split
=================

Lets Ableton, Toolkit, Rocksmith and any other ASIO app use the same audio interface (the TASCAM Model 12)
at the same time. Each app gets every input; whatever they play is mixed together.

Point each app at it
  Ableton:    Preferences > Audio > Driver Type ASIO > Audio Device "BC Split"
  Toolkit:    Settings > ASIO > "BC Split"
  Rocksmith:  in RS_ASIO.ini set  Driver=BC Split  under both [Asio.Output] and [Asio.Input.0]
              (Rocksmith is 32-bit; Split installs a 32-bit driver for it)
  Anything else that uses ASIO: pick "BC Split" as its device.

How it works
  The first app that opens Split starts the Split Host (tray icon). The host opens the real interface and
  shares it; it quits by itself 20 seconds after the last app lets go. Open "BC Split" from the
  Start menu to pick the interface, sample rate and buffer size, see which apps are on it, and open the
  interface's own control panel.

Good to know
  - Every app runs at the same sample rate and buffer (set in the Split window). Keep them at 48 kHz.
    An app on its own can change the rate; with other apps attached it stays put.
  - Outputs go out two buffers later than direct (about 11 ms at 256 samples), or three with Safety
    set to 3 buffers. Split reports it, so DAWs line recordings up.
  - If an app says Split won't start, open the Split window: the status line says why (usually some app
    has the TASCAM driver open directly; point that one at Split too).
  - A "late blocks" count next to an app means it missed its turn (it says when the last one was).
    Split fades a late app out and back in, so you hear a short dip instead of a click. If it keeps
    happening, set Safety to 3 buffers, or raise the buffer.

Uninstall from Windows Settings > Apps.
