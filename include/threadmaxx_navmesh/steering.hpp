#pragma once

#include "threadmaxx/Components.hpp"

#include <cmath>
#include <cstdint>
#include <span>

/// Steering + corridor following (N7). `followPath` turns a waypoint
/// list (the funnel-smoothed `PathResult::waypoints` produced by N4
/// onward) plus the agent's current world state into the velocity the
/// agent should apply this tick. Pure function — all input is in the
/// `FollowPathInput`, all derived signals are in `FollowPathOutput`.
///
/// The model is acceleration-limited, not turn-rate-limited: the agent
/// chases its desired velocity but is allowed to change velocity by at
/// most `maxAcceleration * dt` per call. At constant speed this bounds
/// the angular rate to roughly `maxAcceleration / maxSpeed` — that's
/// the "configured limit" the N7 corner test asserts against. The
/// arrival check is a straight XZ-distance comparison against
/// `arrivalRadius`; the y axis is ignored throughout.
namespace threadmaxx::navmesh {

using ::threadmaxx::Vec3;

/// Inputs to `followPath`. The corridor is the smoothed waypoint list
/// the path-query / batch solver produced (`PathResult::waypoints`);
/// `segmentIndex` is the caller-persisted "which segment am I currently
/// walking" cursor, normally seeded to 0 and re-fed each tick from
/// `FollowPathOutput::segmentIndex`.
struct FollowPathInput {
    /// Waypoint sequence `[start, ..., goal]`. Treated XZ-only.
    std::span<const Vec3> corridor;
    /// Agent's current world position.
    Vec3 currentPosition{};
    /// Agent's current world velocity (used as the basis for the
    /// per-tick acceleration clamp).
    Vec3 currentVelocity{};
    /// Top cruise speed scalar.
    float maxSpeed{1.0f};
    /// Max change in velocity allowed per tick: `|v_new - v_old| <=
    /// maxAcceleration * dt`.
    float maxAcceleration{10.0f};
    /// Goal-snap radius. When the XZ distance from `currentPosition` to
    /// the final waypoint drops below this, `finished` flips to true and
    /// the output velocity is zeroed.
    float arrivalRadius{0.1f};
    /// Timestep length, seconds.
    float dt{1.0f / 60.0f};
    /// Persistent cursor: index of the segment currently being walked
    /// (segment `k` runs `corridor[k] -> corridor[k+1]`). Caller seeds
    /// to 0 on the first call and forwards
    /// `FollowPathOutput::segmentIndex` on each subsequent call.
    std::uint32_t segmentIndex{0};
};

/// Outputs from `followPath`. `desiredVelocity` is what the caller
/// should apply to the agent this tick; `nextTarget` is the corridor
/// point currently being aimed at (useful for debug rendering); the
/// caller forwards `segmentIndex` back into the next call's input.
struct FollowPathOutput {
    Vec3 desiredVelocity{};
    Vec3 nextTarget{};
    std::uint32_t segmentIndex{0};
    bool finished{false};
};

namespace detail {

inline float steeringLengthXZ(const Vec3& v) noexcept {
    return std::sqrt(v.x * v.x + v.z * v.z);
}

inline float steeringSqDistXZ(const Vec3& a, const Vec3& b) noexcept {
    const float dx = b.x - a.x;
    const float dz = b.z - a.z;
    return dx * dx + dz * dz;
}

} // namespace detail

/// Pure steering step. See the file-level comment for the model. Safe
/// to call concurrently from any thread; the function reads from `in`
/// (including the borrowed corridor span) and writes to a fresh return
/// value, with no static or global state.
inline FollowPathOutput followPath(const FollowPathInput& in) noexcept {
    FollowPathOutput out;
    out.segmentIndex = in.segmentIndex;

    // Defensive: a corridor without at least two points has no segments
    // to walk. Report immediately finished, hand back the zero velocity
    // the caller's already at.
    if (in.corridor.size() < 2) {
        out.finished = true;
        if (!in.corridor.empty()) out.nextTarget = in.corridor.front();
        return out;
    }

    // Goal-radius check happens up front: if the agent is already close
    // enough to the final waypoint, we stop regardless of which segment
    // the cursor thinks we're on. Matches the N7 spec ("agent within
    // arrivalRadius of the final waypoint reports finished == true").
    const Vec3& goal = in.corridor.back();
    const float arrSq = in.arrivalRadius * in.arrivalRadius;
    if (detail::steeringSqDistXZ(in.currentPosition, goal) <= arrSq) {
        out.finished = true;
        out.nextTarget = goal;
        out.segmentIndex =
            static_cast<std::uint32_t>(in.corridor.size() - 2);
        return out;
    }

    // Clamp the input cursor to a valid segment index. Then walk the
    // cursor forward across any segments the agent has already passed
    // (projection parameter t >= 1 along the segment direction in XZ).
    // Degenerate zero-length segments are skipped over.
    const std::uint32_t lastSeg =
        static_cast<std::uint32_t>(in.corridor.size() - 2);
    if (out.segmentIndex > lastSeg) out.segmentIndex = lastSeg;

    while (out.segmentIndex < lastSeg) {
        const Vec3& a = in.corridor[out.segmentIndex];
        const Vec3& b = in.corridor[out.segmentIndex + 1];
        const float sx = b.x - a.x;
        const float sz = b.z - a.z;
        const float segLen2 = sx * sx + sz * sz;
        if (segLen2 <= 1e-12f) {
            ++out.segmentIndex;
            continue;
        }
        const float px = in.currentPosition.x - a.x;
        const float pz = in.currentPosition.z - a.z;
        const float t = (px * sx + pz * sz) / segLen2;
        if (t >= 1.0f) {
            ++out.segmentIndex;
        } else {
            break;
        }
    }

    // Aim point = the end of the current segment.
    const Vec3& target = in.corridor[out.segmentIndex + 1];
    out.nextTarget = target;

    // Desired velocity = unit-direction-to-target * maxSpeed, with the
    // y component zeroed (steering is planar). If we're sitting exactly
    // on the target there's no direction; fall back to holding the
    // current velocity so the accel clamp dampens it on the next tick.
    const float dx = target.x - in.currentPosition.x;
    const float dz = target.z - in.currentPosition.z;
    const float distToTarget = std::sqrt(dx * dx + dz * dz);
    Vec3 desired{};
    if (distToTarget > 1e-6f) {
        const float inv = in.maxSpeed / distToTarget;
        desired = Vec3{dx * inv, 0.0f, dz * inv};
    }

    // Acceleration clamp. The XZ delta between the desired and current
    // velocity is bounded to `maxAcceleration * dt`. The y component of
    // the output velocity is forced to 0 (planar steering invariant).
    Vec3 cur{in.currentVelocity.x, 0.0f, in.currentVelocity.z};
    const float deltaX = desired.x - cur.x;
    const float deltaZ = desired.z - cur.z;
    const float deltaLen = std::sqrt(deltaX * deltaX + deltaZ * deltaZ);
    const float maxStep = in.maxAcceleration * in.dt;
    Vec3 applied;
    if (deltaLen <= maxStep || maxStep <= 0.0f) {
        applied = desired;
    } else {
        const float scale = maxStep / deltaLen;
        applied = Vec3{cur.x + deltaX * scale, 0.0f, cur.z + deltaZ * scale};
    }
    out.desiredVelocity = applied;
    return out;
}

} // namespace threadmaxx::navmesh
