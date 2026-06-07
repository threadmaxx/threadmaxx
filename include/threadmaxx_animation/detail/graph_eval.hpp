#pragma once

#include "threadmaxx_animation/clip.hpp"
#include "threadmaxx_animation/detail/curve_eval.hpp"
#include "threadmaxx_animation/graph.hpp"
#include "threadmaxx_animation/pose.hpp"

#include <cmath>

/// Stateless graph-walk helpers. A3 only models Clip → Output, so
/// this is a thin layer over `detail::sampleClip`. A4+ adds Blend
/// node helpers here.
///
/// The Animator owns per-node playhead state (node times + per-node
/// playback-rate overrides). This namespace just operates on borrowed
/// slices — the same pattern A7's batch evaluation will use.
namespace threadmaxx::animation::detail {

/// Step one Clip node's playhead by `dt * effectiveRate` and write the
/// sampled pose into `out`. Returns the post-step time. Looping /
/// clamping matches the per-clip `looping` flag.
inline float stepClipNode(const ClipNode& node,
                          float currentTime,
                          float dt,
                          float effectiveRate,
                          std::span<JointPose> out) noexcept {
    if (node.clip == nullptr) return currentTime;
    const ClipDesc& clip = *node.clip;
    const float duration = clip.duration;
    if (duration <= 0.0f) {
        sampleClip(clip, 0.0f, out);
        return 0.0f;
    }

    float newTime = currentTime + dt * effectiveRate;
    if (clip.looping) {
        if (newTime >= duration || newTime < 0.0f) {
            newTime = std::fmod(newTime, duration);
            if (newTime < 0.0f) newTime += duration;
        }
    } else {
        if (newTime > duration) newTime = duration;
        if (newTime < 0.0f) newTime = 0.0f;
    }
    sampleClip(clip, newTime, out);
    return newTime;
}

} // namespace threadmaxx::animation::detail
