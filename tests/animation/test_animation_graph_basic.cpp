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

ClipDesc makeXSweepClip() {
    ClipDesc c;
    c.name = "x_sweep";
    c.duration = 1.0f;
    c.looping = false;
    c.jointCount = 1;
    c.keyframeTimes = {0.0f, 0.5f, 1.0f};
    c.keyframes.resize(3);
    c.keyframes[0].translation = {0.0f, 0.0f, 0.0f};
    c.keyframes[1].translation = {10.0f, 0.0f, 0.0f};
    c.keyframes[2].translation = {20.0f, 0.0f, 0.0f};
    return c;
}

} // namespace

int main() {
    // 1. Build a minimal Clip → Output graph; verify nodeCount, output
    // assignment, and jointCount derivation.
    {
        ClipDesc clip = makeXSweepClip();
        AnimationGraph g;
        GraphNodeId clipNode = g.addClip(&clip);
        GraphNodeId outNode = g.addOutput();
        g.connect(clipNode, outNode);
        g.setOutput(outNode);

        CHECK_EQ(g.nodeCount(), std::size_t{2});
        CHECK(g.output() == outNode);
        CHECK(g.nodeType(clipNode) == NodeType::Clip);
        CHECK(g.nodeType(outNode) == NodeType::Output);
        CHECK_EQ(g.jointCount(), std::uint32_t{1});

        const auto outInputs = g.inputs(outNode);
        CHECK_EQ(outInputs.size(), std::size_t{1});
        CHECK(outInputs[0] == clipNode);
    }

    // 2. evaluate at dt=0.5 reproduces the same pose as direct clip
    // sampling at t=0.5 (the middle keyframe → translation = 10).
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

        EvalResult r = a.evaluate(EvalContext{0.5f, 0.0f, 1.0f}, buf);
        CHECK_EQ(buf.size(), std::size_t{1});
        CHECK(nearly(buf.localPose().joints[0].translation, Vec3{10.0f, 0, 0}));
        CHECK(r.dirty);  // dt != 0 → dirty
        CHECK(nearly(a.nodeTime(clipNode), 0.5f));

        // Another 0.25 → t=0.75 → midway between kf1 and kf2 → x=15.
        r = a.evaluate(EvalContext{0.25f, 0.0f, 1.0f}, buf);
        CHECK(nearly(buf.localPose().joints[0].translation, Vec3{15.0f, 0, 0}));
        CHECK(r.dirty);
    }

    // 3. Output pose matches direct clip sampling via the A2 single-clip
    // path at the same time.
    {
        ClipDesc clip = makeXSweepClip();

        AnimationGraph g;
        GraphNodeId clipNode = g.addClip(&clip);
        GraphNodeId outNode = g.addOutput();
        g.connect(clipNode, outNode);
        g.setOutput(outNode);

        Animator graphAnim;
        graphAnim.setGraph(&g);
        PoseBuffer graphBuf;
        graphAnim.evaluate(EvalContext{0.3f, 0.0f, 1.0f}, graphBuf);

        Animator clipAnim;
        clipAnim.setClip(&clip);
        clipAnim.advance(0.3f);
        PoseBuffer clipBuf(1);
        clipAnim.samplePose(clipBuf.localPose());

        CHECK(nearly(graphBuf.localPose().joints[0].translation,
                     clipBuf.localPose().joints[0].translation));
    }

    // 4. setGraph(nullptr) detaches; evaluate is a no-op and returns
    // clean defaults.
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
        a.evaluate(EvalContext{0.5f, 0.0f, 1.0f}, buf);
        CHECK_EQ(buf.size(), std::size_t{1});

        a.setGraph(nullptr);
        CHECK(a.graph() == nullptr);
        EvalResult r = a.evaluate(EvalContext{0.5f, 0.0f, 1.0f}, buf);
        CHECK(!r.dirty);
        CHECK_EQ(r.firedEvents.size(), std::size_t{0});
    }

    // 5. Mode switch: setGraph then setClip then setGraph again resets
    // per-node playhead state.
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
        a.evaluate(EvalContext{0.4f, 0.0f, 1.0f}, buf);
        CHECK(nearly(a.nodeTime(clipNode), 0.4f));

        // Switch to single-clip mode: graph detaches, per-node times wiped.
        a.setClip(&clip);
        CHECK(a.graph() == nullptr);
        CHECK(nearly(a.nodeTime(clipNode), 0.0f));

        // Switch back: node time starts at 0 again.
        a.setGraph(&g);
        CHECK(a.clip() == nullptr);
        CHECK(nearly(a.nodeTime(clipNode), 0.0f));
        a.evaluate(EvalContext{0.4f, 0.0f, 1.0f}, buf);
        CHECK(nearly(a.nodeTime(clipNode), 0.4f));
    }

    EXIT_WITH_RESULT();
}
