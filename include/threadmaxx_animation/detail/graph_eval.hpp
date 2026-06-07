#pragma once

#include "threadmaxx_animation/clip.hpp"
#include "threadmaxx_animation/detail/curve_eval.hpp"
#include "threadmaxx_animation/graph.hpp"
#include "threadmaxx_animation/pose.hpp"

#include <cmath>

/// Stateless graph-walk helpers + per-node runtime state. The
/// Animator owns a `std::vector<NodeRuntime>` parallel to the bound
/// graph's nodes; this header defines the struct so blend-evaluator
/// helpers in GraphEval.cpp can poke its fields without exposing a
/// nested private type on `Animator`. The override-bit constants live
/// next to it so the per-parameter override path is internally
/// consistent.
namespace threadmaxx::animation::detail {

/// Per-node runtime: playhead time (Clip) + per-parameter override
/// slots + override-bit mask. One entry per node in the bound graph;
/// `overrideMask` bits track which parameters the Animator has been
/// asked to override via `Animator::setParameter`.
struct NodeRuntime {
    float time = 0.0f;          // Clip playhead
    float playbackRate = 1.0f;  // "playbackRate" override (Clip)
    float param = 0.0f;         // "param" override (Blend1D)
    float blendX = 0.0f;        // "x" override (Blend2D)
    float blendY = 0.0f;        // "y" override (Blend2D)
    float weight = 1.0f;        // "weight" override (Additive / Layer)
    std::uint8_t overrideMask = 0;
};

inline constexpr std::uint8_t kRateOverride = 1u << 0;
inline constexpr std::uint8_t kParamOverride = 1u << 1;
inline constexpr std::uint8_t kXOverride = 1u << 2;
inline constexpr std::uint8_t kYOverride = 1u << 3;
inline constexpr std::uint8_t kWeightOverride = 1u << 4;

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
