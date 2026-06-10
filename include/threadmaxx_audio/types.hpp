#pragma once

/// @file types.hpp
/// @brief Opaque audio-runtime handles + channel-layout enumeration.
///
/// These are the smallest data types the public API can pass around. They
/// are deliberately POD: trivially copyable, no constructors, no allocations.
/// Subsystems shipped in later batches (mixer, voice allocator, streams) hand
/// these out and consume them across thread boundaries.

#include <cstdint>

namespace threadmaxx::audio {

/// Identifies a registered audio clip (resident sample data).
struct SoundId    { std::uint64_t value = 0; };
/// Identifies a live playing voice instance.
struct VoiceId    { std::uint64_t value = 0; };
/// Identifies a mixer bus (a routing node in the bus graph).
struct BusId      { std::uint64_t value = 0; };
/// Identifies a streaming audio source.
struct StreamId   { std::uint64_t value = 0; };
/// Identifies a 3D listener (multiple listeners support split-screen).
struct ListenerId { std::uint64_t value = 0; };

/// Equality on the underlying integer value — the only relation defined.
inline constexpr bool operator==(SoundId    a, SoundId    b) noexcept { return a.value == b.value; }
inline constexpr bool operator==(VoiceId    a, VoiceId    b) noexcept { return a.value == b.value; }
inline constexpr bool operator==(BusId      a, BusId      b) noexcept { return a.value == b.value; }
inline constexpr bool operator==(StreamId   a, StreamId   b) noexcept { return a.value == b.value; }
inline constexpr bool operator==(ListenerId a, ListenerId b) noexcept { return a.value == b.value; }

inline constexpr bool operator!=(SoundId    a, SoundId    b) noexcept { return !(a == b); }
inline constexpr bool operator!=(VoiceId    a, VoiceId    b) noexcept { return !(a == b); }
inline constexpr bool operator!=(BusId      a, BusId      b) noexcept { return !(a == b); }
inline constexpr bool operator!=(StreamId   a, StreamId   b) noexcept { return !(a == b); }
inline constexpr bool operator!=(ListenerId a, ListenerId b) noexcept { return !(a == b); }

/// Channel arrangement of an audio buffer. The mapping to a concrete channel
/// count is fixed by `channelCount()` and verified by `test_audio_format`.
enum class ChannelLayout : std::uint8_t {
    Mono     = 0,
    Stereo   = 1,
    Quad     = 2,
    FiveOne  = 3,
    SevenOne = 4,
    Ambisonic = 5,
};

/// Returns the channel count implied by a `ChannelLayout`.
/// Ambisonic is treated as first-order (4 channels: W,X,Y,Z) — higher-order
/// support is v1.x.
[[nodiscard]] inline constexpr std::uint8_t channelCount(ChannelLayout layout) noexcept {
    switch (layout) {
        case ChannelLayout::Mono:      return 1;
        case ChannelLayout::Stereo:    return 2;
        case ChannelLayout::Quad:      return 4;
        case ChannelLayout::FiveOne:   return 6;
        case ChannelLayout::SevenOne:  return 8;
        case ChannelLayout::Ambisonic: return 4;
    }
    return 0;
}

} // namespace threadmaxx::audio
