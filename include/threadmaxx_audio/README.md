# `threadmaxx_audio`

Engine-agnostic audio mixer + 3D spatializer + Linux device backends
for the `threadmaxx` engine. **Status**: v1.0.0 — production-ready.

## What

A bus-graph mixer for sound effects, music, and 3D positional audio.
The library owns voices, buses, routing, DSP, and spatializer math;
it talks to hardware through an `IAudioDevice` (ALSA / Pulse / a
test-only `LoopbackDevice`). Game code drives it via per-tick calls
on `AudioMixer::play` / `setEmitter` / `mix`.

The library covers eight pillars:

- **Mixer** — `AudioMixer` is the top-level entry. Holds the clip
  registry, the voice pool with steal-oldest overflow, the bus
  graph, the stream table, the listener table, the device pointer.
  `mix()` produces exactly `bufferFrames` of output and submits.
- **Bus graph** — flat master + N user buses with `gainDb`, `muted`,
  `solo`. Voices route to any bus including master; non-master
  buses fold into master with their gain/mute/solo applied.
- **Voice allocator** — fixed-capacity slot table with generation-
  guarded `VoiceId` handles. On overflow, the lowest-tick voice is
  stolen and `MixerStats::droppedVoices` increments.
- **Clip + stream playback** — resident `Clip` (interleaved float
  PCM) and producer-driven `IAudioStream`. Streams handle producer
  underruns (silence-fill + counter bump) and looping voice EOF
  rewind transparently.
- **3D spatialization** — listener / emitter descriptors,
  Linear / Inverse / InverseSquare distance attenuation, equal-power
  stereo panning with behind-attenuation, Doppler pitch shift via
  fractional source-read cursor.
- **DSP helpers** — header-only `applyGain` / `applyPanStereo` /
  `applyFadeIn` / `applyFadeOut`. Zero-alloc; usable in custom
  chains.
- **Diagnostics + events** — `MixerStats` exposes activeVoices /
  droppedVoices / underruns / peak / RMS; `setPlaybackEventCallback`
  fires `VoiceStarted` / `VoiceStopped` / `VoiceLooped` on the
  expected boundaries.
- **Device backends** — `LoopbackDevice` (in-memory test sink),
  `AlsaDevice` (Linux), `PulseDevice` (Linux). CMake gates each
  platform backend on `find_package` discovery.

The library is engine-agnostic — it does NOT link against
`threadmaxx::threadmaxx` and never names an engine type. Game code
converts its own `Transform` to `audio::Vec3` at the call site.

## Quick start

```cmake
target_link_libraries(my_target PRIVATE threadmaxx::audio)
```

```cpp
#include <threadmaxx_audio/threadmaxx_audio.hpp>

using namespace threadmaxx::audio;

// 1. Pick a device. Falls back to LoopbackDevice if no platform
//    backend is available.
auto device = std::make_unique<LoopbackDevice>();
AudioMixer mixer(std::move(device));
mixer.initialize(AudioFormat{48000, 2, ChannelLayout::Stereo}, 1024);

// 2. Register a clip (interleaved float samples).
std::vector<float> samples = loadMyClip();
SoundId clip = mixer.addClip(samples, AudioFormat{48000, 2, ChannelLayout::Stereo});

// 3. Optional: create a listener + spatial voice.
ListenerId listener = mixer.createListener({});
VoiceId voice = mixer.play(VoiceDesc{ .sound = clip, .looping = true });
mixer.setEmitter(voice, listener, EmitterDesc{
    .position    = { 0.0f, 0.0f, 5.0f },
    .minDistance = 1.0f,
    .maxDistance = 30.0f });

// 4. Per game tick: update listener / emitter poses + mix() once.
setListenerPose(mixer, listener, cameraPos, cameraVel);
setEmitterPose(mixer, voice, listener, emitterPos, emitterVel,
               /*minD*/ 1.0f, /*maxD*/ 30.0f);
mixer.mix();
```

`USER_GUIDE.md` walks each pillar in detail.
`MAINTAINER_GUIDE.md` documents the versioning and ABI policy.

## Performance

`bench/audio_crowd_bench` is the throughput gate. On the v1.0 dev
target (Linux x86_64, Release build):

| Voices | Buses | Buffer | Sample rate | Avg mix cost |
|--------|-------|--------|-------------|--------------|
| 256    | 4     | 1024 frames | 48 kHz | **0.139 ms / buffer** (~14× under the 2 ms gate) |

The hot path (`AudioMixer::mix()`) is zero-allocation after
`initialize()`; this is contract, pinned by
`test_audio_voice_pool_256_no_alloc` (256 simultaneously-playing
voices, 100 mix calls, zero heap traffic under a tracking allocator).

## Tests

27 tests in `tests/audio/`, all green on `build/` and `build-werror/`
(`-Wsign-conversion -Wconversion -Wold-style-cast -Werror`).
Categories:

- **Foundations** (3) — types, buffers, loopback device
- **Mixer** (5) — one-shot, bus routing, solo, voice stealing,
  no-allocations
- **Streaming** (4) — basic, rewind, underrun, loop
- **3D spatial** (4) — attenuation, pan, doppler, multi-listener
- **DSP** (4) — gain, pan, fade, no-allocations
- **Diagnostics + events** (3) — meters, reset, playback events
- **Backends** (2) — ALSA, Pulse (CI-tolerant: no-device → PASS)
- **Engine integration + v1.0 gate** (2) — engine_integration,
  voice_pool_256_no_alloc

## Out of scope

- Codec decoders (Ogg / Vorbis / Opus / FLAC) — game-side
- HRTF / binaural rendering — v1.x
- Reverb / occlusion / filter chains — v1.x
- macOS CoreAudio / Windows WASAPI / JACK backends — add when the
  project targets those platforms
