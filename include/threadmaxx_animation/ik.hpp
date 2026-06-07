#pragma once

#include "threadmaxx_animation/pose.hpp"

#include <cstdint>
#include <span>

/// Inverse kinematics for short skeletal chains. v1.0 ships a
/// position-space FABRIK solver plus an analytical 2-bone helper for
/// reference / unit-test asserts.
///
/// The solver operates on world-space joint positions handed to it by
/// the caller. Per-joint rotation reconstruction (so the result feeds
/// back into a `JointPose` stream) is intentionally the caller's
/// problem in v1.0 — A5's scope is the position-space chain; a graph
/// IK node that owns the rotation reconstruction is deferred to a
/// later batch (the `NodeType::IK` slot already exists in `graph.hpp`).
///
/// Out of scope (per design notes §1 and A5's "Risks" entry):
///   - twist/hinge joint constraints
///   - pole vectors beyond the 2-bone helper
///   - target rotation enforcement (the `IKTarget::rotation` field is
///     reserved; v1.0 does not apply it)
namespace threadmaxx::animation {

/// Solve parameters. Position is the world-space target the chain's
/// end-effector should reach. Weight blends the post-solve chain
/// against the input chain — 0 leaves the chain untouched, 1 commits
/// the full solve. `tolerance` is the convergence threshold (end →
/// target distance) and `maxIterations` caps the FABRIK loop.
struct IKTarget {
    Vec3 position{};
    Quat rotation{0.0f, 0.0f, 0.0f, 1.0f};  // reserved; not applied in v1.0
    float weight = 1.0f;
    std::uint32_t maxIterations = 16;
    float tolerance = 1e-3f;
};

/// Outcome of a `solveIK` call. `converged == true` when the
/// end-effector landed within `target.tolerance` of `target.position`
/// before the iteration cap was hit (and the target was reachable in
/// the first place). `finalDistance` is the end-to-target distance
/// after the solve (and the blend, if `weight < 1`).
struct IKSolveResult {
    bool converged = false;
    std::uint32_t iterations = 0;
    float finalDistance = 0.0f;
};

/// FABRIK over a chain of world-space joint positions. `positions[0]`
/// is the root (fixed by the solve); `positions.back()` is the
/// end-effector that chases the target. Bone lengths are computed
/// once from the input positions and preserved across iterations.
///
/// Reachable targets: the FABRIK forward/backward sweep iterates
/// until the end-effector is within `target.tolerance` of
/// `target.position` or `target.maxIterations` is hit.
///
/// Unreachable targets (distance from root > sum of bone lengths):
/// the chain is laid out along (target - root), still anchored at
/// the root, with bone lengths preserved. The solver returns
/// `converged = false` and the leftover distance from the
/// straightened end to the target as `finalDistance`. No NaN, no
/// infinite loop.
///
/// Weight blending: if `target.weight < 1`, the post-solve chain is
/// lerp'd with the input chain component-wise. Bone lengths are not
/// re-projected onto the lerp result, so a partial-weight solve
/// produces a non-rigid intermediate pose by design — matches the
/// "IK as a corrective layer" use case where the caller wants a
/// fractional pull toward the target.
///
/// Chains of fewer than two positions are a no-op (converged=true,
/// finalDistance=0).
IKSolveResult solveIK(std::span<Vec3> positions, const IKTarget& target) noexcept;

/// Analytical 2-bone solver (shoulder, elbow, wrist) using the law
/// of cosines. Returns the elbow position given the shoulder, target
/// wrist, a pole vector that disambiguates the elbow plane, and the
/// upper/lower bone lengths.
///
/// If the target is beyond reach (distance from shoulder > upper +
/// lower), the elbow is placed along the shoulder→target line at
/// distance `upperLength` from the shoulder (stretched-toward).
///
/// `pole` is a world-space hint indicating the side of the
/// shoulder→target line the elbow should bend toward. If `pole` is
/// collinear with the shoulder→target axis, the bend direction is
/// arbitrary but stable.
Vec3 solve2BoneIK(const Vec3& shoulder,
                  const Vec3& wristTarget,
                  const Vec3& pole,
                  float upperLength,
                  float lowerLength) noexcept;

}  // namespace threadmaxx::animation
