#pragma once

#include "threadmaxx_animation/pose.hpp"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <span>

/// Per-joint pose primitives used by clip sampling, blend nodes, and
/// the eventual SIMD-accelerated kernels in A1.x.
///
/// Quaternion conventions match threadmaxx's HierarchySystem:
///   - `rotate(q, v)`  = q * (0, v) * q⁻¹   (right-handed, w last)
///   - `qmul(a, b)`    = Hamilton product, applied parent-then-local
///
/// Pose composition is the canonical skeletal-animation formula —
/// it differs from HierarchySystem's "scale = local.scale" default
/// because skeletal pose evaluation always propagates parent scale
/// down the chain.
namespace threadmaxx::animation::detail {

constexpr Vec3 rotate(const Quat& q, const Vec3& v) noexcept {
    const Vec3 qv{q.x, q.y, q.z};
    const Vec3 t{
        2.0f * (qv.y * v.z - qv.z * v.y),
        2.0f * (qv.z * v.x - qv.x * v.z),
        2.0f * (qv.x * v.y - qv.y * v.x),
    };
    return Vec3{
        v.x + q.w * t.x + (qv.y * t.z - qv.z * t.y),
        v.y + q.w * t.y + (qv.z * t.x - qv.x * t.z),
        v.z + q.w * t.z + (qv.x * t.y - qv.y * t.x),
    };
}

constexpr Quat qmul(const Quat& a, const Quat& b) noexcept {
    return Quat{
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
    };
}

constexpr Vec3 vmul(const Vec3& a, const Vec3& b) noexcept {
    return Vec3{a.x * b.x, a.y * b.y, a.z * b.z};
}

/// Compose a child's local pose into world space given the parent's
/// already-resolved world pose. Full TRS propagation:
///
///   world.translation = parent.translation
///                     + rotate(parent.rotation, parent.scale ⊙ local.translation)
///   world.rotation    = parent.rotation * local.rotation
///   world.scale       = parent.scale ⊙ local.scale
constexpr JointPose compose(const JointPose& parent, const JointPose& local) noexcept {
    JointPose out;
    out.translation = parent.translation +
                      rotate(parent.rotation, vmul(parent.scale, local.translation));
    out.rotation = qmul(parent.rotation, local.rotation);
    out.scale = vmul(parent.scale, local.scale);
    return out;
}

/// Per-joint linear interpolation. Quaternions are nlerp'd (cheaper
/// than slerp; sufficient for blend weights inside the same animation
/// hemisphere). Sign-aligns b's rotation against a so the shorter
/// arc is taken.
inline JointPose lerp(const JointPose& a, const JointPose& b, float alpha) noexcept {
    const float oneMinus = 1.0f - alpha;
    JointPose out;
    out.translation = Vec3{
        oneMinus * a.translation.x + alpha * b.translation.x,
        oneMinus * a.translation.y + alpha * b.translation.y,
        oneMinus * a.translation.z + alpha * b.translation.z,
    };
    out.scale = Vec3{
        oneMinus * a.scale.x + alpha * b.scale.x,
        oneMinus * a.scale.y + alpha * b.scale.y,
        oneMinus * a.scale.z + alpha * b.scale.z,
    };
    const float dot = a.rotation.x * b.rotation.x + a.rotation.y * b.rotation.y +
                      a.rotation.z * b.rotation.z + a.rotation.w * b.rotation.w;
    const float sign = (dot < 0.0f) ? -1.0f : 1.0f;
    Quat r{
        oneMinus * a.rotation.x + alpha * sign * b.rotation.x,
        oneMinus * a.rotation.y + alpha * sign * b.rotation.y,
        oneMinus * a.rotation.z + alpha * sign * b.rotation.z,
        oneMinus * a.rotation.w + alpha * sign * b.rotation.w,
    };
    const float n2 = r.x * r.x + r.y * r.y + r.z * r.z + r.w * r.w;
    if (n2 > 0.0f) {
        const float inv = 1.0f / std::sqrt(n2);
        r.x *= inv; r.y *= inv; r.z *= inv; r.w *= inv;
    }
    out.rotation = r;
    return out;
}

/// Pose-wide lerp. Both spans must be the same size as `out`.
inline void lerp_pose(std::span<const JointPose> a,
                      std::span<const JointPose> b,
                      float alpha,
                      std::span<JointPose> out) noexcept {
    assert(a.size() == out.size());
    assert(b.size() == out.size());
    for (std::size_t i = 0; i < out.size(); ++i) {
        out[i] = lerp(a[i], b[i], alpha);
    }
}

/// Weighted two-pose blend. `weight` selects from `a` (0) to `b` (1).
/// Identical to lerp_pose in v1.0 — the named entrypoint exists so
/// blend nodes (A4) can attach additive / mask logic later without
/// breaking call sites.
inline void blend_pose_weighted(std::span<const JointPose> a,
                                std::span<const JointPose> b,
                                float weight,
                                std::span<JointPose> out) noexcept {
    lerp_pose(a, b, weight, out);
}

} // namespace threadmaxx::animation::detail
