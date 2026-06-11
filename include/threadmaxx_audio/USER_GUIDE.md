# `threadmaxx_audio` — User Guide

The library hands you `AudioMixer`. Everything else flows through it.
This guide walks the pillars in the order you'll typically wire them
up.

## 1. Devices

Pick a device, wrap it in `std::unique_ptr`, construct the mixer.

```cpp
#include <threadmaxx_audio/threadmaxx_audio.hpp>

// Test backend — captures every submit into a vector you can inspect.
auto loopback = std::make_unique<LoopbackDevice>();

// Linux backends — only available when CMake found ALSA / Pulse at
// configure time. The `#if` guards keep the code portable.
#if THREADMAXX_AUDIO_HAS_ALSA
auto alsa = std::make_unique<AlsaDevice>();
#endif
#if THREADMAXX_AUDIO_HAS_PULSE
auto pulse = std::make_unique<PulseDevice>();
#endif
```

**Fallback chain pattern** — the library doesn't ship a "smart"
multi-backend wrapper because the policy is game-specific. Roll
your own:

```cpp
std::unique_ptr<IAudioDevice> pickDevice() {
#if THREADMAXX_AUDIO_HAS_ALSA
    auto a = std::make_unique<AlsaDevice>();
    if (a->initialize(format, frames)) return a;
#endif
#if THREADMAXX_AUDIO_HAS_PULSE
    auto p = std::make_unique<PulseDevice>();
    if (p->initialize(format, frames)) return p;
#endif
    return std::make_unique<LoopbackDevice>();
}
```

## 2. Mixer initialization

```cpp
AudioMixerConfig cfg{};
cfg.maxVoices = 256;     // voice pool size
cfg.maxBuses  = 16;      // master + 15 user buses
cfg.maxClips  = 64;
cfg.maxStreams = 8;
cfg.maxListeners = 2;    // e.g., split-screen

AudioMixer mixer(std::move(device), cfg);
mixer.initialize(AudioFormat{48000, 2, ChannelLayout::Stereo}, 1024);
```

After `initialize()` the mixer is in steady state. Every hot-path
call is zero-allocation; clip / bus / listener tables and the voice
pool are all preallocated.

## 3. Clips

Clips are resident PCM. `addClip` copies the source span into engine-
owned storage.

```cpp
std::vector<float> samples = loadInterleavedFloat(...);
SoundId clipId = mixer.addClip(samples,
    AudioFormat{48000, 2, ChannelLayout::Stereo});
```

`SoundId{0}` means the clip table is full.

## 4. Buses

A bus is a routing node. Voices route to it; it folds into master
with its gain/mute/solo applied. Master is bus slot 0 — always
alive, can carry gain/mute but never solo (it's the output node).

```cpp
BusId musicBus = mixer.createBus(BusDesc{ .gainDb = -3.0f });
BusId sfxBus   = mixer.createBus(BusDesc{});

mixer.setBusGain(musicBus, -6.0f);     // duck the music bus
mixer.setBusMute(sfxBus, true);        // silence the SFX bus
mixer.setBusSolo(musicBus, true);      // only solo'd buses heard
```

Routing a voice to `BusId{0}` (or no bus at all) sends it directly
to master.

## 5. Voice playback

`play()` returns a `VoiceId`; pass it back to `stop()` /
`isPlaying()` / `setEmitter()`.

```cpp
VoiceId v = mixer.play(VoiceDesc{
    .sound   = clipId,
    .bus     = sfxBus,
    .gainDb  = -3.0f,
    .looping = false,
});
if (v.value == 0) { /* pool exhausted AND steal failed */ }
```

**Voice stealing** — when the pool is full, the lowest-tick voice
is stolen, its `VoiceId` decodes as stale, and
`MixerStats::droppedVoices` increments. The new voice gets the slot.

```cpp
mixer.stop(v);
bool alive = mixer.isPlaying(v);
```

## 6. Streaming

Implement `IAudioStream::read(AudioSpan)` and register the
producer:

```cpp
StreamId sid = mixer.addStream(std::make_unique<MyOggStream>("track.ogg"));
VoiceId  vs  = mixer.play(VoiceDesc{ .stream = sid, .looping = true });
```

Stream `read()` runs on the mix thread (`mixer.mix()` calls into it).
For producer underrun (returning < requested AND not finished), the
mixer silences the tail and bumps `MixerStats::underruns`. EOF on a
looping voice triggers a transparent rewind inside the read.

Concurrent playback of the same stream is undefined — one voice per
stream is the recommended pattern.

## 7. 3D spatialization

Create a listener, then attach an emitter to a voice. The
spatializer mixes the voice's source as mono down-mix → equal-power
L/R, applying attenuation and Doppler pitch shift.

```cpp
ListenerId player = mixer.createListener(ListenerDesc{});

VoiceId v = mixer.play(VoiceDesc{ .sound = footstepsClip, .looping = true });
mixer.setEmitter(v, player, EmitterDesc{
    .position         = { x, y, z },
    .velocity         = { vx, vy, vz },
    .minDistance      = 1.0f,
    .maxDistance      = 30.0f,
    .dopplerFactor    = 1.0f,
    .attenuationModel = AttenuationModel::InverseSquare,
});

// Per tick — update listener + emitter from world state.
setListenerPose(mixer, player, cameraPos, cameraVel);
setEmitterPose(mixer, v, player, emitterPos, emitterVel,
               1.0f, 30.0f, 1.0f, AttenuationModel::InverseSquare);
```

**Coordinate convention**: right-handed, X-right / Y-up /
Z-forward. `right = up × forward`.

**Multi-listener**: each voice is attached to ONE listener.
Split-screen → create two listeners + two voices (one per
listener) per emitter source.

**Doppler**: `dopplerFactor = 0` disables pitch shift even with
non-zero velocities; useful when you want spatial pan without
musical pitch effects.

## 8. DSP helpers

Standalone span ops. Header-only; safe to call in custom DSP
chains.

```cpp
AudioSpan buf{ data, frames, format };

applyGain(buf, -6.0f);                      // -6 dB → half amplitude
applyPanStereo(buf, -1.0f);                 // full left
applyFadeIn(buf,  0.05f, 48000.0f);         // 50 ms fade-in
applyFadeOut(buf, 0.05f, 48000.0f);         // 50 ms fade-out
```

0 dB is a bit-exact no-op (early-out skips the multiply loop).
-∞ dB silences.

## 9. Per-tick mixing

`mix()` produces exactly `bufferFrames()` of output and submits to
the device. Call once per tick (or once per device buffer if
running async).

```cpp
for (auto& [voice, entity] : trackedEmitters) {
    Transform t = world.transform(entity);
    setEmitterPose(mixer, voice, listener, toAudioVec3(t.position), ...);
}
mixer.mix();
```

`mix()` is zero-alloc and lock-free on the application side. The
underlying device backend may block if its kernel buffer is full
(ALSA / Pulse synchronous writes).

## 10. Engine integration (recommended pattern)

The library is engine-agnostic — there's no `AudioSystem` shipped
in the library. Roll one in your game using `scene.hpp`'s helpers:

```cpp
class AudioSystem : public threadmaxx::ISystem {
public:
    AudioSystem(audio::AudioMixer& m, audio::ListenerId l)
        : mixer_(&m), listener_(l) {}

    void track(threadmaxx::EntityHandle e, audio::VoiceId v) {
        emitters_.emplace_back(e, v);
    }

    void postStep(threadmaxx::SystemContext& ctx) override {
        const auto* world = ctx.worldView().world();
        for (auto& [entity, voice] : emitters_) {
            if (auto* t = world->tryGetTransform(entity)) {
                audio::setEmitterPose(*mixer_, voice, listener_,
                    audio::Vec3{ t->position.x, t->position.y, t->position.z });
            }
        }
        mixer_->mix();
    }
private:
    audio::AudioMixer* mixer_;
    audio::ListenerId listener_;
    std::vector<std::pair<threadmaxx::EntityHandle, audio::VoiceId>> emitters_;
};
```

Read positions in `postStep` so writers earlier in registration
order (e.g., a physics system) have committed their Transform
updates by the time the audio system samples.

The full working example is at `examples/audio_demo/`.

## 11. Diagnostics

`AudioDiagnostics` is a non-owning view wrapper. Use it from a HUD
or telemetry sink.

```cpp
AudioDiagnostics diag(mixer);
MixerStats s = diag.stats();
// s.activeVoices, s.droppedVoices, s.underruns
// s.peakL, s.peakR (hold-max), s.rmsL, s.rmsR (per-call instantaneous)
diag.resetPeaks(); // clears peakL/peakR; doesn't touch counters
```

## 12. Playback events

Register a C-style callback to listen for voice lifecycle events.

```cpp
mixer.setPlaybackEventCallback(
    [](const PlaybackEvent& ev, void* user) {
        auto* logger = static_cast<MyLogger*>(user);
        switch (ev.type) {
            case PlaybackEventType::VoiceStarted: logger->logStart(ev.voice);  break;
            case PlaybackEventType::VoiceStopped: logger->logStop(ev.voice);   break;
            case PlaybackEventType::VoiceLooped:  logger->logLoop(ev.voice);   break;
        }
    },
    myLoggerPtr);
```

The callback fires synchronously inside the mixer call (play / stop
/ mix). **Do not call mutator APIs from inside the callback** —
the mixer is mid-transition. Record events and process them after
`mix()` returns.

## 13. Common gotchas

- **Forgetting to call `initialize()`** after construction → every
  call is a silent no-op. Check `mixer.initialized()`.
- **Holding a `VoiceId` past the voice's lifetime** — stopping or
  steal-replacement bumps the slot's generation; the old `VoiceId`
  decodes as stale. Always check `isPlaying(voice)` before
  recapturing the slot.
- **Calling `setEmitter` on a stale voice** — silently no-op.
- **Multiple voices reading the same stream** — undefined.
- **Reading `MixerStats::peakL/R` without resetPeaks** — peak is
  hold-max from startup; reset before each metering window.
