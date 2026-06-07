#pragma once

#include "threadmaxx_animation/clip.hpp"
#include "threadmaxx_animation/detail/graph_eval.hpp"
#include "threadmaxx_animation/graph.hpp"
#include "threadmaxx_animation/pose.hpp"

#include <string_view>
#include <vector>

/// `Animator` — the single-instance runtime that drives pose
/// production.
///
/// Two evaluation modes, mutually exclusive at any given moment:
///
/// 1. **Single-clip mode** (A2): `setClip` + `advance` + `samplePose`.
///    The Animator owns one playhead and writes one pose. Use this
///    when you don't need blending and the agent's animation can be
///    described by a single time-keyed clip.
///
/// 2. **Graph mode** (A3+): `setGraph` + `evaluate(ctx, outPose)`.
///    The Animator walks the bound graph, advancing per-node
///    playheads by `ctx.dt * effectiveRate`, and writes the final
///    pose into `outPose`. A3 only models Clip → Output; A4 adds
///    blends; A5 adds IK; A6 adds warping.
///
/// Switching mode (calling `setClip` after `setGraph` or vice versa)
/// detaches the other binding and resets all internal playheads.
/// Mixing the APIs on a single Animator is not supported.
namespace threadmaxx::animation {

/// Per-evaluate input. `dt` advances per-node playheads; `time` is
/// engine wall-clock seconds (forwarded for time-keyed event uses
/// once they land in a later batch). `globalWeight` is the layered
/// composition weight — meaningful from A4 onward; A3 sample path
/// ignores it.
struct EvalContext {
    float dt = 0.0f;
    float time = 0.0f;
    float globalWeight = 1.0f;
};

/// Per-evaluate output. `dirty == false` signals to the caller that
/// nothing about the inputs changed since the last call — the pose
/// in `outPose` is still valid but the caller may skip downstream
/// work (e.g. re-uploading skinning matrices to the GPU).
struct EvalResult {
    bool dirty = false;
    std::vector<EventTrackEvent> firedEvents;
};

class Animator {
public:
    // === Single-clip mode (A2) ===

    /// Switch to single-clip mode. `clip` may be `nullptr` to detach.
    /// Switching clips (or graphs → clip) resets the playhead to t=0
    /// and drops pending events.
    void setClip(const ClipDesc* clip) noexcept;
    const ClipDesc* clip() const noexcept { return clip_; }

    /// Move the single-clip playhead forward by `dt` seconds.
    void advance(float dt) noexcept;

    /// Seek to absolute `time` without firing any events.
    void setTime(float time) noexcept;
    float time() const noexcept { return time_; }

    /// Write the current single-clip pose into `out`.
    void samplePose(PoseSpan out) const noexcept;

    /// Append every queued event to `dst` and clear the queue.
    void drainEvents(std::vector<EventTrackEvent>& dst);

    bool hasPendingEvents() const noexcept { return !pendingEvents_.empty(); }

    // === Graph mode (A3) ===

    /// Bind a graph. `graph` may be `nullptr` to detach. Switching
    /// graphs (or clip → graph) zeros all per-node playheads and
    /// parameter overrides, and clears any pending events.
    void setGraph(const AnimationGraph* graph) noexcept;
    const AnimationGraph* graph() const noexcept { return graph_; }

    /// Walk the bound graph and write the final pose into `outPose`.
    /// `outPose` is resized to match the graph's output joint count
    /// if it doesn't already.
    ///
    /// `EvalResult::dirty` is `true` iff anything observable changed
    /// since the previous `evaluate` call (a non-zero dt, a parameter
    /// override, or the first evaluate after `setGraph`).
    EvalResult evaluate(EvalContext ctx, PoseBuffer& outPose);

    /// Override the default parameter on a specific node, for this
    /// Animator instance only (the bound graph stays immutable).
    /// Recognized parameter names by node kind:
    ///   - Clip:     `"playbackRate"`
    ///   - Blend1D:  `"param"`
    ///   - Blend2D:  `"x"`, `"y"`
    ///   - Additive: `"weight"`
    ///   - Layer:    `"weight"`
    /// Unknown names are silently ignored. Marks the next `evaluate`
    /// as dirty.
    void setParameter(GraphNodeId node, std::string_view name, float value);

    /// Read-back of the per-node parameter override. Returns the graph
    /// default if no override has been set.
    float getParameter(GraphNodeId node, std::string_view name) const;

    /// Current playhead time for a node (Clip nodes only). Returns 0
    /// for non-clip nodes or invalid ids. Useful for tests and
    /// debugging; production code typically doesn't need to peek.
    float nodeTime(GraphNodeId node) const noexcept;

private:
    // Single-clip mode (A2) state.
    const ClipDesc* clip_ = nullptr;
    float time_ = 0.0f;
    std::vector<EventTrackEvent> pendingEvents_;

    // Graph mode (A3 + A4) state. Per-node runtime + override bits live
    // in `detail::NodeRuntime` so the blend-evaluator helpers in
    // GraphEval.cpp can poke its fields directly without exposing a
    // nested private type on this class.
    const AnimationGraph* graph_ = nullptr;
    std::vector<detail::NodeRuntime> nodeRuntime_;

    // Per-Animator scratch pool for blend evaluation. Used as a LIFO
    // stack during the recursive walk; resized lazily on first use,
    // reused across ticks with zero steady-state allocation.
    std::vector<std::vector<JointPose>> scratchPosePool_;

    bool firstEvalAfterSetGraph_ = false;
    bool paramsChangedSinceLastEval_ = false;
};

} // namespace threadmaxx::animation
