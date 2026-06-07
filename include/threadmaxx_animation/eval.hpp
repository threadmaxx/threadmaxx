#pragma once

#include "threadmaxx_animation/clip.hpp"
#include "threadmaxx_animation/pose.hpp"

#include <vector>

/// A2 entrypoint: single-clip playhead. A3 extends `Animator` to
/// consume `AnimationGraph` — the surface stays the same on the
/// "what pose comes out" side; what changes is how the pose is
/// built (clip-only vs. graph evaluation).
namespace threadmaxx::animation {

/// Stateful playhead that advances through a single clip, samples
/// the current pose, and emits time-track events on forward
/// crossings (with wrap handling for looping clips). Not thread-safe
/// — one Animator per logical agent; in A7 the engine integration
/// hands each worker a private slice of the entity array so this
/// stays single-writer-per-instance.
class Animator {
public:
    /// `clip` may be `nullptr` to detach. Switching clips resets the
    /// playhead to t=0 and drops any pending events. Doesn't take
    /// ownership; the caller is responsible for the `ClipDesc`'s
    /// lifetime (typically the `AnimationRegistry`).
    void setClip(const ClipDesc* clip) noexcept;
    const ClipDesc* clip() const noexcept { return clip_; }

    /// Move the playhead forward by `dt` seconds (`dt` may be
    /// negative — a rewind — but no events fire on reverse motion).
    /// Looping clips wrap; non-looping clips clamp. Events in the
    /// crossed window are queued; drain with `drainEvents`.
    void advance(float dt) noexcept;

    /// Seek to absolute `time` without firing any events. Use this
    /// for explicit rewinds / jumps / state restores. The seeked
    /// time is normalized (wrapped or clamped) the same way as
    /// `advance` would normalize an arriving time.
    void setTime(float time) noexcept;

    float time() const noexcept { return time_; }

    /// Write the current pose into `out`. `out.size()` must equal
    /// `clip()->jointCount`. No-op if no clip is attached or the
    /// clip has no keyframes. Const so multiple readers in the
    /// engine's render-frame build path can sample without
    /// contention against the sim-thread playhead state — `out`
    /// itself is the only output, and the caller owns it.
    void samplePose(PoseSpan out) const noexcept;

    /// Append every queued event to `dst` and clear the queue.
    /// Callers typically forward into an EventChannel after
    /// per-tick step completes.
    void drainEvents(std::vector<EventTrackEvent>& dst);

    /// True iff at least one event is queued. Useful for skipping
    /// the drain when nothing happened.
    bool hasPendingEvents() const noexcept { return !pendingEvents_.empty(); }

private:
    const ClipDesc* clip_ = nullptr;
    float time_ = 0.0f;
    std::vector<EventTrackEvent> pendingEvents_;
};

} // namespace threadmaxx::animation
