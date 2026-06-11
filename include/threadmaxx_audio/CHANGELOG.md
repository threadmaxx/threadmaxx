# threadmaxx_audio CHANGELOG

## v1.0.0 — 2026-06-11

Initial release. Bus-graph mixer + 3D spatializer + Linux device
backends. Shipped across eight batches (AU1–AU8); per-batch detail
in `FUTURE_WORK.md`.

### Highlights

- **Bus-graph mixer** — voice pool with steal-oldest overflow,
  master + N user buses with gain/mute/solo, clip + stream
  playback, zero-allocation hot path.
- **3D spatialization** — equal-power stereo pan, Linear /
  Inverse / InverseSquare attenuation, Doppler pitch shift via
  fractional source-read cursor, multi-listener support.
- **DSP helpers** — header-only `applyGain` /
  `applyPanStereo` / `applyFadeIn` / `applyFadeOut`.
- **Streaming** — `IAudioStream` producer interface with
  transparent loop rewind and producer-underrun silence-fill +
  counter.
- **Diagnostics + events** — peak / RMS meters, voice pool
  stats, C-style playback event callback.
- **Backends** — `LoopbackDevice` (test), `AlsaDevice` (Linux),
  `PulseDevice` (Linux). Each gated on `find_package` discovery.

### Performance

- `bench/audio_crowd_bench` at 256 voices / 4 buses / 1024-frame
  buffer @ 48 kHz: **0.139 ms / buffer** on the dev target
  (~14× under the < 2 ms gate).
- Zero-allocation contract pinned at 256 simultaneously-playing
  voices via `test_audio_voice_pool_256_no_alloc`.

### Test coverage

27 tests in `tests/audio/`, all green on `build/` and
`build-werror/` (`-Wsign-conversion -Wconversion -Wold-style-cast
-Werror`). Categories: foundations / mixer / streaming / 3D
spatial / DSP / diagnostics / events / backends / engine
integration / v1.0 gate.

### Public API

Every header under `include/threadmaxx_audio/` (except `detail/`)
is part of the v1.x ABI contract. SemVer bump rules + deprecation
policy are documented in `MAINTAINER_GUIDE.md`.
