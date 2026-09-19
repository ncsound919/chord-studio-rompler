# Chord Studio Rompler

A rompler / granular sampler instrument (VST3 + Standalone), built with JUCE 8.
Load a folder of one-shots and play them chromatically, with click-free loop and
crossfade sustain, a granular grain-cloud engine, time-preserving pitch via
signalsmith-stretch, per-voice filter/envelopes, two LFOs, and a small mod
matrix.

## Features

- **Source**: load a folder; browse with search; per-note generative variation
  (off / random / cycle).
- **Playback**: chromatic repitch with band-limited mipmapped anti-aliasing, or
  time-preserving pitch (signalsmith-stretch) for loop/crossfade.
- **Sustain modes**: one-shot, loop, pre-roll crossfade loop (equal-gain or
  equal-power), and a windowed granular cloud (Hann/Tukey, size/density/spray/
  spread/drift).
- **Amp + filter ADSR envelopes**, per-voice state-variable filter (LP/BP/HP)
  with resonance, drive and filter-envelope/key-track.
- **Two per-voice LFOs** (sine/triangle/saw/square) and a 4-slot mod matrix
  (sources: LFO1/2, filter env, amp env, velocity, mod wheel, key track;
  destinations: cutoff, resonance, pitch, level, grain size, grain density).
- **Expression**: velocity (squared curve), pitch bend, CC11 expression.
- Real-time safe sample publication; click-free voice stealing; master DC
  blocker + limiter.

## UI

The editor is a single-file web UI (`webui/index.html`) hosted in a JUCE
WebBrowserComponent (WebView2 on Windows). On a system without the WebView2
runtime the editor shows an install prompt instead of a blank panel.

## Building

Requirements: CMake >= 3.22, a C++17 toolchain, and JUCE 8.

```sh
# Point at a JUCE checkout...
cmake -B build -DJUCE_DIR=/path/to/JUCE
# ...or let CMake fetch JUCE automatically:
cmake -B build

cmake --build build --config Release
```

Artifacts:

- `build/Rompler_artefacts/Release/VST3/Chord Studio Rompler.vst3`
- `build/Rompler_artefacts/Release/Standalone/Chord Studio Rompler(.exe)`

Windows builds statically link the WebView2 loader (`NEEDS_WEBVIEW2`); the
WebView2 runtime must be installed on the target machine.

### Tests

```sh
cmake --build build --config Release --target RomplerTests
ctest --test-dir build -C Release --output-on-failure
```

The headless test binary covers mipmap band-limiting, chromatic and granular
pitch, crossfade seam continuity, one-shot/granular termination, velocity,
the filter, LFO shapes, and mod-matrix routing.

## Third-party

- **signalsmith-stretch** and **signalsmith-linear** by Signalsmith Audio — MIT.
  See `third_party/*/LICENSE.txt`.
- **JUCE** — AGPLv3 / commercial (not vendored; fetched or supplied at build time).

## License

GPL-3.0-or-later. See `LICENSE`.
