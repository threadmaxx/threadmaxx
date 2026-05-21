# `threadmaxx_audio` — audio mixing and 3D audio sibling library

## 1. Purpose

`threadmaxx_audio` provides runtime audio services for games built on `threadmaxx`.

It is for:

* sound effect playback,
* music playback,
* bus-based mixing,
* 3D positional audio,
* spatial attenuation and panning,
* voice management,
* streaming audio assets,
* event-driven playback,
* capture/playback debugging,
* audio-side diagnostics and profiling.

It is **not** for:

* rendering,
* physics simulation,
* animation math,
* ECS ownership,
* networking/replication,
* navmesh/pathfinding,
* editor UI,
* replacing the engine’s job system,
* owning the game’s asset pipeline.

That matches the roadmap’s boundary: audio is orthogonal to the backend, so it belongs above the engine rather than inside it. 

## 2. Design principles

1. **Above the engine.** The core engine should not know audio device details.
2. **Backend-agnostic.** The runtime can sit over WASAPI, CoreAudio, ALSA/Pulse, SDL audio, or a custom backend.
3. **Mixing over devices.** The library owns voices, buses, routing, and DSP chains; the backend owns the hardware output.
4. **Span-first APIs.** Buffer processing should use contiguous blocks.
5. **No allocations in the hot path.** Audio callback code must stay predictable.
6. **3D audio is data-driven.** Listener and emitter state come from game state.
7. **Streaming is optional.** Small clips can be fully resident; large assets can stream.
8. **Small, explicit public surface.** Keep the core easy to reason about.
9. **Deterministic where possible.** Given the same input stream and timebase, the mix graph should be reproducible.
10. **Game code owns policy.** Ducking rules, bus structure, and voice priorities stay configurable.

## 3. Package layout

```text id="g3r2qp"
include/threadmaxx_audio/
  threadmaxx_audio.hpp   // umbrella include
  config.hpp             // device settings, sample rate, buffer size
  types.hpp              // SoundId, VoiceId, BusId, ListenerId, StreamId
  device.hpp             // backend device interface
  buffer.hpp             // audio buffers, channel layouts, sample views
  clip.hpp               // resident sample clips
  stream.hpp             // streamed audio sources
  mixer.hpp              // mixing graph, buses, sends
  voice.hpp              // playback voices and instances
  spatial.hpp            // 3D positioning and attenuation
  dsp.hpp                // gain, pan, filters, envelopes
  scene.hpp              // listener/emitter integration helpers
  events.hpp             // playback events, callbacks
  diagnostics.hpp        // meters, clipping, underrun stats
  serialization.hpp     // save/load audio routing and settings
  detail/
    ring_buffer.hpp
    resampler.hpp
    pan_law.hpp
    voice_allocator.hpp
```

If you want platform-specific backends separated, keep them in `src/threadmaxx_audio/backends/`.

## 4. Core data model

### 4.1 Basic handles

```cpp id="j7n1xk"
namespace threadmaxx::audio {

struct SoundId   { std::uint64_t value{}; };
struct VoiceId   { std::uint64_t value{}; };
struct BusId     { std::uint64_t value{}; };
struct StreamId  { std::uint64_t value{}; };
struct ListenerId{ std::uint64_t value{}; };

enum class ChannelLayout : std::uint8_t {
    Mono,
    Stereo,
    Quad,
    FiveOne,
    SevenOne,
    Ambisonic
};

} // namespace threadmaxx::audio
```

### 4.2 Audio buffers

```cpp id="r5q3mw"
namespace threadmaxx::audio {

struct AudioFormat {
    std::uint32_t sampleRate = 48000;
    std::uint8_t channels = 2;
    ChannelLayout layout = ChannelLayout::Stereo;
};

struct AudioSpan {
    float* interleaved = nullptr;
    std::size_t frames = 0;
    AudioFormat format{};
};

struct ConstAudioSpan {
    const float* interleaved = nullptr;
    std::size_t frames = 0;
    AudioFormat format{};
};

} // namespace threadmaxx::audio
```

### 4.3 Buses and routing

```cpp id="p9d7fc"
namespace threadmaxx::audio {

struct BusDesc {
    std::string name;
    float gainDb = 0.0f;
    float pan = 0.0f;
    bool muted = false;
    bool solo = false;
};

struct SendDesc {
    BusId destination{};
    float gainDb = 0.0f;
    bool preFader = false;
};

} // namespace threadmaxx::audio
```

### 4.4 Voices

```cpp id="v2s8ld"
namespace threadmaxx::audio {

enum class VoiceState : std::uint8_t {
    Playing,
    Paused,
    FadingIn,
    FadingOut,
    Stopped
};

struct VoiceDesc {
    SoundId sound{};
    float gainDb = 0.0f;
    float pitch = 1.0f;
    float loopStartSeconds = 0.0f;
    float loopEndSeconds = 0.0f;
    bool looping = false;
    bool streaming = false;
    std::span<const SendDesc> sends;
};

struct VoiceStateDesc {
    VoiceState state{};
    float playheadSeconds = 0.0f;
    float currentGainDb = 0.0f;
};

} // namespace threadmaxx::audio
```

### 4.5 3D audio state

```cpp id="h4m6za"
namespace threadmaxx::audio {

struct ListenerDesc {
    Vec3 position{};
    Vec3 velocity{};
    Vec3 forward{0.0f, 0.0f, 1.0f};
    Vec3 up{0.0f, 1.0f, 0.0f};
};

struct EmitterDesc {
    Vec3 position{};
    Vec3 velocity{};
    float minDistance = 1.0f;
    float maxDistance = 50.0f;
    float dopplerFactor = 1.0f;
    float spread = 0.0f;
};

} // namespace threadmaxx::audio
```

The roadmap’s boundary here is simple: the engine is not supposed to own audio at all, so this state lives entirely in the sibling library or in game-side components. 

## 5. Public API

### 5.1 Device backend interface

```cpp id="c6m8qh"
namespace threadmaxx::audio {

class IAudioDevice {
public:
    virtual ~IAudioDevice() = default;

    virtual bool initialize(const AudioFormat& format, std::size_t bufferFrames) = 0;
    virtual void shutdown() = 0;

    virtual void submit(ConstAudioSpan mixBuffer) = 0;
    virtual AudioFormat format() const noexcept = 0;
    virtual std::size_t bufferFrames() const noexcept = 0;
};

} // namespace threadmaxx::audio
```

### 5.2 Mixer

```cpp id="m1q7av"
namespace threadmaxx::audio {

class AudioMixer {
public:
    explicit AudioMixer(std::unique_ptr<IAudioDevice> device);

    BusId createBus(const BusDesc& desc);
    void destroyBus(BusId bus);

    VoiceId play(const VoiceDesc& desc);
    void stop(VoiceId voice);
    void pause(VoiceId voice);
    void resume(VoiceId voice);

    void setBusGain(BusId bus, float gainDb);
    void setBusMute(BusId bus, bool muted);
    void setBusSolo(BusId bus, bool solo);

    void setListener(ListenerId listener, const ListenerDesc& desc);
    void setEmitter(ListenerId listener, VoiceId voice, const EmitterDesc& desc);

    void mix(float dtSeconds);
};

} // namespace threadmaxx::audio
```

### 5.3 Clip and stream management

```cpp id="d8n4yl"
namespace threadmaxx::audio {

class AudioRegistry {
public:
    SoundId addClip(std::span<const float> samples, AudioFormat format);
    StreamId addStream(std::unique_ptr<class IAudioStream> stream);

    void removeClip(SoundId id);
    void removeStream(StreamId id);

    bool isValid(SoundId id) const noexcept;
    bool isValid(StreamId id) const noexcept;
};

class IAudioStream {
public:
    virtual ~IAudioStream() = default;
    virtual std::size_t read(AudioSpan out) = 0;
    virtual void rewind() = 0;
    virtual bool finished() const = 0;
};

} // namespace threadmaxx::audio
```

### 5.4 DSP helpers

```cpp id="b7p2cd"
namespace threadmaxx::audio {

void applyGain(AudioSpan buffer, float gainDb) noexcept;
void applyPanStereo(AudioSpan buffer, float pan) noexcept;
void applyFadeIn(AudioSpan buffer, float seconds, float sampleRate) noexcept;
void applyFadeOut(AudioSpan buffer, float seconds, float sampleRate) noexcept;

} // namespace threadmaxx::audio
```

### 5.5 Diagnostics

```cpp id="k3f9um"
namespace threadmaxx::audio {

struct MixerStats {
    std::uint32_t activeVoices = 0;
    std::uint32_t allocatedVoices = 0;
    std::uint32_t droppedVoices = 0;
    std::uint32_t underruns = 0;
    float peakL = 0.0f;
    float peakR = 0.0f;
    float rmsL = 0.0f;
    float rmsR = 0.0f;
};

class AudioDiagnostics {
public:
    MixerStats stats() const noexcept;
    void resetPeaks() noexcept;
};

} // namespace threadmaxx::audio
```

## 6. Integration with `threadmaxx`

The roadmap does not require any core engine audio subsystem, so the cleanest integration is through game systems and data-driven state. Audio belongs above the engine. 

### 6.1 Component-driven integration

A game can define its own audio-related components using the existing user-component hook and then feed them into `threadmaxx_audio`.

```cpp id="q9x4np"
class AudioSystem final : public threadmaxx::ISystem {
public:
    void update(threadmaxx::SystemContext& ctx) override {
        ctx.forEachChunk<
            threadmaxx::Transform,
            threadmaxx::Velocity
        >([&](auto& chunk) {
            // update emitters / listeners from world positions
        });
    }
};
```

### 6.2 Event-driven playback

Typical use cases:

* weapon fired → play one-shot sound,
* chest opened → play mechanical click,
* NPC speaks → route voice to dialog bus,
* ambient zone changes → crossfade music,
* hit event → emit positional impact sound.

The engine already provides event channels, so the game can raise audio events and the audio layer can consume them without core changes. 

### 6.3 Frame update order

Recommended order:

1. gather audio-relevant state from the world,
2. update listener and emitter transforms,
3. process queued play/stop events,
4. advance streams,
5. mix voices into buses,
6. submit final buffer to the device.

That keeps audio deterministic enough for replay and easy to debug.

## 7. What the library should not do

* no physics authority,
* no animation graph ownership,
* no navmesh logic,
* no rendering backend,
* no networking stack,
* no editor UI,
* no ECS storage model,
* no hard dependency on any one asset format,
* no hidden state inside the engine.

That is exactly why the roadmap places audio outside the backend boundary. 

## 8. Implementation order

1. device interface,
2. clip playback,
3. bus routing,
4. voice management,
5. streaming playback,
6. 3D spatialization,
7. event-driven triggers,
8. diagnostics,
9. optional DSP filters,
10. capture/export tools.

## 9. Tests to add

* one-shot clip playback,
* bus gain/mute/solo routing,
* voice stealing behavior,
* stream rewind and end-of-stream handling,
* stereo pan correctness,
* 3D attenuation and Doppler tests,
* underrun detection,
* diagnostics snapshot consistency,
* multi-bus mix correctness,
* long-run stress tests with many voices.
