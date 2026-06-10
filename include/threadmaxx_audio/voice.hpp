#pragma once

/// @file voice.hpp
/// @brief Bus and voice descriptors. AU2 ships the minimum subset needed for
/// one-shot playback + bus routing; AU4 extends `VoiceDesc` with 3D state.

#include "threadmaxx_audio/types.hpp"

#include <cstdint>

namespace threadmaxx::audio {

/// Playback state for a live voice.
enum class VoiceState : std::uint8_t {
    Stopped = 0,
    Playing = 1,
    Paused  = 2,
};

/// Routing node in the mixer's bus graph. Buses fold into the master bus
/// after the per-voice mix pass. Mute silences the bus's contribution; solo
/// silences every non-solo'd bus when at least one bus is solo'd.
struct BusDesc {
    float gainDb = 0.0f;
    bool  muted  = false;
    bool  solo   = false;
};

/// Settings handed to `AudioMixer::play()`. AU2 fields only — 3D position
/// and sends arrive in later batches.
struct VoiceDesc {
    SoundId sound{};
    BusId   bus{};               ///< Defaults to the master bus.
    float   gainDb  = 0.0f;
    bool    looping = false;
};

} // namespace threadmaxx::audio
