#include "Check.hpp"
#include "threadmaxx_animation/clip.hpp"
#include "threadmaxx_animation/eval.hpp"
#include "threadmaxx_animation/graph.hpp"
#include "threadmaxx_animation/pose.hpp"

using namespace threadmaxx::animation;

namespace {

constexpr bool nearly(float a, float b, float eps = 1e-5f) noexcept {
    const float d = a - b;
    return (d < eps) && (d > -eps);
}

constexpr bool nearly(const Vec3& a, const Vec3& b, float eps = 1e-5f) noexcept {
    return nearly(a.x, b.x, eps) && nearly(a.y, b.y, eps) && nearly(a.z, b.z, eps);
}

// Linear-in-x sweep over a 2-second clip — every 1 second of playhead
// advance ⇒ 10 units of x translation. Picking a long duration so a
// 2x playbackRate doesn't accidentally wrap.
ClipDesc makeLongSweepClip() {
    ClipDesc c;
    c.name = "long_sweep";
    c.duration = 2.0f;
    c.looping = false;
    c.jointCount = 1;
    c.keyframeTimes = {0.0f, 1.0f, 2.0f};
    c.keyframes.resize(3);
    c.keyframes[0].translation = {0.0f, 0.0f, 0.0f};
    c.keyframes[1].translation = {10.0f, 0.0f, 0.0f};
    c.keyframes[2].translation = {20.0f, 0.0f, 0.0f};
    return c;
}

} // namespace

int main() {
    ClipDesc clip = makeLongSweepClip();

    // 1. Default playbackRate (1.0): dt=0.5 advances clip time by 0.5 →
    // sample x = 5 (half the 0→10 segment).
    {
        AnimationGraph g;
        GraphNodeId clipNode = g.addClip(&clip);
        GraphNodeId outNode = g.addOutput();
        g.connect(clipNode, outNode);
        g.setOutput(outNode);

        Animator a;
        a.setGraph(&g);
        PoseBuffer buf;
        a.evaluate(EvalContext{0.5f, 0.0f, 1.0f}, buf);
        CHECK(nearly(a.nodeTime(clipNode), 0.5f));
        CHECK(nearly(buf.localPose().joints[0].translation, Vec3{5.0f, 0, 0}));
    }

    // 2. Override playbackRate=2.0 via Animator::setParameter: dt=0.5
    // advances clip time by 1.0 → sample at the kf1 keyframe (x=10).
    {
        AnimationGraph g;
        GraphNodeId clipNode = g.addClip(&clip);
        GraphNodeId outNode = g.addOutput();
        g.connect(clipNode, outNode);
        g.setOutput(outNode);

        Animator a;
        a.setGraph(&g);
        a.setParameter(clipNode, "playbackRate", 2.0f);
        CHECK(nearly(a.getParameter(clipNode, "playbackRate"), 2.0f));

        PoseBuffer buf;
        a.evaluate(EvalContext{0.5f, 0.0f, 1.0f}, buf);
        CHECK(nearly(a.nodeTime(clipNode), 1.0f));
        CHECK(nearly(buf.localPose().joints[0].translation, Vec3{10.0f, 0, 0}));
    }

    // 3. Graph default rate (set on the AnimationGraph itself, not the
    // Animator instance): same effect when no per-instance override
    // exists.
    {
        AnimationGraph g;
        GraphNodeId clipNode = g.addClip(&clip, /*playbackRate=*/2.0f);
        GraphNodeId outNode = g.addOutput();
        g.connect(clipNode, outNode);
        g.setOutput(outNode);

        Animator a;
        a.setGraph(&g);

        PoseBuffer buf;
        a.evaluate(EvalContext{0.5f, 0.0f, 1.0f}, buf);
        CHECK(nearly(a.nodeTime(clipNode), 1.0f));
        CHECK(nearly(buf.localPose().joints[0].translation, Vec3{10.0f, 0, 0}));

        // Animator override beats the graph default.
        a.setParameter(clipNode, "playbackRate", 0.5f);
        a.evaluate(EvalContext{0.5f, 0.0f, 1.0f}, buf);
        // Time: 1.0 + 0.5*0.5 = 1.25 → midway between x=10 and x=20 at
        // alpha=0.25 → x = 12.5.
        CHECK(nearly(a.nodeTime(clipNode), 1.25f));
        CHECK(nearly(buf.localPose().joints[0].translation, Vec3{12.5f, 0, 0}));
    }

    // 4. graph.setParameter mutates the default (rebuilding a new
    // Animator sees the new default).
    {
        AnimationGraph g;
        GraphNodeId clipNode = g.addClip(&clip);
        GraphNodeId outNode = g.addOutput();
        g.connect(clipNode, outNode);
        g.setOutput(outNode);

        g.setParameter(clipNode, "playbackRate", 0.5f);
        CHECK(nearly(*g.getParameter(clipNode, "playbackRate"), 0.5f));

        Animator a;
        a.setGraph(&g);
        PoseBuffer buf;
        a.evaluate(EvalContext{1.0f, 0.0f, 1.0f}, buf);
        // 1.0 * 0.5 = 0.5 → x = 5.
        CHECK(nearly(a.nodeTime(clipNode), 0.5f));
        CHECK(nearly(buf.localPose().joints[0].translation, Vec3{5.0f, 0, 0}));
    }

    // 5. Reverse rate plays the clip backward.
    {
        AnimationGraph g;
        GraphNodeId clipNode = g.addClip(&clip);
        GraphNodeId outNode = g.addOutput();
        g.connect(clipNode, outNode);
        g.setOutput(outNode);

        Animator a;
        a.setGraph(&g);
        PoseBuffer buf;

        // Advance forward to t=1.0.
        a.evaluate(EvalContext{1.0f, 0.0f, 1.0f}, buf);
        CHECK(nearly(a.nodeTime(clipNode), 1.0f));

        // Now reverse: rate=-1, dt=0.5 → time goes from 1.0 to 0.5 →
        // x = 5.
        a.setParameter(clipNode, "playbackRate", -1.0f);
        a.evaluate(EvalContext{0.5f, 0.0f, 1.0f}, buf);
        CHECK(nearly(a.nodeTime(clipNode), 0.5f));
        CHECK(nearly(buf.localPose().joints[0].translation, Vec3{5.0f, 0, 0}));
    }

    EXIT_WITH_RESULT();
}
