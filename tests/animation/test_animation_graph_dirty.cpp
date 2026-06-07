#include "Check.hpp"
#include "threadmaxx_animation/clip.hpp"
#include "threadmaxx_animation/eval.hpp"
#include "threadmaxx_animation/graph.hpp"
#include "threadmaxx_animation/pose.hpp"

using namespace threadmaxx::animation;

namespace {

ClipDesc makeXSweepClip() {
    ClipDesc c;
    c.name = "x_sweep";
    c.duration = 1.0f;
    c.looping = false;
    c.jointCount = 1;
    c.keyframeTimes = {0.0f, 1.0f};
    c.keyframes.resize(2);
    c.keyframes[0].translation = {0.0f, 0.0f, 0.0f};
    c.keyframes[1].translation = {10.0f, 0.0f, 0.0f};
    return c;
}

} // namespace

int main() {
    // 1. The first evaluate after setGraph is always dirty.
    {
        ClipDesc clip = makeXSweepClip();
        AnimationGraph g;
        GraphNodeId clipNode = g.addClip(&clip);
        GraphNodeId outNode = g.addOutput();
        g.connect(clipNode, outNode);
        g.setOutput(outNode);

        Animator a;
        a.setGraph(&g);
        PoseBuffer buf;

        EvalResult r = a.evaluate(EvalContext{0.0f, 0.0f, 1.0f}, buf);
        CHECK(r.dirty);  // first-after-setGraph
    }

    // 2. evaluate with dt=0 and no param change after a first call is
    // NOT dirty.
    {
        ClipDesc clip = makeXSweepClip();
        AnimationGraph g;
        GraphNodeId clipNode = g.addClip(&clip);
        GraphNodeId outNode = g.addOutput();
        g.connect(clipNode, outNode);
        g.setOutput(outNode);

        Animator a;
        a.setGraph(&g);
        PoseBuffer buf;

        a.evaluate(EvalContext{0.0f, 0.0f, 1.0f}, buf);   // first → dirty
        EvalResult r1 = a.evaluate(EvalContext{0.0f, 0.0f, 1.0f}, buf);
        CHECK(!r1.dirty);
        EvalResult r2 = a.evaluate(EvalContext{0.0f, 0.0f, 1.0f}, buf);
        CHECK(!r2.dirty);
    }

    // 3. dt != 0 makes the next evaluate dirty.
    {
        ClipDesc clip = makeXSweepClip();
        AnimationGraph g;
        GraphNodeId clipNode = g.addClip(&clip);
        GraphNodeId outNode = g.addOutput();
        g.connect(clipNode, outNode);
        g.setOutput(outNode);

        Animator a;
        a.setGraph(&g);
        PoseBuffer buf;

        a.evaluate(EvalContext{0.0f, 0.0f, 1.0f}, buf);   // dirty (first)
        EvalResult clean = a.evaluate(EvalContext{0.0f, 0.0f, 1.0f}, buf);
        CHECK(!clean.dirty);

        EvalResult advanced = a.evaluate(EvalContext{0.1f, 0.0f, 1.0f}, buf);
        CHECK(advanced.dirty);

        // Once advanced, settling back to dt=0 produces clean again.
        EvalResult settle = a.evaluate(EvalContext{0.0f, 0.0f, 1.0f}, buf);
        CHECK(!settle.dirty);
    }

    // 4. setParameter marks the next evaluate dirty even when dt=0.
    {
        ClipDesc clip = makeXSweepClip();
        AnimationGraph g;
        GraphNodeId clipNode = g.addClip(&clip);
        GraphNodeId outNode = g.addOutput();
        g.connect(clipNode, outNode);
        g.setOutput(outNode);

        Animator a;
        a.setGraph(&g);
        PoseBuffer buf;

        a.evaluate(EvalContext{0.0f, 0.0f, 1.0f}, buf);   // first → dirty
        EvalResult clean = a.evaluate(EvalContext{0.0f, 0.0f, 1.0f}, buf);
        CHECK(!clean.dirty);

        a.setParameter(clipNode, "playbackRate", 2.0f);
        EvalResult afterParam = a.evaluate(EvalContext{0.0f, 0.0f, 1.0f}, buf);
        CHECK(afterParam.dirty);

        // After consuming the change, the next dt=0 evaluate is clean.
        EvalResult settle = a.evaluate(EvalContext{0.0f, 0.0f, 1.0f}, buf);
        CHECK(!settle.dirty);
    }

    // 5. Unrecognized parameter names don't flip dirty (silently ignored).
    {
        ClipDesc clip = makeXSweepClip();
        AnimationGraph g;
        GraphNodeId clipNode = g.addClip(&clip);
        GraphNodeId outNode = g.addOutput();
        g.connect(clipNode, outNode);
        g.setOutput(outNode);

        Animator a;
        a.setGraph(&g);
        PoseBuffer buf;

        a.evaluate(EvalContext{0.0f, 0.0f, 1.0f}, buf);   // first
        a.evaluate(EvalContext{0.0f, 0.0f, 1.0f}, buf);   // clean

        a.setParameter(clipNode, "doesNotExist", 99.0f);
        EvalResult r = a.evaluate(EvalContext{0.0f, 0.0f, 1.0f}, buf);
        CHECK(!r.dirty);
    }

    EXIT_WITH_RESULT();
}
