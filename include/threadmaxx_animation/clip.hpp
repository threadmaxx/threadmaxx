#pragma once

#include "threadmaxx_animation/pose.hpp"
#include "threadmaxx_animation/types.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace threadmaxx::animation {

/// A single sampled pose-at-time pair. Retained for callers that
/// want to address a stored keyframe pose by id; A2's curve sampler
/// consumes the flat `ClipDesc::keyframes` directly and does not need
/// this type.
struct ClipSample {
    PoseId pose{};
    float time{};
};

/// Time-keyed event fired by clip sampling. Time is in seconds
/// within `[0, ClipDesc::duration]`.
struct EventTrackEvent {
    float time{};
    std::string name;
};

/// Clip pose data + metadata.
///
/// Keyframe storage is AoS-flat: `keyframes[k * jointCount + j]` is
/// joint `j`'s pose at keyframe `k`. `keyframeTimes` is parallel,
/// sorted ascending, and must satisfy
/// `keyframeTimes.size() * jointCount == keyframes.size()`. The
/// recommended convention is `keyframeTimes.front() == 0` and
/// `keyframeTimes.back() == duration` so non-looping clips clamp
/// cleanly at both ends.
///
/// Per-channel SoA curves are deliberately deferred — the AoS layout
/// is what `detail::lerp_pose` already consumes, and a profiled
/// crowd-eval bench (A7) is the right place to revisit the decision.
struct ClipDesc {
    std::string name;
    float duration{};
    bool looping{};
    std::vector<EventTrackEvent> events;

    std::uint32_t jointCount{0};
    std::vector<float> keyframeTimes;
    std::vector<JointPose> keyframes;
};

} // namespace threadmaxx::animation
