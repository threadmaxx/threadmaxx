#pragma once

#include "threadmaxx_animation/pose.hpp"
#include "threadmaxx_animation/skeleton.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

/// Skeleton retargeting helpers — basic v1.0 surface for the
/// "shared clip on different rigs" use case.
///
/// The model is name-matched: a `RetargetMap` carries a list of
/// `(srcJointIndex, dstJointIndex)` pairs built once from two
/// `SkeletonDesc`s by walking joint names. `retargetPose` copies the
/// per-joint local rotation (and optionally translation/scale) from
/// the source pose to the destination pose; dest joints with no
/// mapping retain their dest-bind-local pose.
///
/// Out of scope for v1.0: bone-length scaling, axis remapping
/// (different rest orientations), constraint-aware re-projection.
/// Those are v1.x topics — the design notes' §5.7 "advanced
/// retarget" boxes.
namespace threadmaxx::animation {

/// Which channels to copy from source onto dest. Rotation is the most
/// portable channel between rigs (same skeletal topology, possibly
/// different bone lengths), so it defaults on. Translation is usually
/// rig-specific (different bone lengths) so defaults off; setting it
/// is safe for character clips authored at unit scale but will compress
/// or stretch limbs otherwise. Scale defaults off — rare in practice.
struct RetargetChannels {
    bool copyRotation = true;
    bool copyTranslation = false;
    bool copyScale = false;
};

/// One source-joint → dest-joint mapping. Indices are into the
/// respective `SkeletonDesc::joints` arrays. Indices outside the
/// joint counts of the actual runtime pose are silently skipped by
/// `retargetPose`.
struct RetargetJointMapping {
    std::uint32_t srcIndex;
    std::uint32_t dstIndex;
};

/// Built from two `SkeletonDesc`s by `buildRetargetMap`. Owns its
/// pair list — copy / move semantics are the std::vector defaults.
/// The two skeleton-name fields are debug-only metadata so a failed
/// retarget can identify what was being mapped.
struct RetargetMap {
    std::string sourceName;
    std::string destName;
    std::vector<RetargetJointMapping> mappings;

    std::size_t size() const noexcept { return mappings.size(); }
    bool empty() const noexcept { return mappings.empty(); }
};

/// Walk both skeletons in joint-index order, emit one mapping for
/// every joint name that exists in both. O(N * M) in the simple
/// implementation (small skeletons in practice) — drop in a name
/// hash lookup later if a benched workload says so. Joint names that
/// appear in `src` but not `dst` (or vice versa) are silently
/// skipped — the report you'd want for "tell me what didn't map" is
/// `diagnostics.hpp`'s job in v1.x.
RetargetMap buildRetargetMap(const SkeletonDesc& src,
                             const SkeletonDesc& dst);

/// Apply a retarget map. `srcPose` is the source-rig joint poses
/// (one per joint in the source skeleton); `dstPose` is the
/// destination buffer (sized for the destination skeleton). Dest
/// joints with no mapping are NOT written — they retain whatever the
/// caller seeded them with (typically the dest skeleton's bind-local
/// pose, or the previous tick's output for stateful chaining).
///
/// Mappings that reference an index out of range of the supplied
/// poses are skipped (debug-safe; no UB on a stale map).
void retargetPose(std::span<const JointPose> srcPose,
                  std::span<JointPose> dstPose,
                  const RetargetMap& map,
                  RetargetChannels channels = {}) noexcept;

/// PoseSpan overload — same semantics, takes a non-owning view on
/// the destination buffer.
inline void retargetPose(std::span<const JointPose> srcPose,
                         PoseSpan dstPose,
                         const RetargetMap& map,
                         RetargetChannels channels = {}) noexcept {
    retargetPose(srcPose,
                 std::span<JointPose>(dstPose.joints.data(),
                                      dstPose.joints.size()),
                 map, channels);
}

} // namespace threadmaxx::animation
