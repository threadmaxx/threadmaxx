#pragma once

// M4.8 — AudioSystem owns a single miniaudio engine and a small bank
// of preloaded sound effects. Subscribes to the typed `AudioPlay`
// event channel; on each event, plays a one-shot copy of the indexed
// sound. The implementation lives in `AudioSystem.cpp` — miniaudio is
// a 50k-LOC single-header library and must be `#define
// MINIAUDIO_IMPLEMENTATION`'d in exactly one TU.
//
// Failure modes (all non-fatal):
//   * No audio device present — miniaudio init returns failure, the
//     system becomes a no-op subscriber.
//   * Sound file missing on disk — that slot stays empty; events for it
//     are silently dropped (logged once at init).
//   * Concurrent AudioPlay events from worker threads — miniaudio's
//     own thread-safety covers `ma_engine_play_sound`; the event
//     channel itself is MPSC-safe per `EventChannel.hpp`.

#include <threadmaxx/Components.hpp>
#include <threadmaxx/System.hpp>

#include <filesystem>
#include <memory>

namespace threadmaxx { class Engine; }

namespace tou2d {

class AudioSystem : public threadmaxx::ISystem {
public:
    /// Subscribes to the `AudioPlay` event channel via @p engine and
    /// pre-loads each `audio::*` sound from `<assetDir>/sfx/<file>`.
    /// `engine` is borrowed for event-channel subscription; the system
    /// does not call back into it after construction.
    AudioSystem(threadmaxx::Engine* engine,
                std::filesystem::path assetDir);
    ~AudioSystem() override;

    AudioSystem(const AudioSystem&)            = delete;
    AudioSystem& operator=(const AudioSystem&) = delete;

    const char* name() const noexcept override { return "audio"; }

    void update(threadmaxx::SystemContext& /*ctx*/) override {}

    threadmaxx::ComponentSet reads()  const noexcept override {
        return threadmaxx::ComponentSet::none();
    }
    threadmaxx::ComponentSet writes() const noexcept override {
        return threadmaxx::ComponentSet::none();
    }

    /// Diagnostics: how many sounds successfully loaded out of
    /// `audio::kSoundCount`.
    std::size_t soundsLoaded() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace tou2d
