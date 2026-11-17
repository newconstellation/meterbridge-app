# Meter Bridge

A standalone Mac app that meters audio inputs, built for Dante Virtual
Soundcard channels and Pro Tools aux outputs. The goal: load up to four stereo
sources (for example four keyboards) into one colour-coded overlay
spectrograph, so you can see which source is playing which frequencies.

## Status

| Step | What | State |
| --- | --- | --- |
| 1 | App skeleton, input device picker, Sources A-D, basic L/R meters | in progress |
| 2 | Overlay spectrograph (spectrum curves + "loudest source wins" spectrogram) | planned |
| 3 | Per-source peak / RMS / LUFS meters with clip indicators | planned |
| 4 | Testing with DVS and Pro Tools, signed release build | planned |

## Requirements

- macOS 14 Sonoma or later
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

## Routing

- **Dante Virtual Soundcard:** pick it as the input device. Each source takes
  a stereo pair of DVS channels (1-2, 3-4, ...).
- **Pro Tools aux outputs:** send the auxes to outputs of a device the app can
  read, for example DVS transmit channels or a loopback device, then pick
  those pairs in Meter Bridge.

## Layout

- `Sources/MeterBridgeCore` - UI-free logic: source setup, device choice,
  persistence, level math. Unit tested (`swift test`).
- `Sources/MeterBridge` - the SwiftUI app: Core Audio device discovery, a
  real-time-safe AUHAL capture, and the views.
