// threadmaxx_simd — public Transform batch kernels.
//
// Current dispatch:
//   - `apply_transforms`         →  AVX2 when built (1.23× win).
//   - `integrate_positions`      →  ALWAYS scalar (Transform-stride
//     gather PLUS per-element quaternion composition; no AVX2 impl).
//   - `integrate_linear_motion`  →  ALWAYS scalar (AVX2 impl exists
//     for reference but the Transform 40-byte stride costs ≈ scalar
//     throughput per benchmark).
//
// Conventions:
//   - All kernels walk parallel spans. Writers stop at the shorter
//     span; no exception is thrown for size mismatch.
//   - `integrate_positions` treats `Velocity.angular` as an axis-
//     angle pair (direction = axis, magnitude = rad/s) and composes
//     the per-tick delta into the orientation via normalized quat
//     multiplication.
//   - `integrate_linear_motion` advances position only; it's the
//     hot-path for projectiles / particle systems that don't carry
//     orientation state.

#pragma once

#include "config.hpp"
#include "detail/scalar.hpp"
#if THREADMAXX_SIMD_HAS_AVX2
#  include "detail/avx2.hpp"
#endif
#include "views.hpp"

#include <threadmaxx/Components.hpp>   // Transform, Velocity, Vec3

#include <span>

namespace threadmaxx::simd {

/// Project per-pair local-space points through the matching
/// Transform: `out[i] = t[i].position + rotate(t[i].orientation,
/// t[i].scale * points[i])`.
///
/// §S4.x — AVX2-vectorized when available. The AVX2 path uses
/// stride-10 gathers for Transform fields + stride-3 gathers for
/// points, then runs the Rodrigues quaternion rotation 8-wide.
inline void apply_transforms(std::span<const Transform> t,
                             std::span<const Vec3> points,
                             std::span<Vec3> out) noexcept {
#if THREADMAXX_SIMD_HAS_AVX2
    detail::avx2::apply_transforms(t, points, out);
#else
    detail::scalar::apply_transforms(t, points, out);
#endif
}

/// In-place integration of linear AND angular state over `dt`.
///
/// §S4.x — stays on the scalar path. Vectorizing requires both a
/// Transform-stride gather/scatter AND per-element quaternion
/// composition (orientation *= delta_quat with renormalization).
/// Two layers of complexity; benchmark didn't justify the
/// implementation effort vs. the marginal win.
inline void integrate_positions(std::span<Transform> t,
                                std::span<const Velocity> v,
                                float dt) noexcept {
    detail::scalar::integrate_positions(t, v, dt);
}

/// In-place pure-translation integration. Cheaper than
/// `integrate_positions` when angular state isn't needed (no
/// quaternion math fires per element).
///
/// §S4 close-out: ALWAYS dispatches to scalar even when AVX2 is
/// built. Benchmark (`bench/simd_kernels`) confirms what the
/// pre-implementation analysis predicted: the Transform 40-byte
/// stride means the AVX2 path needs 6 separate stride-10 / stride-3
/// gather instructions per 8 elements, and the gather latency
/// dominates the trivial 3-mul/3-add integration body. Net result
/// is ~5% SLOWER than scalar. AVX2 impl preserved in
/// `detail/avx2.hpp` + equivalence test.
inline void integrate_linear_motion(std::span<Transform> t,
                                    std::span<const Vec3> velocity,
                                    float dt) noexcept {
    detail::scalar::integrate_linear_motion(t, velocity, dt);
}

// ---- span_view overloads ------------------------------------------------

inline void apply_transforms(span_view<const Transform> t,
                             span_view<const Vec3> points,
                             span_view<Vec3> out) noexcept {
    apply_transforms(t.values, points.values, out.values);
}

inline void integrate_positions(span_view<Transform> t,
                                span_view<const Velocity> v,
                                float dt) noexcept {
    integrate_positions(t.values, v.values, dt);
}

inline void integrate_linear_motion(span_view<Transform> t,
                                    span_view<const Vec3> velocity,
                                    float dt) noexcept {
    integrate_linear_motion(t.values, velocity.values, dt);
}

} // namespace threadmaxx::simd
