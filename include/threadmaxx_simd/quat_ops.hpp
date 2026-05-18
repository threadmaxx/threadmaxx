// threadmaxx_simd — public Quaternion batch kernels.
//
// In S2 the dispatch path is "always scalar"; S4 grows compile-time
// branching onto AVX2 paths when the build target supports them.
//
// Conventions:
//   - `normalize(span<Quat>)` is in-place. Zero-norm input becomes
//     identity (0, 0, 0, 1) — no NaN.
//   - `slerp` walks parallel spans, picking the shortest path when
//     `dot(a, b) < 0`. Stops at the shorter span.

#pragma once

#include "config.hpp"
#include "detail/scalar.hpp"
#if THREADMAXX_SIMD_HAS_AVX2
#  include "detail/avx2.hpp"
#endif
#include "views.hpp"

#include <threadmaxx/Components.hpp>   // Quat

#include <span>

namespace threadmaxx::simd {

/// In-place per-element normalize.
///
/// §S4 close-out: ALWAYS dispatches to scalar even when AVX2 is
/// built. Benchmark (`bench/simd_kernels`) shows the AVX2 path
/// (2 quats per __m256 via `_mm256_dp_ps`) is ~20% slower than
/// scalar — `dp_ps` has ~12-15 cycle latency on Skylake which
/// dwarfs the 4-mul/3-add scalar reduction at this small grain.
/// AVX2 impl stays in `detail/avx2.hpp` for the equivalence test
/// and as a reference for a future SoA-shifted approach.
inline void normalize(std::span<Quat> q) noexcept {
    detail::scalar::quat_normalize(q);
}

/// Spherical linear interpolation across parallel spans:
/// `out[i] = slerp(a[i], b[i], alpha)`. The shortest-path branch
/// kicks in automatically when `dot(a[i], b[i]) < 0`.
inline void slerp(std::span<const Quat> a,
                  std::span<const Quat> b,
                  std::span<Quat> out,
                  float alpha) noexcept {
    detail::scalar::quat_slerp(a, b, out, alpha);
}

// ---- span_view overloads ------------------------------------------------

inline void normalize(span_view<Quat> q) noexcept {
    normalize(q.values);
}

inline void slerp(span_view<const Quat> a,
                  span_view<const Quat> b,
                  span_view<Quat> out,
                  float alpha) noexcept {
    slerp(a.values, b.values, out.values, alpha);
}

} // namespace threadmaxx::simd
