#pragma once

#include "threadmaxx_animation/clip.hpp"
#include "threadmaxx_animation/detail/pose_math.hpp"
#include "threadmaxx_animation/pose.hpp"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <span>
#include <vector>

/// Stateless clip-sampling primitives. The public surface is the
/// triple `wrapTime` / `sampleClip` / `collectEvents`; the public
/// `Animator` in `eval.hpp` stitches them into a stateful playhead.
///
/// AoS keyframe storage matches `ClipDesc::keyframes` (see clip.hpp).
/// `lerp_pose` from `detail/pose_math.hpp` is the inner blend; the
/// per-channel SoA / SIMD path lives in the A1.x roadmap.
namespace threadmaxx::animation::detail {

/// Normalize an arbitrary `t` against `[0, duration]`. Looping wraps
/// (including negative `t`); non-looping clamps. `duration <= 0`
/// degenerates to 0 so a malformed clip can't divide-by-zero a sampler.
inline float wrapTime(float t, float duration, bool looping) noexcept {
    if (duration <= 0.0f) return 0.0f;
    if (!looping) {
        if (t < 0.0f) return 0.0f;
        if (t > duration) return duration;
        return t;
    }
    float r = std::fmod(t, duration);
    if (r < 0.0f) r += duration;
    return r;
}

/// Linear-interp a clip's pose at `time` into `out`. `out.size()`
/// must equal `clip.jointCount`. Empty / degenerate clips leave `out`
/// untouched (caller is responsible for an initial fill if it cares
/// about the no-keyframe case).
inline void sampleClip(const ClipDesc& clip,
                       float time,
                       std::span<JointPose> out) noexcept {
    if (clip.jointCount == 0) return;
    if (clip.keyframeTimes.empty()) return;
    assert(out.size() == clip.jointCount);
    assert(clip.keyframeTimes.size() * clip.jointCount == clip.keyframes.size());

    const float t = wrapTime(time, clip.duration, clip.looping);
    const auto& times = clip.keyframeTimes;
    const std::size_t numKeys = times.size();
    const std::uint32_t J = clip.jointCount;

    auto copyKey = [&](std::size_t k) {
        const JointPose* src = clip.keyframes.data() + k * J;
        for (std::uint32_t j = 0; j < J; ++j) out[j] = src[j];
    };

    if (numKeys == 1 || t <= times.front()) {
        copyKey(0);
        return;
    }
    if (t >= times.back()) {
        copyKey(numKeys - 1);
        return;
    }

    // Find k such that times[k] <= t < times[k+1]. Linear scan is fine
    // for small key counts; a binary search is the obvious upgrade once
    // long captured clips appear in a benched workload.
    std::size_t k = 0;
    for (std::size_t i = 0; i + 1 < numKeys; ++i) {
        if (times[i] <= t && t < times[i + 1]) {
            k = i;
            break;
        }
    }

    const float t0 = times[k];
    const float t1 = times[k + 1];
    const float span = t1 - t0;
    const float alpha = (span > 0.0f) ? (t - t0) / span : 0.0f;

    std::span<const JointPose> a{clip.keyframes.data() + k * J, J};
    std::span<const JointPose> b{clip.keyframes.data() + (k + 1) * J, J};
    lerp_pose(a, b, alpha, out);
}

/// Append every event with time in `(lastTime, newTime]` to `out`.
/// Plain forward window — looping wrap is handled by
/// `collectEvents` below.
inline void collectEventsForward(const ClipDesc& clip,
                                 float lastTime,
                                 float newTime,
                                 std::vector<EventTrackEvent>& out) {
    for (const auto& ev : clip.events) {
        if (ev.time > lastTime && ev.time <= newTime) {
            out.push_back(ev);
        }
    }
}

/// Wrap-aware event collection. When `wrapped` is true the playhead
/// just crossed `duration`; events in `(lastTime, duration]` and in
/// `[0, newTime]` both fire. When `wrapped` is false a non-monotonic
/// move (newTime < lastTime) is treated as a rewind and fires
/// nothing — the explicit-replay path is on the caller via
/// `Animator::setTime` + a separate replay call (not yet exposed).
inline void collectEvents(const ClipDesc& clip,
                          float lastTime,
                          float newTime,
                          bool wrapped,
                          std::vector<EventTrackEvent>& out) {
    if (clip.events.empty()) return;

    if (!wrapped) {
        if (newTime < lastTime) return;
        collectEventsForward(clip, lastTime, newTime, out);
        return;
    }

    // Tail of the previous loop, then head of the new loop. The head
    // window is closed at both ends so an event keyed at exactly t=0
    // fires once per wrap.
    collectEventsForward(clip, lastTime, clip.duration, out);
    for (const auto& ev : clip.events) {
        if (ev.time >= 0.0f && ev.time <= newTime) {
            out.push_back(ev);
        }
    }
}

} // namespace threadmaxx::animation::detail
