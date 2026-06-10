#pragma once

/// @file buffer.hpp
/// @brief Audio buffer primitives — `AudioFormat`, `AudioSpan`,
/// `ConstAudioSpan`, and the `framesToBytes()` helper.
///
/// AU1 settles the interleaved-vs-planar question: **interleaved**. Every
/// real device backend (ALSA / PulseAudio / WASAPI / CoreAudio / SDL audio)
/// accepts interleaved samples on submit; planar conversions happen inside
/// the DSP layer when individual channels are needed in isolation.

#include "threadmaxx_audio/types.hpp"

#include <cstddef>
#include <cstdint>

namespace threadmaxx::audio {

/// Describes the sample-rate / channel layout of a buffer.
/// Layout and channel count are coupled — `channelCount(layout)` is the
/// authoritative source of truth; the `channels` field exists so a producer
/// can pass a buffer whose channel count was already computed (cheap to
/// validate via `channels == channelCount(layout)`).
struct AudioFormat {
    std::uint32_t sampleRate = 48000;
    std::uint8_t  channels   = 2;
    ChannelLayout layout     = ChannelLayout::Stereo;
};

/// Equality on a format. Used by tests and the loopback device to assert that
/// a submitted buffer's format matches the one it was initialized with.
inline constexpr bool operator==(const AudioFormat& a, const AudioFormat& b) noexcept {
    return a.sampleRate == b.sampleRate
        && a.channels   == b.channels
        && a.layout     == b.layout;
}
inline constexpr bool operator!=(const AudioFormat& a, const AudioFormat& b) noexcept {
    return !(a == b);
}

/// Mutable view into an interleaved float buffer.
/// `interleaved` points at frame 0's first sample. Memory is owned by the
/// producer; the span is non-owning. `frames` is the count of multi-channel
/// frames the buffer holds — total float count is `frames * format.channels`.
struct AudioSpan {
    float*        interleaved = nullptr;
    std::size_t   frames      = 0;
    AudioFormat   format{};
};

/// Read-only view over an interleaved float buffer. Same shape as `AudioSpan`
/// but the producer hands out a `const float*` so the device backend cannot
/// mutate the caller's buffer.
struct ConstAudioSpan {
    const float*  interleaved = nullptr;
    std::size_t   frames      = 0;
    AudioFormat   format{};
};

/// Total raw byte size of `frames` frames at the given format. `sizeof(float)`
/// per sample × channel count. Pure function; tested for exactness in
/// `test_audio_buffer`.
[[nodiscard]] inline constexpr std::size_t framesToBytes(const AudioFormat& format,
                                                         std::size_t frames) noexcept {
    return frames * static_cast<std::size_t>(format.channels) * sizeof(float);
}

/// Total sample count (frames × channels). Convenience for span iteration.
[[nodiscard]] inline constexpr std::size_t samplesIn(const AudioFormat& format,
                                                     std::size_t frames) noexcept {
    return frames * static_cast<std::size_t>(format.channels);
}

} // namespace threadmaxx::audio
