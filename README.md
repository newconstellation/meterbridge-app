# Meter Bridge

A standalone Mac app that meters audio inputs, built for Dante Virtual
Soundcard channels and Pro Tools aux outputs. The goal: load up to four stereo
sources (for example four keyboards) into one colour-coded overlay
spectrograph, so you can see which source is playing which frequencies.

## Status

| Step | What | State |
| --- | --- | --- |
| 1 | Input device picker, Sources A-D routing, basic L/R meters | done |
| 2 | Overlay spectrum (curves), 1/3-octave bands, "loudest source wins" spectrogram, solo/hide | done |
| 3 | Loudness panel: momentary, short-term, integrated LUFS, loudness range, true peak, session clock | done |
| 4 | Testing with DVS and Pro Tools, signed release build | planned |

## Main window

- **Left screen:** pick *Overlay* (each source's spectrum as a coloured curve
  with peak-hold), *Bands* (1/3-octave bars, sources side by side) or
  *Spectrogram* (scrolling history; each pixel takes the colour of the loudest
  source at that frequency, and mixes towards white where two clash).
- **Right screen:** session clock, signal status, and EBU R128 loudness for the
  chosen source (momentary bar, true-peak bar, integrated / short-term /
  range). Reset clears the integrated reading and the clock.
- **Bottom strip:** each enabled source with live L/R meters; the eye hides it
  from the spectrum, S solos it.
- **Inputs** (top bar) shows or hides the routing sidebar.

## Requirements

- macOS 15 Sequoia or later
- Xcode 15+ (or the Swift 5.9+ toolchain)

## Run it

```sh
swift run MeterBridge            # quick dev run
./scripts/build-app.sh           # builds "build/Meter Bridge.app"
open "build/Meter Bridge.app"
```

The app asks for audio input permission the first time. It only reads levels;
nothing is recorded or sent anywhere. The `.app` is signed ad hoc for now, so
the first launch needs right-click > Open.

You can also download a built app from the **Build** workflow run on GitHub
(artifact `MeterBridge-app`).

## App icon

`scripts/make-icon.py` (Pillow) draws `Resources/AppIcon.iconset`; the build
script turns it into `AppIcon.icns`.

## Routing

- **Dante Virtual Soundcard:** pick it as the input device. Each source takes
  a stereo pair of DVS channels (1-2, 3-4, ...).
- **Pro Tools aux outputs:** send the auxes to outputs of a device the app can
  read, for example DVS transmit channels or a loopback device, then pick
  those pairs in Meter Bridge.

## Layout

- `Sources/MeterBridgeCore` - UI-free logic: source setup, device choice,
  persistence, level math, lock-free sample rings, FFT spectrum, frequency
  maps, EBU R128 loudness and true peak. Unit tested (`swift test`).
- `Sources/MeterBridge` - the SwiftUI app: Core Audio device discovery, a
  real-time-safe AUHAL capture, and the views.
