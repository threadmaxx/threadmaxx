// threadmaxx_simd — small float utilities.
//
// Scalar-only in S1. Future batches may add SIMD specializations
// (e.g. an AVX2 `rsqrt_ps` + one Newton iteration) but the scalar
// versions are always available so the public kernels can lower to
// them on the fallback path.

#pragma once

#include <algorithm>
#include <cmath>

namespace threadmaxx::simd {

/// Scalar reciprocal-square-root. Returns `1.0f` for non-positive
/// input (avoids `inf` / `NaN` propagation through `normalize`-style
/// kernels). The SIMD specializations in later batches preserve this
/// zero-input policy.
inline float rsqrt(float x) noexcept {
    return x > 0.0f ? 1.0f / std::sqrt(x) : 1.0f;
}

inline float clamp(float v, float lo, float hi) noexcept {
    return std::min(std::max(v, lo), hi);
}

inline float min(float a, float b) noexcept { return std::min(a, b); }
inline float max(float a, float b) noexcept { return std::max(a, b); }

} // namespace threadmaxx::simd
