#pragma once

#include <algorithm>
#include <cmath>

namespace threadmaxx::input::detail {

// Result of a paired-axis radial deadzone. Direction is preserved; the
// post-deadzone magnitude is in [0, 1].
struct Deadzoned2D {
    float x{};
    float y{};
};

// Radial deadzone for a stick pair. Input components are typically in
// [-1, 1]. Magnitude below `inner` is clipped to (0, 0); magnitude in
// [inner, outer] scales linearly from 0 to 1; magnitude above `outer` is
// clamped to unit length while preserving direction.
//
// Requirements: 0 <= inner < outer; outer > 0.
inline Deadzoned2D applyRadial(float x, float y, float inner, float outer) noexcept {
    const float mag = std::sqrt(x * x + y * y);
    if (mag <= inner) return {0.0f, 0.0f};

    const float band = std::max(outer - inner, 1e-6f);
    const float scaled = std::clamp((mag - inner) / band, 0.0f, 1.0f);
    const float s = scaled / mag;
    return {x * s, y * s};
}

// 1D trigger / per-axis threshold. Returns 0 at or below `threshold`;
// linearly scales to 1.0 between `threshold` and 1.0.
inline float applyTrigger(float v, float threshold) noexcept {
    if (v <= threshold) return 0.0f;
    const float band = std::max(1.0f - threshold, 1e-6f);
    return std::clamp((v - threshold) / band, 0.0f, 1.0f);
}

}  // namespace threadmaxx::input::detail
