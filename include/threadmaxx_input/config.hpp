#pragma once

#include <cstddef>

namespace threadmaxx::input {

// Capacity caps for fixed-size storage on InputContext / InputState. All
// overflows drop silently to keep the hot path allocation-free.

inline constexpr std::size_t kMaxGamepads = 8;
inline constexpr std::size_t kMaxCharsPerFrame = 32;
inline constexpr std::size_t kEventDrainBatch = 64;
inline constexpr std::size_t kInitialEventBuffer = 256;

// Deadzone defaults (overridable per-binding once I4 lands).
inline constexpr float kDefaultStickInnerDeadzone = 0.15f;
inline constexpr float kDefaultStickOuterDeadzone = 0.95f;
inline constexpr float kDefaultTriggerThreshold = 0.05f;

}  // namespace threadmaxx::input
