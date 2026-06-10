#pragma once

/// @file clip.hpp
/// @brief Resident audio clip storage — interleaved float samples plus the
/// format they were authored in. AU2 mixer stores clips by `SoundId`; AU3
/// adds a streaming path that bypasses this storage.

#include "threadmaxx_audio/buffer.hpp"

#include <cstddef>
#include <vector>

namespace threadmaxx::audio {

/// In-memory PCM audio clip. Sample data is interleaved (matches buffer.hpp
/// AU1 convention). Ownership is the registry's; consumers borrow via
/// `const Clip&` for the duration of a `mix()` call.
struct Clip {
    AudioFormat        format{};
    std::vector<float> samples;

    /// Frames present in the clip. Zero when `samples` is empty or
    /// `format.channels == 0`.
    [[nodiscard]] std::size_t frames() const noexcept {
        return format.channels == 0
            ? 0
            : samples.size() / static_cast<std::size_t>(format.channels);
    }
};

} // namespace threadmaxx::audio
