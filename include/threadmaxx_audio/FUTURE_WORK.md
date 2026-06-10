# `threadmaxx_audio` — batch plan

Sibling-library implementation plan. `DESIGN_NOTES.md` is the
authoritative spec; this doc breaks it down into shippable
test-driven batches.

Status: **AU1-AU2 shipped (2026-06-10)**. AU3-AU8 are 📋 planned.
Sequencing follows the §8 "implementation order" of the design notes,
regrouped into shippable units that each carry their own tests.

## Conventions

Each batch is independently shippable:

- **Goal** — what the batch accomplishes in one sentence.
- **Test gate** — assertions that prove the batch landed.
- **Files** — what's added / modified.
- **Risks** — what could break.
- **Out of scope** — explicitly deferred to a later batch.

Batches start red, go green, then refactor. The library produces a
static library `threadmaxx::audio` plus public headers under
`include/threadmaxx_audio/`. Platform backends live under
`src/threadmaxx_audio/backends/`.

The hot path (mixer callback) must never allocate. Tests prove this
by running the mix loop under a custom allocator that asserts on
allocation.

## Library structure (target end-state)

```
include/threadmaxx_audio/
  threadmaxx_audio.hpp     # umbrella
  config.hpp               # device + buffer settings
  types.hpp                # SoundId / VoiceId / BusId / ListenerId / StreamId
  device.hpp               # IAudioDevice
  buffer.hpp               # AudioSpan / ConstAudioSpan / AudioFormat
  clip.hpp                 # resident clip storage
  stream.hpp               # IAudioStream + helpers
  mixer.hpp                # AudioMixer top-level entry
  voice.hpp                # voice descriptors + state
  spatial.hpp              # listener/emitter / attenuation / panning
  dsp.hpp                  # gain / pan / filter helpers
  scene.hpp                # integration helpers
  events.hpp               # play/stop event types
  diagnostics.hpp          # MixerStats / meters
  serialization.hpp        # routing save/load
  detail/
    ring_buffer.hpp
    resampler.hpp
    pan_law.hpp
    voice_allocator.hpp
src/threadmaxx_audio/
  AudioMixer.cpp
  VoiceAllocator.cpp
  Spatializer.cpp
  backends/
    LoopbackDevice.cpp     # for tests (deterministic)
    AlsaDevice.cpp         # Linux primary (AU8)
    PulseDevice.cpp        # Linux fallback (AU8)
tests/audio/
  test_audio_*.cpp
bench/
  audio_*.cpp
```

## Batch AU1 — Foundations (types + buffers + loopback device) ✅ landed 2026-06-10

**Goal**: header-only types, audio buffer primitives, and a
`LoopbackDevice` test backend that captures submitted mix buffers
for assertion. No mixer yet — just the data model + the device
contract being exercisable.

**Test gate** (all green on `build/` + `build-werror/`):

- ✅ `test_audio_buffer` — `AudioSpan` / `ConstAudioSpan` round-trip
  through frame-iteration; `framesToBytes(format, n)` is exact across
  mono / stereo / 5.1; `samplesIn` matches `frames × channels`.
- ✅ `test_audio_loopback_device` — initialize → submit 1024 frames of
  stereo silence → shutdown; captured buffer length matches input;
  format round-trips; post-shutdown submit is a silent drop;
  re-initialize succeeds with a different format; zero-channel and
  zero-frame initialize rejected.
- ✅ `test_audio_format` — channel layout to channel count mapping
  (Mono=1, Stereo=2, Quad=4, FiveOne=6, SevenOne=8, Ambisonic=4);
  `AudioFormat` and handle equality.

**Files landed**:
- `include/threadmaxx_audio/types.hpp` — handles + `ChannelLayout`
- `include/threadmaxx_audio/buffer.hpp` — `AudioFormat`,
  `AudioSpan`, `ConstAudioSpan`, `framesToBytes`, `samplesIn`
- `include/threadmaxx_audio/config.hpp` — `kDefaultSampleRate` /
  `kDefaultBufferFrames` / `kDefaultMaxVoices` / `kMaxSendsPerVoice`
- `include/threadmaxx_audio/device.hpp` — `IAudioDevice` interface
- `include/threadmaxx_audio/loopback_device.hpp` — `LoopbackDevice`
- `include/threadmaxx_audio/threadmaxx_audio.hpp` — umbrella
- `src/threadmaxx_audio/backends/LoopbackDevice.cpp` — impl
- `src/threadmaxx_audio/CMakeLists.txt` — static lib `threadmaxx::audio`
- `tests/audio/CMakeLists.txt` + three `test_audio_*.cpp`

**Resolved decisions**:
- Interleaved storage for `AudioSpan` (matches every real backend's
  submit format); planar conversions happen inside the DSP layer
  when needed.
- Ambisonic = first-order (4 channels W,X,Y,Z); higher-order is v1.x.
- LoopbackDevice silently drops post-shutdown submits and asserts on
  format/frame-count mismatches against initialize parameters.

**Out of scope**: actual mixing (AU2), real device backends (AU8).

## Batch AU2 — Bus graph + voice playback + mixing ✅ landed 2026-06-10

**Goal**: a usable mono/stereo mixer. Create buses, route voices
to buses, mix per-frame into the device. Voice allocator with
stealing policy.

**Test gate** (all green on `build/` + `build-werror/`):

- ✅ `test_audio_mixer_one_shot` — register a 1-second sine clip,
  `play()` returns a `VoiceId`, mix N frames; LoopbackDevice
  captures audible output above silence; non-looping voice
  auto-frees its slot at clip end.
- ✅ `test_audio_mixer_bus_routing` — voice routed to a muted bus
  produces silence; un-mute resumes audible output; -20dB bus
  gain attenuates by ~10×.
- ✅ `test_audio_mixer_solo` — soloing bus A silences bus B;
  soloing both restores full mix; un-soloing both returns to
  normal mix.
- ✅ `test_audio_mixer_voice_stealing` — exhausting the voice pool
  with `MaxVoices=8` plus a 9th play() steals the oldest;
  `MixerStats::droppedVoices` increments; stolen `VoiceId`
  decodes as stale; stopping a voice frees its slot without
  triggering steal.
- ✅ `test_audio_mixer_no_allocations` — under a tracking allocator
  (global `operator new` override), 100 mix-cycles after warmup
  produce zero allocations.

**Files landed**:
- `include/threadmaxx_audio/voice.hpp` — `BusDesc`, `VoiceDesc`,
  `VoiceState`
- `include/threadmaxx_audio/clip.hpp` — `Clip` POD (interleaved
  PCM)
- `include/threadmaxx_audio/mixer.hpp` — `AudioMixer`,
  `AudioMixerConfig`, `MixerStats`
- `include/threadmaxx_audio/detail/voice_allocator.hpp` —
  `VoiceAllocator` + `VoiceSlot`
- `src/threadmaxx_audio/AudioMixer.cpp`
- `src/threadmaxx_audio/VoiceAllocator.cpp`
- `LoopbackDevice` got `setCaptureEnabled()` /
  `droppedSubmits()` so the no-alloc test can suppress
  capture-time `emplace_back` after warmup
- five `tests/audio/test_audio_mixer_*.cpp`

**Resolved decisions**:
- Master bus is bus slot 0 — always alive, never destroyable,
  can carry gain/mute but not solo (it's the output node).
- Voice → bus routing falls back to master when the requested
  bus is `BusId{0}` or has been destroyed.
- Voice stealing picks the lowest-`startTick` slot (oldest);
  generation bump invalidates both the stolen `VoiceId` and any
  prior reference to the slot.
- Mono clip → stereo bus duplicates to L+R; matching counts copy
  through; mismatched counts down-mix to mono then fan out.
- `VoiceId` / `BusId` / `SoundId` encoding: low 32 bits = slot
  index, high 32 bits = generation. `…Id{0}` reserved as invalid.

**Out of scope**: streaming (AU3), 3D (AU4).

## Batch AU3 — Streaming audio

**Goal**: `IAudioStream` interface and the ring-buffer pump that
feeds streamed voices. Test with a synthetic noise stream that
yields known bytes.

**Test gate**:

- `test_audio_stream_basic` — register a noise stream, play it,
  mix 10 seconds at 48kHz; sample counts match.
- `test_audio_stream_rewind` — `rewind()` resets read cursor;
  `finished()` is false after rewind.
- `test_audio_stream_underrun` — synthetic stream that returns
  fewer frames than requested triggers `MixerStats::underruns++`
  and the mixer emits silence for the missing frames.
- `test_audio_stream_loop` — looping stream wraps cleanly at
  end-of-stream without dropping samples.

**Files**: `stream.hpp`, `detail/ring_buffer.hpp`, extension to
`src/AudioMixer.cpp`.

**Risks**: producer/consumer threading. Spec the stream as
read-on-mixer-thread for v1.0; threaded prefetch is v1.x.

**Out of scope**: codec decoders (Ogg/FLAC/Opus). Game-side
responsibility — this library exposes the stream interface and
ships a synthetic noise stream for tests, not real decoders.

## Batch AU4 — 3D spatialization

**Goal**: listener / emitter state, distance attenuation, stereo
panning by listener-relative angle. Doppler optional.

**Test gate**:

- `test_audio_spatial_attenuation` — emitter at `maxDistance`
  produces silence; at `minDistance` produces full gain; midpoint
  follows the configured curve.
- `test_audio_spatial_pan` — emitter directly to listener's left
  produces left-heavy mix; directly behind produces equal-power
  pan with a documented "behind" attenuation.
- `test_audio_spatial_doppler` — relative velocity along the
  listener-emitter axis shifts pitch by the expected factor (test
  with a known sine clip; FFT peak detection verifies).
- `test_audio_spatial_multiple_listeners` — two listeners get
  independent attenuation/pan (use case: split-screen).

**Files**: `spatial.hpp`, `detail/pan_law.hpp`,
`src/Spatializer.cpp`, extension to mixer.

**Risks**: Doppler done wrong sounds terrible; gate it behind a
config flag (`dopplerFactor=0` disables) so games can opt out.

**Out of scope**: HRTF / binaural rendering (v1.x), reverb
(v1.x), occlusion (v1.x).

## Batch AU5 — DSP helpers

**Goal**: gain / pan / fade-in / fade-out as standalone span ops
(per DESIGN_NOTES §5.4). Used both internally by the mixer and
exposed publicly for custom DSP chains.

**Test gate**:

- `test_audio_dsp_gain` — `applyGain(buf, 0dB)` is a no-op;
  `applyGain(buf, -inf dB)` produces silence.
- `test_audio_dsp_pan` — applyPanStereo at -1/0/+1 produces
  left-only / center / right-only.
- `test_audio_dsp_fade_in` — fadeIn over N seconds produces a
  monotonically-increasing gain envelope; sample[0] is silent;
  sample[N*rate-1] is full gain.
- `test_audio_dsp_no_allocations` — every helper runs zero-alloc
  under the tracking allocator.

**Files**: `dsp.hpp`. No source file needed — these are header-only.

**Out of scope**: filters (lowpass/highpass/EQ) — v1.x.

## Batch AU6 — Diagnostics + events

**Goal**: `MixerStats` with peak/RMS meters, underrun counter,
voice-pool stats. `events.hpp` for playback callbacks.

**Test gate**:

- `test_audio_diagnostics_meters` — play a known sine clip; peak
  meter reports ~1.0 ± 1e-3 within the clip's duration.
- `test_audio_diagnostics_reset` — `resetPeaks()` clears peakL/peakR
  but preserves underrun count.
- `test_audio_events_playback` — start / stop / loop events fire
  on the expected boundaries; callbacks receive the right VoiceId.

**Files**: `diagnostics.hpp`, `events.hpp`.

**Out of scope**: spectrum analysis, capture-to-file.

## Batch AU7 — Engine integration

**Goal**: a recommended `ISystem` pattern that reads `Transform` +
`Velocity` chunks and updates listener / emitter state. Lives in
documentation and a smoke test, not in the library itself (the
library stays engine-agnostic per DESIGN_NOTES §2.1).

**Test gate**:

- `test_audio_engine_integration` — register an Engine + an
  AudioMixer + a sample `AudioSystem` (in the test harness); spawn
  a moving emitter entity; verify the mixer's spatializer received
  the per-tick updated position.
- `bench/audio_crowd_bench.cpp` — 512 simultaneously-playing
  emitters across 4 buses; report mix-cost / frame.

**Files**: `scene.hpp` (helper functions for engine integration),
example integration code in `examples/audio_demo/` (separate
target, not part of the library).

**Out of scope**: the full RPG demo audio integration (lives in
GAME_EXTENSION.md's mid-term tranche, depends on this library).

## Batch AU8 — Platform backends (Linux first)

**Goal**: ship `AlsaDevice` and `PulseDevice` so the library is
useful end-to-end on the dev target. CMake `find_package` gates
each one; missing libs silently skip the backend.

**Test gate**:

- `test_audio_backend_alsa` — gated by `find_package(ALSA)`;
  initialize → submit 1024 frames → shutdown without error. No
  audible-output assertion (CI is silent), just no-crash + no-error
  return.
- `test_audio_backend_pulse` — same shape, gated by
  `find_package(PulseAudio)`.

**Files**: `src/backends/AlsaDevice.cpp`, `src/backends/PulseDevice.cpp`.

**Risks**: ALSA/PulseAudio on headless CI may fail device open;
fallback chain (Alsa → Pulse → Loopback) lets games not care.

**Out of scope**: macOS CoreAudio, Windows WASAPI, JACK. Add when
the project targets those platforms.

## v1.0 close-out criteria

- ✓ Every batch AU1–AU8 landed and tested.
- ✓ Voice pool tested up to 256 simultaneously-playing voices
  without allocation.
- ✓ ALSA backend boots on the dev target.
- ✓ Bench `audio_crowd_bench.cpp` reports <2ms mix cost per 1024
  frames at 48kHz for 256 voices.
- ✓ Docs: README, USER_GUIDE, MAINTAINER_GUIDE.
- ✓ ctest 100% on `build/` and `build-werror/`.
- ✓ Version stamped at 1.0.0 in
  `include/threadmaxx_audio/version.hpp`.

## v1.x candidate batches (not blocking v1.0)

### v1.x — Filter chain (lowpass / highpass / EQ)

Per-voice and per-bus filter slots. Biquad implementation;
coefficient computation already well-documented (RBJ cookbook).
Bench gate: 256-voice mix with 1-band EQ per voice must stay
under 4ms per 1024-frame buffer.

### v1.x — Reverb send bus

Schroeder or freeverb-style algorithmic reverb on a dedicated send
bus. Optional per-voice send level.

### v1.x — HRTF / binaural rendering

For VR / headphone targets. Per-listener convolution against
shipped HRTF impulse-response sets. Significant CPU cost; gate it
hard behind a config flag.

### v1.x — Codec adapters

Optional `IAudioStream` adapters for Ogg Vorbis (via libvorbis) and
Opus (via libopus). Ship as separate optional CMake targets.

### v1.x — Backend: CoreAudio / WASAPI / JACK

Add when the project targets those platforms. Templates from the
existing ALSA/Pulse backends apply.

### v1.x — Capture-to-WAV diagnostic mode

A `CaptureDevice` decorator that writes the submitted mix buffer
to a WAV file. Invaluable for debugging mix issues offline.

### v1.x — SIMD-accelerated mixer

Per-voice gain + mix-into-bus is the obvious vector target.
8-channel `__m256` mix at FMA rate. Bench gate: ≥2× over scalar at
256-voice load.

## Out of scope for the whole library

Per DESIGN_NOTES §1 — none of this lands at any batch:

- Physics simulation / collision authority
- Animation graph ownership
- Navmesh logic
- Rendering backend
- Networking stack
- Editor UI
- ECS storage model
- Asset format ownership (game owns its codecs)
- Hidden engine-side audio state
