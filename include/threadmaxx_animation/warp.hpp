#pragma once

#include "threadmaxx_animation/pose.hpp"

#include <cstdint>

/// Motion warping. Morphs a single joint's authored translation toward
/// a target over a time window. Foot-placement and attack-alignment
/// are the canonical use cases:
///
///   - **Foot placement**: caller sets `from` to the authored foot
///     world position at `endTime` (sampled from the clip) and `to`
///     to the desired plant position. The warp adds a progressively
///     larger offset across `[startTime, endTime]` so the foot lands
///     exactly at `to` when the window closes.
///   - **Attack alignment**: same idea applied to the root joint so a
///     strike lands at a moving target.
///
/// The warp is additive — `pose.joints[jointIndex].translation +=
/// (to - from) * alpha * weight` — and leaves rotation, scale, and
/// every other joint untouched. Caller samples the clip first, then
/// hands the sampled pose to `applyWarp`.
///
/// Outside the `[startTime, endTime]` window the pose is unmodified.
/// At `time == startTime` the warp contributes 0. At
/// `time == endTime` it contributes `(to - from) * weight`. Partial
/// `weight` ∈ [0, 1] produces an authored→target lerp at the end of
/// the window.
///
/// Out of scope (deferred — see FUTURE_WORK.md A6 / v1.x):
///   - Multi-joint warps (graph "Warping" node, named joint
///     selection beyond a single index)
///   - Rotation warping
///   - Contact-IK during warping (would compose A5 + A6)
namespace threadmaxx::animation {

/// Description of a single-joint translation warp.
///
/// `from` is the authored target position to subtract; `to` is the
/// desired position to inject. The offset `(to - from)` is scaled by
/// `alpha = (time - startTime) / (endTime - startTime)` and by
/// `weight`. `jointIndex` selects which joint of the pose receives
/// the offset; defaults to 0 (the root joint), which matches the
/// "root-motion warp" canonical use case.
struct WarpRequest {
    Vec3 from{};
    Vec3 to{};
    float startTime{};
    float endTime{};
    float weight = 1.0f;
    std::uint32_t jointIndex = 0;
};

/// Outcome of an `applyWarp` call. `applied == true` when `time` was
/// inside `[startTime, endTime]` and the joint index was in range —
/// even if the resulting offset was zero (e.g. weight == 0 or
/// time == startTime). `alpha` is the normalized progress through
/// the window (0 at `startTime`, 1 at `endTime`); meaningful only
/// when `applied == true`.
struct WarpResult {
    bool applied = false;
    float alpha = 0.0f;
};

/// Apply the warp to `pose` at sampled `time`. Modifies only
/// `pose.joints[request.jointIndex].translation` (additive).
///
/// No-op (returns `WarpResult{}` with `applied == false`) when:
///   - `pose.joints` is empty
///   - `request.jointIndex` is out of range
///   - `time < request.startTime` or `time > request.endTime`
///   - the window is degenerate (`endTime < startTime`)
///
/// When `endTime == startTime` and `time` equals both, `alpha = 1`
/// and the full offset is applied — an instantaneous warp at a
/// single time point.
WarpResult applyWarp(PoseSpan pose, float time, const WarpRequest& request) noexcept;

}  // namespace threadmaxx::animation
