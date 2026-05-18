// threadmaxx_simd — public AABB + frustum batch kernels.
//
// `transform_aabb` produces the axis-aligned bound of an oriented +
// scaled box. `frustum_cull` is a sphere-based broad-phase culler
// writing a `uint8_t` visible mask per element.
//
// Both work against the engine's existing types:
//   - `BoundingVolume` from `threadmaxx/Components.hpp` (the "AABB"
//     mentioned in DESIGN_NOTES §5.3).
//   - `Frustum` from `threadmaxx/render/Visibility.hpp` (a normalized
//     6-plane structure that `extractFrustum(camera)` produces).
//
// Sphere-based culling complements (rather than replaces) the
// per-camera mask sweep in `threadmaxx::cullByFrustum`. The engine's
// version handles up to 32 cameras and writes 32-bit masks; this
// library's version is single-frustum-per-call and writes 1-bit
// masks — useful for shadow-pass or single-viewport scenarios where
// the per-camera bitset would be wasted.

#pragma once

#include "config.hpp"
#include "detail/scalar.hpp"
#if THREADMAXX_SIMD_HAS_AVX2
#  include "detail/avx2.hpp"
#endif
#include "views.hpp"

#include <threadmaxx/Components.hpp>           // Vec3, BoundingVolume
#include <threadmaxx/render/Visibility.hpp>    // Frustum

#include <cstdint>
#include <span>

namespace threadmaxx::simd {

/// For each (transform, AABB) pair, compute the axis-aligned bound
/// of the 8 transformed corners. Stops at the shorter span.
///
/// §S4.x — AVX2-vectorized when available. The AVX2 path
/// parallelizes ACROSS the 8 corners of a single AABB (1 AABB per
/// iteration), broadcasting the Transform's fields across 8 lanes
/// and computing the corner transform 8-wide; the horizontal
/// min/max reduction recovers the axis-aligned bound.
inline void transform_aabb(std::span<const Transform> t,
                           std::span<const BoundingVolume> in,
                           std::span<BoundingVolume> out) noexcept {
#if THREADMAXX_SIMD_HAS_AVX2
    detail::avx2::transform_aabb(t, in, out);
#else
    detail::scalar::transform_aabb(t, in, out);
#endif
}

/// Sphere-based broad-phase cull. `visible_mask[i] = 1` iff the
/// sphere at `centers[i]` with radius `radii[i]` is at least
/// partially inside `frustum`; `0` otherwise. The shorter of the
/// three spans bounds iteration.
///
/// §S4 — AVX2-vectorized when available: 8 spheres per iteration
/// via gather + 6-plane survival reduction + movemask pack.
inline void frustum_cull(std::span<const Vec3> centers,
                         std::span<const float> radii,
                         const Frustum& frustum,
                         std::span<std::uint8_t> visible_mask) noexcept {
#if THREADMAXX_SIMD_HAS_AVX2
    detail::avx2::frustum_cull(centers, radii, frustum, visible_mask);
#else
    detail::scalar::frustum_cull(centers, radii, frustum, visible_mask);
#endif
}

// ---- span_view overloads ------------------------------------------------

inline void transform_aabb(span_view<const Transform> t,
                           span_view<const BoundingVolume> in,
                           span_view<BoundingVolume> out) noexcept {
    transform_aabb(t.values, in.values, out.values);
}

inline void frustum_cull(span_view<const Vec3> centers,
                         span_view<const float> radii,
                         const Frustum& frustum,
                         span_view<std::uint8_t> visible_mask) noexcept {
    frustum_cull(centers.values, radii.values, frustum, visible_mask.values);
}

} // namespace threadmaxx::simd
