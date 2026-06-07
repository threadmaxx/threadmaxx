#pragma once

#include "threadmaxx_animation/pose.hpp"
#include "threadmaxx_animation/types.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

/// Cloth attachment hook signatures — v1.0 ships ONLY the public PODs
/// and free-function hook signatures. There is NO solver in this
/// library; per `DESIGN_NOTES.md` §1, the cloth solver lives in a
/// future sibling library (`threadmaxx_cloth`), the same boundary
/// discipline as `threadmaxx_navmesh` vs. `threadmaxx_physics`.
///
/// The hooks exist so that a game's pose-eval pipeline can declare
/// attachment points (per-joint anchors) and a cloth-update callback
/// in a single place, and so the future cloth lib has a stable
/// header to consume. The default `updateCloth` here is a deliberate
/// no-op that lets game code link against `threadmaxx::animation`
/// even when no cloth backend is plugged in.
namespace threadmaxx::animation {

/// One attachment between a cloth particle and a skeleton joint.
/// `localOffset` is the position of the particle in the joint's
/// local frame at the rest pose; the cloth solver uses it to
/// reconstruct the particle's world position each tick.
struct ClothAttachmentPoint {
    std::uint32_t particleIndex{};
    JointId joint{};
    Vec3 localOffset{};
};

/// Static description of one cloth piece. Particle/edge data is
/// solver-side state — this POD only carries the data the animation
/// library needs to map skeleton state onto particle anchors.
struct ClothAttachmentSet {
    std::string name;
    SkeletonRef skeleton{};
    std::vector<ClothAttachmentPoint> attachments;
};

/// Game-supplied callback fired by the cloth backend each tick.
/// Default is a no-op so callers building `threadmaxx::animation`
/// without a cloth backend still link cleanly. When the
/// `threadmaxx_cloth` sibling lands, its public surface will
/// re-declare this hook and provide the real implementation.
struct ClothSolverHooks {
    /// Skeleton pose for the current tick (in skeleton-local frame).
    /// The implementation is expected to drive its particle anchors
    /// from `attachments.joint` indices into `pose.joints`.
    using UpdateFn = void (*)(std::span<const JointPose> pose,
                              const ClothAttachmentSet& attachments,
                              void* userData);

    UpdateFn update = nullptr;
    void* userData = nullptr;
};

/// No-op placeholder — calling this with a null `hooks.update` is a
/// no-op, and the function returns false. With a non-null callback,
/// returns true. Game code can use the return as the "did cloth
/// actually run?" signal for HUD diagnostics.
inline bool updateCloth(std::span<const JointPose> pose,
                        const ClothAttachmentSet& attachments,
                        const ClothSolverHooks& hooks) noexcept {
    if (!hooks.update) return false;
    hooks.update(pose, attachments, hooks.userData);
    return true;
}

} // namespace threadmaxx::animation
