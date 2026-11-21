# Prism Meter (vendored)

Meter Bridge reuses the metering core and the meter views from our Prism Meter
plugin, so both products look and measure the same.

- Source: `jstg-235/prism-meter`, commit `b29bc90`.
- `core/` is copied from Prism (`prism_core`, pure C++20, no JUCE), with one small addition listed below.
- `plugin/Source/` is the plugin's processor and UI. Meter Bridge runs one
  `PrismMeterProcessor` per input source and shows its meters with the
  plugin's own `MeterModule` views. The editor is compiled but never opened.
- Local changes, to send back upstream to Prism later:
  - `ui/MeterViews.*`: the spectrogram defaults to a vertical waterfall
    (newest at the top, frequency across) and the Peak + Average dual-needle
    gauge is redrawn in a VU hardware style (backlit face, PEAK/AVERAGE key,
    peak-hold flag, OVER lamp, PK/AVG/CR readouts).
  - `core/SpectrumAnalyzer`: `resetPeaks()` clears only the peak curve, so the
    overlay can run its own peak hold and release time.

Keep edits here small and mark them, so they can be merged back into Prism.
