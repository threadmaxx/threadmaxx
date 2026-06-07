#include "Check.hpp"
#include "threadmaxx_animation/clip.hpp"
#include "threadmaxx_animation/eval.hpp"
#include "threadmaxx_animation/graph.hpp"
#include "threadmaxx_animation/pose.hpp"

#include <array>

using namespace threadmaxx::animation;

namespace {

constexpr bool nearly(float a, float b, float eps = 1e-3f) noexcept {
    const float d = a - b;
    return (d < eps) && (d > -eps);
}

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
    // 4-corner unit-square grid: A=(0,0), B=(1,0), C=(0,1), D=(1,1).
    // Translation x: 1, 2, 3, 4. Centroid (0.5, 0.5) → all weights
    // equal → expected x = (1+2+3+4)/4 = 2.5.
    ClipDesc clipA = makeConstClip(1.0f);
    ClipDesc clipB = makeConstClip(2.0f);
    ClipDesc clipC = makeConstClip(3.0f);
    ClipDesc clipD = makeConstClip(4.0f);

    AnimationGraph g;
    GraphNodeId na = g.addClip(&clipA);
    GraphNodeId nb = g.addClip(&clipB);
    GraphNodeId nc = g.addClip(&clipC);
    GraphNodeId nd = g.addClip(&clipD);

    std::array<float, 4> xs{0.0f, 1.0f, 0.0f, 1.0f};
    std::array<float, 4> ys{0.0f, 0.0f, 1.0f, 1.0f};
    GraphNodeId blend = g.addBlend2D(std::span<const float>(xs),
                                     std::span<const float>(ys));
    g.connect(na, blend);
    g.connect(nb, blend);
    g.connect(nc, blend);
    g.connect(nd, blend);

    GraphNodeId out = g.addOutput();
    g.connect(blend, out);
    g.setOutput(out);

    // 1. Centroid query → uniform weights → x = 2.5.
    {
        Animator a;
        a.setGraph(&g);
        PoseBuffer buf;
        a.setParameter(blend, "x", 0.5f);
        a.setParameter(blend, "y", 0.5f);
        a.evaluate(EvalContext{0.0f, 0.0f, 1.0f}, buf);
        CHECK(nearly(buf.localPose().joints[0].translation.x, 2.5f));
    }

    // 2. Query exactly at a sample point collapses to that sample.
    {
        Animator a;
        a.setGraph(&g);
        PoseBuffer buf;

        a.setParameter(blend, "x", 0.0f);
        a.setParameter(blend, "y", 0.0f);
        a.evaluate(EvalContext{0.0f, 0.0f, 1.0f}, buf);
        CHECK(nearly(buf.localPose().joints[0].translation.x, 1.0f));

        a.setParameter(blend, "x", 1.0f);
        a.setParameter(blend, "y", 1.0f);
        a.evaluate(EvalContext{0.0f, 0.0f, 1.0f}, buf);
        CHECK(nearly(buf.localPose().joints[0].translation.x, 4.0f));
    }

    // 3. Off-center query biases toward the closer corners. Query at
    // (0.25, 0.25) is closer to A(0,0) than to D(1,1) → output should be
    // less than the centroid value 2.5 (A's x is smallest).
    {
        Animator a;
        a.setGraph(&g);
        PoseBuffer buf;
        a.setParameter(blend, "x", 0.25f);
        a.setParameter(blend, "y", 0.25f);
        a.evaluate(EvalContext{0.0f, 0.0f, 1.0f}, buf);
        CHECK(buf.localPose().joints[0].translation.x < 2.5f);
        CHECK(buf.localPose().joints[0].translation.x > 1.0f);
    }

    // 4. Graph defaults `"x"` / `"y"` at 0,0 (without Animator overrides)
    // collapse to the (0,0) sample.
    {
        AnimationGraph g2;
        GraphNodeId a0 = g2.addClip(&clipA);
        GraphNodeId b0 = g2.addClip(&clipB);
        std::array<float, 2> x2{0.0f, 1.0f};
        std::array<float, 2> y2{0.0f, 0.0f};
        GraphNodeId bl = g2.addBlend2D(std::span<const float>(x2),
                                       std::span<const float>(y2));
        g2.connect(a0, bl);
        g2.connect(b0, bl);
        GraphNodeId o = g2.addOutput();
        g2.connect(bl, o);
        g2.setOutput(o);

        Animator a;
        a.setGraph(&g2);
        PoseBuffer buf;
        a.evaluate(EvalContext{0.0f, 0.0f, 1.0f}, buf);
        // Defaults (0,0) → snap to A (x=1.0)
        CHECK(nearly(buf.localPose().joints[0].translation.x, 1.0f));
    }

    EXIT_WITH_RESULT();
}
