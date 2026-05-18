// threadmaxx_simd — scalar kernel implementations.
//
// Always available as the fallback. The public `*_ops.hpp` headers
// in later batches dispatch to these when no SIMD backend matches
// the target ISA. In S1 the dispatch path is "always scalar".
//
// Conventions:
//   - Writers (add/sub/scale/madd/normalize) silently no-op past
//     `min(in.size(), out.size())`. Mismatched-length inputs are not
//     a runtime error; the kernel just stops at the shorter span.
//   - `dot` accumulates across the shorter span. Returns 0.0f for
//     empty inputs.
//   - `normalize` on a zero-magnitude vector leaves it as zero (no
//     NaN). Matches the `rsqrt(0)` policy in `simd_math.hpp`.

#pragma once

#include "../simd_math.hpp"

#include <threadmaxx/Components.hpp>           // Vec3, Quat, Transform, Velocity, BoundingVolume
#include <threadmaxx/render/Visibility.hpp>    // Frustum

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace threadmaxx::simd::detail::scalar {

inline void add(std::span<const Vec3> a,
                std::span<const Vec3> b,
                std::span<Vec3> out) noexcept {
    const std::size_t n = std::min({a.size(), b.size(), out.size()});
    for (std::size_t i = 0; i < n; ++i) {
        out[i] = Vec3{a[i].x + b[i].x, a[i].y + b[i].y, a[i].z + b[i].z};
    }
}

inline void sub(std::span<const Vec3> a,
                std::span<const Vec3> b,
                std::span<Vec3> out) noexcept {
    const std::size_t n = std::min({a.size(), b.size(), out.size()});
    for (std::size_t i = 0; i < n; ++i) {
        out[i] = Vec3{a[i].x - b[i].x, a[i].y - b[i].y, a[i].z - b[i].z};
    }
}

inline void scale(std::span<const Vec3> in,
                  float s,
                  std::span<Vec3> out) noexcept {
    const std::size_t n = std::min(in.size(), out.size());
    for (std::size_t i = 0; i < n; ++i) {
        out[i] = Vec3{in[i].x * s, in[i].y * s, in[i].z * s};
    }
}

inline void madd(std::span<const Vec3> a,
                 std::span<const Vec3> b,
                 float s,
                 std::span<Vec3> out) noexcept {
    const std::size_t n = std::min({a.size(), b.size(), out.size()});
    for (std::size_t i = 0; i < n; ++i) {
        out[i] = Vec3{
            a[i].x + b[i].x * s,
            a[i].y + b[i].y * s,
            a[i].z + b[i].z * s,
        };
    }
}

inline void normalize(std::span<const Vec3> in,
                      std::span<Vec3> out) noexcept {
    const std::size_t n = std::min(in.size(), out.size());
    for (std::size_t i = 0; i < n; ++i) {
        const float len2 = in[i].x * in[i].x +
                           in[i].y * in[i].y +
                           in[i].z * in[i].z;
        if (len2 <= 0.0f) {
            out[i] = Vec3{0.0f, 0.0f, 0.0f};
            continue;
        }
        const float invLen = simd::rsqrt(len2);
        out[i] = Vec3{in[i].x * invLen, in[i].y * invLen, in[i].z * invLen};
    }
}

inline float dot(std::span<const Vec3> a,
                 std::span<const Vec3> b) noexcept {
    const std::size_t n = std::min(a.size(), b.size());
    float acc = 0.0f;
    for (std::size_t i = 0; i < n; ++i) {
        acc += a[i].x * b[i].x + a[i].y * b[i].y + a[i].z * b[i].z;
    }
    return acc;
}

// ---- Quaternion helpers --------------------------------------------------
//
// All operate on a single Quat. The span-level kernels below loop these
// over the input. The helpers are inline so AVX2 / SSE2 backends (S4)
// can choose whether to fuse them into vectorized variants or call the
// scalar version on the tail.

/// q * r (Hamilton product). Right-multiply convention: rotating a
/// vector v is `q * v * q⁻¹`.
inline Quat quat_mul_one(const Quat& q, const Quat& r) noexcept {
    return Quat{
        q.w * r.x + q.x * r.w + q.y * r.z - q.z * r.y,
        q.w * r.y - q.x * r.z + q.y * r.w + q.z * r.x,
        q.w * r.z + q.x * r.y - q.y * r.x + q.z * r.w,
        q.w * r.w - q.x * r.x - q.y * r.y - q.z * r.z,
    };
}

/// Unit-length q. Zero-norm input collapses to identity (1 in w);
/// matches the engine's convention and avoids NaN propagation.
inline Quat quat_normalize_one(const Quat& q) noexcept {
    const float len2 = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    if (len2 <= 0.0f) return Quat{0.0f, 0.0f, 0.0f, 1.0f};
    const float inv = simd::rsqrt(len2);
    return Quat{q.x * inv, q.y * inv, q.z * inv, q.w * inv};
}

/// Build a unit quaternion from an axis-angle pair. Handles tiny-angle
/// inputs by falling back to identity (avoids `sin(0)/0` blowup).
inline Quat quat_from_axis_angle(const Vec3& axis, float angleRad) noexcept {
    const float len2 = axis.x * axis.x + axis.y * axis.y + axis.z * axis.z;
    if (len2 <= 0.0f || angleRad == 0.0f) {
        return Quat{0.0f, 0.0f, 0.0f, 1.0f};
    }
    const float invLen = simd::rsqrt(len2);
    const float half = 0.5f * angleRad;
    const float s = std::sin(half);
    const float c = std::cos(half);
    return Quat{
        axis.x * invLen * s,
        axis.y * invLen * s,
        axis.z * invLen * s,
        c,
    };
}

/// Rotate a Vec3 by a unit quaternion. Standard `q * v * q⁻¹`
/// expansion using the Rodrigues identity:
/// `v' = v + 2 * cross(q.xyz, cross(q.xyz, v) + q.w * v)`.
inline Vec3 quat_rotate_vec_one(const Quat& q, const Vec3& v) noexcept {
    // u = q.xyz × v + q.w * v
    const Vec3 u{
        q.y * v.z - q.z * v.y + q.w * v.x,
        q.z * v.x - q.x * v.z + q.w * v.y,
        q.x * v.y - q.y * v.x + q.w * v.z,
    };
    // v' = v + 2 * (q.xyz × u)
    return Vec3{
        v.x + 2.0f * (q.y * u.z - q.z * u.y),
        v.y + 2.0f * (q.z * u.x - q.x * u.z),
        v.z + 2.0f * (q.x * u.y - q.y * u.x),
    };
}

/// SLERP between two unit quaternions. Picks the shortest path (flips
/// `b` if `dot(a, b) < 0`). Falls back to linear-blend-and-normalize
/// when the angle is small enough that `sin(theta)` would underflow.
inline Quat quat_slerp_one(const Quat& a, const Quat& b, float alpha) noexcept {
    float cosTheta = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    Quat bb = b;
    if (cosTheta < 0.0f) {
        bb = Quat{-b.x, -b.y, -b.z, -b.w};
        cosTheta = -cosTheta;
    }
    constexpr float kLerpThreshold = 0.9995f;
    if (cosTheta > kLerpThreshold) {
        // Near-parallel — linear blend + normalize avoids `sin(theta)/0`.
        return quat_normalize_one(Quat{
            a.x + alpha * (bb.x - a.x),
            a.y + alpha * (bb.y - a.y),
            a.z + alpha * (bb.z - a.z),
            a.w + alpha * (bb.w - a.w),
        });
    }
    const float theta = std::acos(simd::clamp(cosTheta, -1.0f, 1.0f));
    const float sinTheta = std::sin(theta);
    const float w0 = std::sin((1.0f - alpha) * theta) / sinTheta;
    const float w1 = std::sin(alpha * theta) / sinTheta;
    return Quat{
        w0 * a.x + w1 * bb.x,
        w0 * a.y + w1 * bb.y,
        w0 * a.z + w1 * bb.z,
        w0 * a.w + w1 * bb.w,
    };
}

// ---- Quaternion kernels --------------------------------------------------

/// In-place per-element normalize of a span of quaternions.
inline void quat_normalize(std::span<Quat> q) noexcept {
    for (auto& x : q) x = quat_normalize_one(x);
}

/// Parallel-span slerp. out[i] = slerp(a[i], b[i], alpha). Stops at
/// the shorter span.
inline void quat_slerp(std::span<const Quat> a,
                       std::span<const Quat> b,
                       std::span<Quat> out,
                       float alpha) noexcept {
    const std::size_t n = std::min({a.size(), b.size(), out.size()});
    for (std::size_t i = 0; i < n; ++i) {
        out[i] = quat_slerp_one(a[i], b[i], alpha);
    }
}

// ---- Transform kernels ---------------------------------------------------

/// Per-pair application: `out[i] = t[i].position + rotate(t[i].orientation,
/// t[i].scale * points[i])`. Standard local→world projection.
inline void apply_transforms(std::span<const Transform> t,
                             std::span<const Vec3> points,
                             std::span<Vec3> out) noexcept {
    const std::size_t n = std::min({t.size(), points.size(), out.size()});
    for (std::size_t i = 0; i < n; ++i) {
        const Vec3 scaled{
            points[i].x * t[i].scale.x,
            points[i].y * t[i].scale.y,
            points[i].z * t[i].scale.z,
        };
        const Vec3 rotated = quat_rotate_vec_one(t[i].orientation, scaled);
        out[i] = Vec3{
            t[i].position.x + rotated.x,
            t[i].position.y + rotated.y,
            t[i].position.z + rotated.z,
        };
    }
}

/// In-place integration: `t[i].position += v[i].linear * dt` and the
/// orientation is multiplied by a delta quat derived from
/// `v[i].angular * dt` (axis-angle interpretation: direction = axis,
/// magnitude = angle in radians). Zero-magnitude angular velocity
/// leaves orientation untouched.
inline void integrate_positions(std::span<Transform> t,
                                std::span<const Velocity> v,
                                float dt) noexcept {
    const std::size_t n = std::min(t.size(), v.size());
    for (std::size_t i = 0; i < n; ++i) {
        t[i].position = Vec3{
            t[i].position.x + v[i].linear.x * dt,
            t[i].position.y + v[i].linear.y * dt,
            t[i].position.z + v[i].linear.z * dt,
        };
        const Vec3 omega = v[i].angular;
        const float len2 = omega.x * omega.x + omega.y * omega.y + omega.z * omega.z;
        if (len2 > 0.0f) {
            const float len = std::sqrt(len2);
            const float angle = len * dt;
            const Quat dq = quat_from_axis_angle(omega, angle);
            t[i].orientation = quat_normalize_one(
                quat_mul_one(t[i].orientation, dq));
        }
    }
}

/// Linear-only counterpart: ignore angular state, advance position only.
inline void integrate_linear_motion(std::span<Transform> t,
                                    std::span<const Vec3> velocity,
                                    float dt) noexcept {
    const std::size_t n = std::min(t.size(), velocity.size());
    for (std::size_t i = 0; i < n; ++i) {
        t[i].position = Vec3{
            t[i].position.x + velocity[i].x * dt,
            t[i].position.y + velocity[i].y * dt,
            t[i].position.z + velocity[i].z * dt,
        };
    }
}

// ---- AABB + Frustum kernels ----------------------------------------------

/// Transform each input AABB by the corresponding Transform and store
/// the axis-aligned bound of the 8 transformed corners in out[i].
/// Conservative — produces a tight AABB only for axis-aligned
/// rotations; otherwise slightly inflates.
inline void transform_aabb(std::span<const Transform> t,
                           std::span<const BoundingVolume> in,
                           std::span<BoundingVolume> out) noexcept {
    const std::size_t n = std::min({t.size(), in.size(), out.size()});
    for (std::size_t i = 0; i < n; ++i) {
        const Vec3 mn = in[i].min;
        const Vec3 mx = in[i].max;
        const Vec3 corners[8] = {
            {mn.x, mn.y, mn.z}, {mx.x, mn.y, mn.z},
            {mn.x, mx.y, mn.z}, {mx.x, mx.y, mn.z},
            {mn.x, mn.y, mx.z}, {mx.x, mn.y, mx.z},
            {mn.x, mx.y, mx.z}, {mx.x, mx.y, mx.z},
        };
        Vec3 lo{ std::numeric_limits<float>::infinity(),
                 std::numeric_limits<float>::infinity(),
                 std::numeric_limits<float>::infinity()};
        Vec3 hi{-std::numeric_limits<float>::infinity(),
                -std::numeric_limits<float>::infinity(),
                -std::numeric_limits<float>::infinity()};
        for (const auto& c : corners) {
            const Vec3 scaled{
                c.x * t[i].scale.x,
                c.y * t[i].scale.y,
                c.z * t[i].scale.z,
            };
            const Vec3 rotated = quat_rotate_vec_one(t[i].orientation, scaled);
            const Vec3 world{
                t[i].position.x + rotated.x,
                t[i].position.y + rotated.y,
                t[i].position.z + rotated.z,
            };
            lo.x = std::min(lo.x, world.x);
            lo.y = std::min(lo.y, world.y);
            lo.z = std::min(lo.z, world.z);
            hi.x = std::max(hi.x, world.x);
            hi.y = std::max(hi.y, world.y);
            hi.z = std::max(hi.z, world.z);
        }
        out[i].min = lo;
        out[i].max = hi;
    }
}

/// Bounding-sphere broad-phase cull. visible_mask[i] = 1 iff the
/// sphere (centers[i], radii[i]) is at least partially inside the
/// frustum; 0 otherwise. Mask must be at least as long as the
/// shorter of centers / radii.
inline void frustum_cull(std::span<const Vec3> centers,
                         std::span<const float> radii,
                         const Frustum& frustum,
                         std::span<std::uint8_t> visible_mask) noexcept {
    const std::size_t n = std::min({centers.size(), radii.size(),
                                    visible_mask.size()});
    for (std::size_t i = 0; i < n; ++i) {
        std::uint8_t vis = 1;
        for (const auto& p : frustum.planes) {
            const float d = p[0] * centers[i].x + p[1] * centers[i].y +
                            p[2] * centers[i].z + p[3];
            if (d < -radii[i]) { vis = 0; break; }
        }
        visible_mask[i] = vis;
    }
}

} // namespace threadmaxx::simd::detail::scalar
