# `threadmaxx_audio` — Maintainer Guide

This document is the contract for keeping the library at v1.x
binary-compatible while it evolves. It lives next to the code so it
moves when the code moves.

## 1. Versioning

The current release is **v1.0.0**. Three artifacts MUST move
together when bumping:

1. `include/threadmaxx_audio/version.hpp` —
   `THREADMAXX_AUDIO_VERSION_MAJOR/MINOR/PATCH` macros AND the
   `version_string()` literal.
2. Tag the commit.
3. Update the status line at the top of `FUTURE_WORK.md`.

### Bump rules (SemVer)

- **MAJOR** — breaking public API change (signature of a `*.hpp`
  entry point, layout of a public POD, removal of a function /
  class / enum value, change to the wire format magic).
- **MINOR** — additive change (new mixer method, new DSP helper,
  new channel layout value, new backend, additive event type).
  Existing API stays source + binary compatible.
- **PATCH** — bug fix, perf, or doc improvement. No API change.

### Deprecation cycle

`[[deprecated]]` for one minor release, then removal in the next
major. Example:

```cpp
[[deprecated("use applyGain in 1.2")]]
void applyVolume(AudioSpan, float linear);
```

ABI breaks bundled in the same major release are documented in a
migration page.

## 2. Public surface

Everything reachable through `#include
<threadmaxx_audio/threadmaxx_audio.hpp>` is the contract. The
umbrella header lists every public file currently in play:

```
buffer.hpp         clip.hpp          config.hpp
device.hpp         diagnostics.hpp   dsp.hpp
events.hpp         loopback_device.hpp
mixer.hpp          scene.hpp         spatial.hpp
stream.hpp         types.hpp         version.hpp
voice.hpp
+ alsa_device.hpp  (gated by THREADMAXX_AUDIO_HAS_ALSA)
+ pulse_device.hpp (gated by THREADMAXX_AUDIO_HAS_PULSE)
```

Anything under `include/threadmaxx_audio/detail/` (currently
`pan_law.hpp`, `voice_allocator.hpp`) is implementation detail and
NOT part of the API contract — those can change between minor
releases as long as the public surface stays stable.

Everything in `src/threadmaxx_audio/` is private; the mixer is
PImpl'd through `AudioMixer::Impl`.

## 3. Adding a public symbol

1. Doxygen `@brief` on the symbol. Load-bearing methods also get
   `@thread_safety` / `@pre`.
2. If it changes `MixerStats`, add new fields at the end (never
   reorder) — `MixerStats` is a value type passed by copy across
   ABI boundaries.
3. If it adds a new `IAudioDevice` virtual, the addition is a
   MAJOR break — bump or stage behind a parallel interface.
4. If it adds a new event type, append to the `PlaybackEventType`
   enum — never reuse old values.

## 4. Adding a new backend

Pattern (mirrors `AlsaDevice`):

1. `include/threadmaxx_audio/<name>_device.hpp` — public header,
   PImpl, no platform headers leaked.
2. `src/threadmaxx_audio/backends/<Name>Device.cpp` — impl, may
   include platform headers freely.
3. `src/threadmaxx_audio/CMakeLists.txt` — `find_package` (or
   `pkg_check_modules`) probe, append to
   `THREADMAXX_AUDIO_SOURCES` + `THREADMAXX_AUDIO_PUBLIC_HEADERS`,
   set the `THREADMAXX_AUDIO_HAS_<NAME>` cache var + the matching
   `target_compile_definitions`.
4. `tests/audio/test_audio_backend_<name>.cpp` — tolerant smoke
   test: `init()` failure (no device) is a PASS so CI on headless
   runners stays green.
5. Add the test to the foreach-loop in
   `tests/audio/CMakeLists.txt`.

## 5. Adding a new DSP helper

Header-only in `include/threadmaxx_audio/dsp.hpp`. Constraints:

- **No allocations** — pinned by `test_audio_dsp_no_allocations`.
  Tracking allocator must report zero.
- **Identity input handled** — 0 dB / 0 pan / 0-second fade should
  be bit-exact no-ops (early-out before the loop).
- **Edge cases** — empty buffer / null pointer / zero channels →
  silent no-op (not abort).
- **Test under `tests/audio/test_audio_dsp_*.cpp`** with both
  happy-path and edge-case coverage.

## 6. Hot-path discipline

`AudioMixer::mix()` is the steady-state hot path. Two contracts:

1. **Zero allocations after `initialize()`** — pinned by
   `test_audio_mixer_no_allocations` and
   `test_audio_voice_pool_256_no_alloc`. Vectors stay at their
   reserved capacity; no `std::function` captures; no
   `emplace_back` that could realloc.
2. **Performance gate** — `bench/audio_crowd_bench` at 256
   voices, 4 buses, 1024 frames @ 48 kHz must stay under
   2 ms/buffer in Release builds. The current baseline is
   0.139 ms/buffer; regressions to ≥ 1 ms should be investigated.

The AU7 perf rewrite is the canonical example: a naive
per-frame-callback inner loop ran at 4.5 ms; segmented mixing +
stereo→stereo specialization in `mixFramesIntoBus` brought it to
0.14 ms. **Don't reintroduce per-frame function calls inside the
mix loop** — vectorizers can't see through them.

## 7. Internal layout cheat sheet

```
include/threadmaxx_audio/
  buffer.hpp          AudioFormat, AudioSpan, ConstAudioSpan, framesToBytes, samplesIn
  types.hpp           SoundId, VoiceId, BusId, StreamId, ListenerId, ChannelLayout
  config.hpp          kDefault* compile-time defaults
  clip.hpp            Clip POD (interleaved float storage)
  voice.hpp           BusDesc, VoiceDesc, VoiceState
  spatial.hpp         Vec3, AttenuationModel, ListenerDesc, EmitterDesc, kSpeedOfSound
  events.hpp          PlaybackEventType, PlaybackEvent, PlaybackEventCallback
  dsp.hpp             applyGain / applyPanStereo / applyFadeIn / applyFadeOut
  device.hpp          IAudioDevice interface
  stream.hpp          IAudioStream + NoiseStream + StarvedStream
  loopback_device.hpp In-memory test device
  alsa_device.hpp     Linux ALSA backend (gated)
  pulse_device.hpp    Linux Pulse backend (gated)
  mixer.hpp           AudioMixer, AudioMixerConfig, MixerStats
  scene.hpp           setListenerPose / setEmitterPose convenience wrappers
  diagnostics.hpp     AudioDiagnostics view wrapper
  version.hpp         macro + version_string()
  detail/
    pan_law.hpp       computeSpatial, computeAttenuation, Vec3 math helpers
    voice_allocator.hpp VoiceAllocator + VoiceSlot

src/threadmaxx_audio/
  AudioMixer.cpp      mixer pimpl, mix() loop, command dispatch
  VoiceAllocator.cpp  steal-oldest pool
  backends/
    LoopbackDevice.cpp
    AlsaDevice.cpp    (gated)
    PulseDevice.cpp   (gated)
```

## 8. Test taxonomy

27 tests in `tests/audio/`. When adding a feature, drop the test
in the matching bucket — the CI grep prefix `^audio\.` keeps the
suite namespaced.

| Bucket | Tests | What they pin |
|--------|-------|---------------|
| Foundations | 3 | types, buffer math, loopback round-trip |
| Mixer core | 5 | one-shot, bus routing, solo, voice stealing, no-alloc |
| Streaming | 4 | basic, rewind, underrun, loop wrap |
| 3D spatial | 4 | attenuation, pan, doppler, multi-listener |
| DSP | 4 | gain, pan, fade, no-alloc |
| Diagnostics + events | 3 | meters, peak reset, lifecycle events |
| Backends | 2 | ALSA, Pulse (CI-tolerant) |
| Integration + v1.0 gate | 2 | engine_integration, voice_pool_256_no_alloc |

## 9. Sanitizer + warning hygiene

The build must stay clean under:

- `-Wall -Wextra -Wpedantic -Wshadow -Wsign-conversion
  -Wconversion -Wold-style-cast -Werror` (the
  `THREADMAXX_WARNINGS_AS_ERRORS=ON` build tree).
- ASAN / UBSAN — engine-side sanitizer trees include the audio
  suite. TSAN isn't run on the audio library since the public
  surface is single-threaded by contract (the mixer is the only
  caller; device backends are sync).

## 10. Out-of-scope for v1.x

Per DESIGN_NOTES §1, none of this lands in the audio library at
any version:

- Codec decoders (Ogg / Vorbis / Opus / FLAC) — game-side
- ECS storage model — engine-side
- Asset format ownership — game-side
- Networking / replication / save format — game-side
- Hidden engine-side audio state — explicitly forbidden

Things slated for v1.x but not v1.0:

- Filter chain (lowpass / highpass / EQ)
- Reverb send bus
- HRTF / binaural rendering
- macOS / Windows backends
- Capture-to-WAV diagnostic mode
- SIMD-accelerated mixer (current scalar path already vectorises)
