#pragma once

/// @file events.hpp
/// @brief Playback event types + the C-style callback signature the mixer
/// fires them through. Function-pointer + user-data shape avoids the hidden
/// `std::function` allocation so the event path stays compatible with the
/// no-alloc hot-path contract.

#include "threadmaxx_audio/types.hpp"

#include <cstdint>

namespace threadmaxx::audio {

enum class PlaybackEventType : std::uint8_t {
    VoiceStarted = 0,  ///< Fired immediately after `AudioMixer::play()` allocates a voice.
    VoiceStopped = 1,  ///< Fired when the voice frees its slot (clip end, stream EOF, or explicit `stop()`).
    VoiceLooped  = 2,  ///< Fired once per `mix()` call where ≥1 clip-loop wraps occurred.
};

/// Per-event payload. `sound` and `stream` are zero for the half that
/// doesn't apply (a clip voice has `stream.value == 0` and vice versa).
struct PlaybackEvent {
    PlaybackEventType type   = PlaybackEventType::VoiceStarted;
    VoiceId           voice{};
    SoundId           sound{};
    StreamId          stream{};
};

/// Callback signature. `user` is the pointer registered with the mixer.
/// Must not call back into `AudioMixer` mutators (the mixer's invariants
/// are mid-transition when the callback fires); recording the event for
/// later processing is the recommended pattern.
using PlaybackEventCallback = void(*)(const PlaybackEvent& event, void* user);

} // namespace threadmaxx::audio
