#include "Check.hpp"
#include "threadmaxx_animation/clip.hpp"
#include "threadmaxx_animation/eval.hpp"
#include "threadmaxx_animation/graph.hpp"
#include "threadmaxx_animation/pose.hpp"

#include <array>

using namespace threadmaxx::animation;

namespace {

constexpr bool nearly(float a, float b, float eps = 1e-4f) noexcept {
    const float d = a - b;
    return (d < eps) && (d > -eps);
}

// One-joint clip with a single keyframe (constant pose). Translation
// x is the only varying field — perfect for inspecting weighted blends.
ClipDesc makeConstClip(float tx) {
    ClipDesc c;
    c.name = "const";
    c.duration = 1.0f;
    c.looping = false;
    c.jointCount = 1;
    c.keyframeTimes = {0.0f};
    c.keyframes.resize(1);
    c.keyframes[0].translation = {tx, 0.0f, 0.0f};
    return c;
}

} // namespace

int main() {
    // Three input clips at thresholds 0.0 / 0.5 / 1.0 with distinct x
    // translations so weighted blends produce a known scalar.
    ClipDesc clipA = makeConstClip(0.0f);   // at threshold 0.0
    ClipDesc clipB = makeConstClip(10.0f);  // at threshold 0.5
    ClipDesc clipC = makeConstClip(20.0f);  // at threshold 1.0

    AnimationGraph g;
    GraphNodeId na = g.addClip(&clipA);
    GraphNodeId nb = g.addClip(&clipB);
    GraphNodeId nc = g.addClip(&clipC);

    std::array<float, 3> thresholds{0.0f, 0.5f, 1.0f};
    GraphNodeId blend = g.addBlend1D(std::span<const float>(thresholds));
    g.connect(na, blend);
    g.connect(nb, blend);
    g.connect(nc, blend);

    GraphNodeId out = g.addOutput();
    g.connect(blend, out);
    g.setOutput(out);

    // 1. Query at thresholds exactly produces the input clip's pose.
    {
        Animator a;
        a.setGraph(&g);
        PoseBuffer buf;

        a.setParameter(blend, "param", 0.0f);
        a.evaluate(EvalContext{0.0f, 0.0f, 1.0f}, buf);
        CHECK(nearly(buf.localPose().joints[0].translation.x, 0.0f));

        a.setParameter(blend, "param", 0.5f);
        a.evaluate(EvalContext{0.0f, 0.0f, 1.0f}, buf);
        CHECK(nearly(buf.localPose().joints[0].translation.x, 10.0f));

        a.setParameter(blend, "param", 1.0f);
        a.evaluate(EvalContext{0.0f, 0.0f, 1.0f}, buf);
        CHECK(nearly(buf.localPose().joints[0].translation.x, 20.0f));
    }

    // 2. Intermediate values produce the linear-weighted blend.
    {
        Animator a;
        a.setGraph(&g);
        PoseBuffer buf;

        // param=0.25: between 0.0 and 0.5 → alpha = 0.5 → 0.5*A + 0.5*B = 5.0
        a.setParameter(blend, "param", 0.25f);
        a.evaluate(EvalContext{0.0f, 0.0f, 1.0f}, buf);
        CHECK(nearly(buf.localPose().joints[0].translation.x, 5.0f));

        // param=0.75: between 0.5 and 1.0 → alpha = 0.5 → 0.5*B + 0.5*C = 15.0
        a.setParameter(blend, "param", 0.75f);
        a.evaluate(EvalContext{0.0f, 0.0f, 1.0f}, buf);
        CHECK(nearly(buf.localPose().joints[0].translation.x, 15.0f));

        // param=0.1: between 0.0 and 0.5 → alpha = 0.2 → 0.8*0 + 0.2*10 = 2.0
        a.setParameter(blend, "param", 0.1f);
        a.evaluate(EvalContext{0.0f, 0.0f, 1.0f}, buf);
        CHECK(nearly(buf.localPose().joints[0].translation.x, 2.0f));
    }

    // 3. Out-of-range values clamp to the nearest endpoint.
    {
        Animator a;
        a.setGraph(&g);
        PoseBuffer buf;

        a.setParameter(blend, "param", -1.0f);
        a.evaluate(EvalContext{0.0f, 0.0f, 1.0f}, buf);
        CHECK(nearly(buf.localPose().joints[0].translation.x, 0.0f));

        a.setParameter(blend, "param", 5.0f);
        a.evaluate(EvalContext{0.0f, 0.0f, 1.0f}, buf);
        CHECK(nearly(buf.localPose().joints[0].translation.x, 20.0f));
    }

    // 4. Graph-default param value works without an Animator override.
    {
        AnimationGraph g2;
        GraphNodeId a0 = g2.addClip(&clipA);
        GraphNodeId b0 = g2.addClip(&clipB);
        std::array<float, 2> th2{0.0f, 1.0f};
        GraphNodeId bl = g2.addBlend1D(std::span<const float>(th2));
        g2.connect(a0, bl);
        g2.connect(b0, bl);
        GraphNodeId o = g2.addOutput();
        g2.connect(bl, o);
        g2.setOutput(o);
        g2.setParameter(bl, "param", 0.5f);  // graph default

        Animator a;
        a.setGraph(&g2);
        PoseBuffer buf;
        a.evaluate(EvalContext{0.0f, 0.0f, 1.0f}, buf);
        CHECK(nearly(buf.localPose().joints[0].translation.x, 5.0f));
    }

    EXIT_WITH_RESULT();
}
