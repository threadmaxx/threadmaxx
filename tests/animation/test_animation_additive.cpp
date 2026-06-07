#include "Check.hpp"
#include "threadmaxx_animation/clip.hpp"
#include "threadmaxx_animation/eval.hpp"
#include "threadmaxx_animation/graph.hpp"
#include "threadmaxx_animation/pose.hpp"

using namespace threadmaxx::animation;

namespace {

constexpr bool nearly(float a, float b, float eps = 1e-4f) noexcept {
    const float d = a - b;
    return (d < eps) && (d > -eps);
}

constexpr bool nearly(const Vec3& a, const Vec3& b, float eps = 1e-4f) noexcept {
    return nearly(a.x, b.x, eps) && nearly(a.y, b.y, eps) && nearly(a.z, b.z, eps);
}

constexpr bool nearly(const Quat& a, const Quat& b, float eps = 1e-4f) noexcept {
    return nearly(a.x, b.x, eps) && nearly(a.y, b.y, eps) &&
           nearly(a.z, b.z, eps) && nearly(a.w, b.w, eps);
}

ClipDesc makeConstClip(const JointPose& p) {
    ClipDesc c;
    c.name = "const";
    c.duration = 1.0f;
    c.looping = false;
    c.jointCount = 1;
    c.keyframeTimes = {0.0f};
    c.keyframes.resize(1);
    c.keyframes[0] = p;
    return c;
}

} // namespace

int main() {
    // Base pose: translation (0,0,0), identity rotation, scale (1,1,1).
    JointPose basePose;
    basePose.translation = {0.0f, 0.0f, 0.0f};
    basePose.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    basePose.scale = {1.0f, 1.0f, 1.0f};

    // Delta pose: extra translation (10,0,0), identity rotation, identity scale.
    JointPose deltaPose;
    deltaPose.translation = {10.0f, 0.0f, 0.0f};
    deltaPose.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    deltaPose.scale = {1.0f, 1.0f, 1.0f};

    ClipDesc baseClip = makeConstClip(basePose);
    ClipDesc deltaClip = makeConstClip(deltaPose);

    AnimationGraph g;
    GraphNodeId bn = g.addClip(&baseClip);
    GraphNodeId dn = g.addClip(&deltaClip);
    GraphNodeId add = g.addAdditive(1.0f);
    g.connect(bn, add);
    g.connect(dn, add);
    GraphNodeId out = g.addOutput();
    g.connect(add, out);
    g.setOutput(out);

    // 1. weight=1.0 → output = base + delta = (10,0,0).
    {
        Animator a;
        a.setGraph(&g);
        PoseBuffer buf;
        a.evaluate(EvalContext{0.0f, 0.0f, 1.0f}, buf);
        CHECK(nearly(buf.localPose().joints[0].translation, Vec3{10.0f, 0, 0}));
        CHECK(nearly(buf.localPose().joints[0].scale, Vec3{1.0f, 1.0f, 1.0f}));
    }

    // 2. weight=0.5 → output = base + 0.5*delta = (5,0,0).
    {
        Animator a;
        a.setGraph(&g);
        a.setParameter(add, "weight", 0.5f);
        PoseBuffer buf;
        a.evaluate(EvalContext{0.0f, 0.0f, 1.0f}, buf);
        CHECK(nearly(buf.localPose().joints[0].translation, Vec3{5.0f, 0, 0}));
    }

    // 3. weight=0 → output = base (no-op layer).
    {
        Animator a;
        a.setGraph(&g);
        a.setParameter(add, "weight", 0.0f);
        PoseBuffer buf;
        a.evaluate(EvalContext{0.0f, 0.0f, 1.0f}, buf);
        CHECK(nearly(buf.localPose().joints[0].translation, basePose.translation));
        CHECK(nearly(buf.localPose().joints[0].rotation, basePose.rotation));
        CHECK(nearly(buf.localPose().joints[0].scale, basePose.scale));
    }

    // 4. "Removing" the additive layer (rebinding to a Clip→Output graph)
    // returns to base.
    {
        AnimationGraph g2;
        GraphNodeId bn2 = g2.addClip(&baseClip);
        GraphNodeId out2 = g2.addOutput();
        g2.connect(bn2, out2);
        g2.setOutput(out2);

        Animator a;
        a.setGraph(&g2);
        PoseBuffer buf;
        a.evaluate(EvalContext{0.0f, 0.0f, 1.0f}, buf);
        CHECK(nearly(buf.localPose().joints[0].translation, basePose.translation));
        CHECK(nearly(buf.localPose().joints[0].scale, basePose.scale));
    }

    // 5. Additive scale composition: base scale (2,2,2) + delta scale
    // (1.5,1.5,1.5) at weight=1 → output = 2 * 1.5 = (3,3,3).
    {
        JointPose b;
        b.translation = {0.0f, 0.0f, 0.0f};
        b.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
        b.scale = {2.0f, 2.0f, 2.0f};
        JointPose d;
        d.translation = {0.0f, 0.0f, 0.0f};
        d.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
        d.scale = {1.5f, 1.5f, 1.5f};
        ClipDesc bClip = makeConstClip(b);
        ClipDesc dClip = makeConstClip(d);

        AnimationGraph g3;
        GraphNodeId bn3 = g3.addClip(&bClip);
        GraphNodeId dn3 = g3.addClip(&dClip);
        GraphNodeId add3 = g3.addAdditive(1.0f);
        g3.connect(bn3, add3);
        g3.connect(dn3, add3);
        GraphNodeId out3 = g3.addOutput();
        g3.connect(add3, out3);
        g3.setOutput(out3);

        Animator a;
        a.setGraph(&g3);
        PoseBuffer buf;
        a.evaluate(EvalContext{0.0f, 0.0f, 1.0f}, buf);
        CHECK(nearly(buf.localPose().joints[0].scale, Vec3{3.0f, 3.0f, 3.0f}));
    }

    EXIT_WITH_RESULT();
}
