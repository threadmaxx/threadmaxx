#pragma once

/// @file mixer.hpp
/// @brief `AudioMixer` — the top-level entry point for AU2 onward.
///
/// Owns the voice pool, the bus graph, the resident clip registry, and the
/// IAudioDevice the final mixed buffer is submitted to. The hot path (`mix()`)
/// is zero-alloc after `initialize()`; this is contract, enforced by
/// `tests/audio/test_audio_mixer_no_allocations.cpp`.

#include "threadmaxx_audio/buffer.hpp"
#include "threadmaxx_audio/config.hpp"
#include "threadmaxx_audio/device.hpp"
#include "threadmaxx_audio/spatial.hpp"
#include "threadmaxx_audio/types.hpp"
#include "threadmaxx_audio/voice.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace threadmaxx::audio { class IAudioStream; }

namespace threadmaxx::audio {

/// Runtime mixer counters. AU2 populates `activeVoices` / `allocatedVoices`
/// / `droppedVoices`; AU3 ticks `underruns`; AU6 fills the peak/RMS meters.
struct MixerStats {
    std::uint32_t activeVoices    = 0;
    std::uint32_t allocatedVoices = 0;
    std::uint32_t droppedVoices   = 0;
    std::uint32_t underruns       = 0;
    float         peakL           = 0.0f;
    float         peakR           = 0.0f;
    float         rmsL            = 0.0f;
    float         rmsR            = 0.0f;
};

/// Construction-time mixer settings. Bus and voice capacity are fixed at
/// `initialize()`; both feed into the pre-allocation of every hot-path buffer.
struct AudioMixerConfig {
    std::uint32_t maxVoices    = kDefaultMaxVoices;
    std::uint32_t maxBuses     = 16;
    std::uint32_t maxClips     = 256;
    std::uint32_t maxStreams   = 16;
    std::uint32_t maxListeners = 4;
};

class AudioMixer {
public:
    AudioMixer(std::unique_ptr<IAudioDevice> device, AudioMixerConfig config = {});
    ~AudioMixer();

    AudioMixer(const AudioMixer&) = delete;
    AudioMixer& operator=(const AudioMixer&) = delete;
    AudioMixer(AudioMixer&&) noexcept;
    AudioMixer& operator=(AudioMixer&&) noexcept;

    /// Bring the underlying device up at `format` + `bufferFrames` and
    /// reserve every hot-path buffer at the implied size. Returns false on
    /// device-init failure or zero-channel format.
    [[nodiscard]] bool initialize(const AudioFormat& format, std::size_t bufferFrames);

    /// Tear the device down + drop every live voice. Idempotent.
    void shutdown();

    [[nodiscard]] bool initialized() const noexcept;
    [[nodiscard]] AudioFormat format() const noexcept;
    [[nodiscard]] std::size_t bufferFrames() const noexcept;

    /// Non-owning device accessor — useful for tests that need to inspect
    /// the captured output (e.g. `LoopbackDevice::capturedBuffers()`).
    [[nodiscard]] IAudioDevice&       device() noexcept;
    [[nodiscard]] const IAudioDevice& device() const noexcept;

    /// Register a clip from interleaved float samples. Copies into engine-
    /// owned storage; the source span can be freed after return. Returns
    /// `SoundId{0}` if the clip registry is full.
    [[nodiscard]] SoundId addClip(std::span<const float> samples, AudioFormat format);
    void removeClip(SoundId id);
    [[nodiscard]] bool isValidClip(SoundId id) const noexcept;

    /// Register a streaming source. The mixer takes ownership; the stream's
    /// `read()` runs on the mix thread. Returns `StreamId{0}` if the stream
    /// table is full.
    [[nodiscard]] StreamId addStream(std::unique_ptr<IAudioStream> stream);
    void removeStream(StreamId id);
    [[nodiscard]] bool isValidStream(StreamId id) const noexcept;

    /// The implicit master bus. Always alive, can have gain/mute but is
    /// never affected by solo logic (it's the output node).
    [[nodiscard]] BusId masterBus() const noexcept;

    /// Append a routing bus. Returns `BusId{0}` (i.e. master) if the bus
    /// table is full — the caller can detect via `id == masterBus()`.
    [[nodiscard]] BusId createBus(const BusDesc& desc = {});
    void destroyBus(BusId id);
    void setBusGain(BusId id, float gainDb);
    void setBusMute(BusId id, bool muted);
    void setBusSolo(BusId id, bool solo);
    [[nodiscard]] bool isValidBus(BusId id) const noexcept;

    /// Start a voice. Returns the new `VoiceId`. On voice-pool exhaustion the
    /// lowest-tick voice is stolen, `MixerStats::droppedVoices` increments,
    /// and the prior `VoiceId` for that slot decodes as stale.
    [[nodiscard]] VoiceId play(const VoiceDesc& desc);
    void stop(VoiceId voice);
    [[nodiscard]] bool isPlaying(VoiceId voice) const noexcept;

    /// AU4 — register a 3D listener. Returns `ListenerId{0}` if the table is
    /// full. The pose is consumed by the spatializer of any voice
    /// `setEmitter`-attached to this listener.
    [[nodiscard]] ListenerId createListener(const ListenerDesc& desc);
    void destroyListener(ListenerId id);
    void setListener(ListenerId id, const ListenerDesc& desc);
    [[nodiscard]] bool isValidListener(ListenerId id) const noexcept;

    /// Attach a 3D emitter to a live voice, tying it to a listener for
    /// spatialization. The voice becomes spatial — its source is down-mixed
    /// to mono and pan/attenuation/Doppler are applied per-frame against the
    /// listener's pose. No-op if voice or listener is invalid.
    void setEmitter(VoiceId voice, ListenerId listener, const EmitterDesc& desc);
    /// Revert a voice to non-spatial playback. No-op if the voice isn't
    /// spatial or doesn't exist.
    void clearEmitter(VoiceId voice);

    /// Generate exactly `bufferFrames()` frames of mixed output and submit
    /// to the device. Zero-alloc after `initialize()`. No-op if the mixer
    /// isn't initialized.
    void mix();

    [[nodiscard]] MixerStats stats() const noexcept;
    void resetPeaks() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace threadmaxx::audio
