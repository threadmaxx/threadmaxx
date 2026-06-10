#pragma once

/// @file config.hpp
/// @brief Library-wide compile-time defaults — sample rate, buffer size,
/// voice-pool sizing. AU2+ consume the voice constants; AU1 only needs the
/// device defaults.

#include <cstddef>
#include <cstdint>

namespace threadmaxx::audio {

/// Default mixer sample rate. 48 kHz is the standard rate of every modern
/// consumer audio device.
inline constexpr std::uint32_t kDefaultSampleRate = 48000;

/// Default device buffer size in frames. 1024 frames @ 48 kHz ≈ 21.3 ms —
/// a comfortable trade-off between latency and CPU pressure on the mixer
/// callback.
inline constexpr std::size_t kDefaultBufferFrames = 1024;

/// Default voice-pool size — the AU2 voice allocator caps simultaneous
/// playback at this number, with stealing on overflow.
inline constexpr std::uint32_t kDefaultMaxVoices = 64;

/// Hard cap on a single bus-graph send count. Constant rather than a config
/// option because the in-place send array sizing in AU2 needs to be known
/// at compile time.
inline constexpr std::uint32_t kMaxSendsPerVoice = 4;

} // namespace threadmaxx::audio
