#pragma once

#include "threadmaxx_animation/pose.hpp"

#include <cmath>
#include <cstddef>
#include <span>

/// Pose validation diagnostics — the v1.0 contract for catching
/// arithmetic regressions before they propagate into the renderer.
///
/// All entry points are header-only, `noexcept`, and read-only over the
/// caller-supplied PoseSpan / std::span. Validation never allocates, and
/// never mutates. Callers asserting the pose is valid wrap the result
/// of `validatePose` in their own contract check.
namespace threadmaxx::animation {

/// Per-joint validation failure category.
///
/// Bit-flag layout so a single uint32 can carry every issue a joint
/// hit — useful for crowd-eval loops that want to count distinct
/// failure modes without separate passes.
enum class PoseIssue : std::uint32_t {
    None              = 0,
    NanTranslation    = 1u << 0,
    NanRotation       = 1u << 1,
    NanScale          = 1u << 2,
    DenormalRotation  = 1u << 3,  ///< `|q| < 1e-3` — near-zero quat.
    DegenerateScale   = 1u << 4,  ///< any scale axis ≤ 0 (sign-flip or collapse).
};

constexpr PoseIssue operator|(PoseIssue a, PoseIssue b) noexcept {
    return static_cast<PoseIssue>(
        static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}
constexpr PoseIssue& operator|=(PoseIssue& a, PoseIssue b) noexcept {
    a = a | b;
    return a;
}
constexpr bool any(PoseIssue x) noexcept {
    return static_cast<std::uint32_t>(x) != 0;
}
constexpr bool has(PoseIssue x, PoseIssue bit) noexcept {
    return (static_cast<std::uint32_t>(x) & static_cast<std::uint32_t>(bit))
           != 0;
}

/// Aggregate report — one bit-flag per joint plus an overall summary.
/// Joint indices NOT in `[0, jointCount)` are silently ignored (the
/// caller-provided span's size IS the joint count).
struct PoseValidationReport {
    PoseIssue overall = PoseIssue::None;
    std::size_t firstBadJoint = 0;
    std::size_t badJointCount = 0;

    bool ok() const noexcept { return !any(overall); }
};

namespace detail {

inline bool isFinite(float f) noexcept {
    return std::isfinite(f);
}

inline bool isFinite(const Vec3& v) noexcept {
    return isFinite(v.x) && isFinite(v.y) && isFinite(v.z);
}

inline bool isFinite(const Quat& q) noexcept {
    return isFinite(q.x) && isFinite(q.y) && isFinite(q.z) && isFinite(q.w);
}

inline float quatLengthSquared(const Quat& q) noexcept {
    return q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
}

inline PoseIssue classifyJoint(const JointPose& jp) noexcept {
    PoseIssue out = PoseIssue::None;
    if (!isFinite(jp.translation)) out |= PoseIssue::NanTranslation;
    if (!isFinite(jp.rotation))    out |= PoseIssue::NanRotation;
    if (!isFinite(jp.scale))       out |= PoseIssue::NanScale;
    if (isFinite(jp.rotation)
        && quatLengthSquared(jp.rotation) < 1e-6f) {
        out |= PoseIssue::DenormalRotation;
    }
    if (isFinite(jp.scale)
        && (jp.scale.x <= 0.0f
            || jp.scale.y <= 0.0f
            || jp.scale.z <= 0.0f)) {
        out |= PoseIssue::DegenerateScale;
    }
    return out;
}

} // namespace detail

/// Validate every joint in `joints`. Walk is single-pass, in order;
/// `firstBadJoint` records the lowest index with any issue (or 0 if
/// none). `badJointCount` is the count of joints with at least one
/// non-`None` flag set.
inline PoseValidationReport validatePose(
    std::span<const JointPose> joints) noexcept {
    PoseValidationReport rep;
    bool firstSet = false;
    for (std::size_t i = 0; i < joints.size(); ++i) {
        const PoseIssue jointFlags = detail::classifyJoint(joints[i]);
        if (any(jointFlags)) {
            rep.overall |= jointFlags;
            if (!firstSet) {
                rep.firstBadJoint = i;
                firstSet = true;
            }
            ++rep.badJointCount;
        }
    }
    return rep;
}

/// PoseSpan overload — same semantics, takes the non-owning view.
inline PoseValidationReport validatePose(PoseSpan pose) noexcept {
    return validatePose(
        std::span<const JointPose>(pose.joints.data(),
                                   pose.joints.size()));
}

/// Single-joint convenience entry — exposed so callers driving an
/// IK / warp pipeline can check individual joints without scanning a
/// whole pose.
inline PoseIssue validateJoint(const JointPose& jp) noexcept {
    return detail::classifyJoint(jp);
}

} // namespace threadmaxx::animation
