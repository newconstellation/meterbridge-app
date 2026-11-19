# Meter Bridge

A standalone Mac app from New Constellation that meters audio inputs: Dante
Virtual Soundcard channels, Pro Tools aux outputs, or any Core Audio device.
Up to four sources at a time are overlaid on one spectrum, so you can see
which source (for example which keyboard) is playing which frequencies.

It reuses the meters and look of our Prism Meter plugin (vendored in
`third_party/prism`), built with JUCE 8 and C++20.

## Views

Each window picks a view from its toolbar. Open one window per display
(Settings > New display window).

| View | For | What it shows |
| --- | --- | --- |
| Quad (2 x 2) | 12" display, 1920 x 720 | Four Prism meters for any library source; each panel's menu picks its tool |
| Row of four | 12" display | Four meters side by side |
| Meter panel | 8" display, 1920 x 480 | Arched LED peak + average and loudness |
| Arched LED only | 8" display | The arched LED meter alone |
| Spectrum overlay | any | Up to four sources as coloured traces over the sum of them, with an inputs sidebar |

Spectrograms in the meters run vertically (newest at the top).

**Overlay background** (menu in the overlay's title strip):
- **3D** (default): the sum of the shown sources as a grey 3D surface, newest
  slice at the front. Loud ridges warm into heat colours. Slices stack straight
  up, so every frequency stays on its own Hz mark.
- **Split**: traces on a clean graph, the summed heat-map waterfall below.
- **Quiet blue**: the summed waterfall in one dim blue behind the traces.

**Averaging** (overlay title strip): off, 0.1, 0.3, 1 or 3 seconds, for how
quickly the traces and the background follow the music. History is drawn on a
fixed dB scale and never changes once drawn; it only scrolls back.

In the overlay sidebar, the eye hides a source, S solos it, and clicking a
name swaps another library source into that slot.

## Input device

One device at a time, for example Dante Virtual Soundcard. Pick it from the
INPUT menu on any window's toolbar, or in Settings > Audio device (with sample
rate and buffer size).

## Sources

Settings holds a **source library**: as many sources as you like, each with a
name, a colour, mono or stereo, and its input channels. Every library source
is metered all the time (up to 32), so the grid views (Quad, Row of four, Meter
panel, Arched LED) can pick any of them from the toolbar, and switching shows
its loudness history straight away. The overlay shows up to four at once, in
slots A to D.

- A stereo source takes one slot. Its trace averages the power of L and R per
  frequency, so out-of-phase content never cancels. Loudness stays true stereo.
- A mono source is metered as dual mono (as if panned centre).
- The overlay background sums the power of the shown sources.

## Full screen

Use the toolbar's Full screen button, Ctrl-Cmd-F, or the green window button;
each window goes full screen on its own display. While full screen, the
toolbar and cursor hide after three idle seconds and come back when the mouse
moves. Esc or Ctrl-Cmd-F leaves. Settings > "Full screen on launch" brings
every window back full screen when the app opens.

## Build (Mac)

```sh
cmake -S . -B build -G Xcode            # or Ninja / Unix Makefiles
cmake --build build --config Release --target MeterBridge
open "build/MeterBridge_artefacts/Release/Meter Bridge.app"
```

CMake downloads JUCE 8.0.9. To build offline, pass `-DMB_JUCE_DIR=/path/to/JUCE`.
The app asks for microphone (audio input) permission on first launch. It only
reads levels; nothing is recorded or sent anywhere.

### Snapshot tool

`-DMB_BUILD_SNAPSHOT_TOOL=ON` builds `MeterBridgeSnapshot`, which renders any
view to PNG with four synthetic keyboards and no audio device:

```sh
MeterBridgeSnapshot overlay overlay.png 8 grey3d 1920 720
```

## Layout

- `app/Source`: the app. `AudioEngine` (device, source library, slot routing,
  one Prism processor per slot), `MeterHub` (30 Hz UI clock), `DisplayWindow`
  (toolbar, full screen), `GridView` (Prism meter grids), `OverlayView`,
  `SettingsWindow`, `AppSettings` (saved state).
- `third_party/prism`: Prism Meter's DSP core and views (see its README).
- `tools/Snapshot.cpp`: the snapshot tool.
- `legacy/swift`: the earlier SwiftUI prototype, kept for reference.
