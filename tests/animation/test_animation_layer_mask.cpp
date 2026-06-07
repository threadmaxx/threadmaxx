#include "Check.hpp"
#include "threadmaxx_animation/clip.hpp"
#include "threadmaxx_animation/eval.hpp"
#include "threadmaxx_animation/graph.hpp"
#include "threadmaxx_animation/pose.hpp"

#include <array>
#include <cstdint>

using namespace threadmaxx::animation;

namespace {

constexpr bool nearly(float a, float b, float eps = 1e-4f) noexcept {
    const float d = a - b;
    return (d < eps) && (d > -eps);
}

constexpr bool nearly(const Vec3& a, const Vec3& b, float eps = 1e-4f) noexcept {
    return nearly(a.x, b.x, eps) && nearly(a.y, b.y, eps) && nearly(a.z, b.z, eps);
}

// 4-joint clip: every joint at the same translation.
ClipDesc makeUniformClip(float tx) {
    ClipDesc c;
    c.name = "uniform";
    c.duration = 1.0f;
    c.looping = false;
    c.jointCount = 4;
    c.keyframeTimes = {0.0f};
    c.keyframes.resize(4);
    for (int j = 0; j < 4; ++j) {
        c.keyframes[j].translation = {tx, 0.0f, 0.0f};
        c.keyframes[j].rotation = {0.0f, 0.0f, 0.0f, 1.0f};
        c.keyframes[j].scale = {1.0f, 1.0f, 1.0f};
    }
    return c;
}

} // namespace

int main() {
    // Base: every joint at x=0. Overlay: every joint at x=10. Mask
    // selects only joints 1 and 3. At weight=1, joints 1+3 follow
    // overlay; joints 0+2 stay at base.
    ClipDesc base = makeUniformClip(0.0f);
    ClipDesc overlay = makeUniformClip(10.0f);

    AnimationGraph g;
    GraphNodeId bn = g.addClip(&base);
    GraphNodeId on = g.addClip(&overlay);
    GraphNodeId layer = g.addLayer(1.0f);
    g.connect(bn, layer);
    g.connect(on, layer);
    std::array<std::uint8_t, 4> mask{0, 1, 0, 1};
    g.setLayerMask(layer, std::span<const std::uint8_t>(mask));

    GraphNodeId out = g.addOutput();
    g.connect(layer, out);
    g.setOutput(out);

    // 1. weight=1 → masked joints follow overlay; unmasked stay at base.
    {
        Animator a;
        a.setGraph(&g);
        PoseBuffer buf;
        a.evaluate(EvalContext{0.0f, 0.0f, 1.0f}, buf);
        const auto& js = buf.localPose().joints;
        CHECK_EQ(buf.size(), std::size_t{4});
        CHECK(nearly(js[0].translation, Vec3{0.0f, 0, 0}));   // unmasked
        CHECK(nearly(js[1].translation, Vec3{10.0f, 0, 0}));  // masked
        CHECK(nearly(js[2].translation, Vec3{0.0f, 0, 0}));   // unmasked
        CHECK(nearly(js[3].translation, Vec3{10.0f, 0, 0}));  // masked
    }

    // 2. weight=0.5 → masked joints lerp halfway; unmasked stay at base.
    {
        Animator a;
        a.setGraph(&g);
        a.setParameter(layer, "weight", 0.5f);
        PoseBuffer buf;
        a.evaluate(EvalContext{0.0f, 0.0f, 1.0f}, buf);
        const auto& js = buf.localPose().joints;
        CHECK(nearly(js[0].translation, Vec3{0.0f, 0, 0}));
        CHECK(nearly(js[1].translation, Vec3{5.0f, 0, 0}));
        CHECK(nearly(js[2].translation, Vec3{0.0f, 0, 0}));
        CHECK(nearly(js[3].translation, Vec3{5.0f, 0, 0}));
    }

    // 3. weight=0 → all joints stay at base (Layer is a no-op).
    {
        Animator a;
        a.setGraph(&g);
        a.setParameter(layer, "weight", 0.0f);
        PoseBuffer buf;
        a.evaluate(EvalContext{0.0f, 0.0f, 1.0f}, buf);
        const auto& js = buf.localPose().joints;
        for (int j = 0; j < 4; ++j) {
            CHECK(nearly(js[j].translation, Vec3{0.0f, 0, 0}));
        }
    }

    // 4. All-ones mask → every joint follows overlay.
    {
        AnimationGraph g2;
        GraphNodeId b2 = g2.addClip(&base);
        GraphNodeId o2 = g2.addClip(&overlay);
        GraphNodeId l2 = g2.addLayer(1.0f);
        g2.connect(b2, l2);
        g2.connect(o2, l2);
        std::array<std::uint8_t, 4> allOn{1, 1, 1, 1};
        g2.setLayerMask(l2, std::span<const std::uint8_t>(allOn));
        GraphNodeId out2 = g2.addOutput();
        g2.connect(l2, out2);
        g2.setOutput(out2);

        Animator a;
        a.setGraph(&g2);
        PoseBuffer buf;
        a.evaluate(EvalContext{0.0f, 0.0f, 1.0f}, buf);
        const auto& js = buf.localPose().joints;
        for (int j = 0; j < 4; ++j) {
            CHECK(nearly(js[j].translation, Vec3{10.0f, 0, 0}));
        }
    }

    // 5. Empty mask → every joint stays at base (default behavior).
    {
        AnimationGraph g3;
        GraphNodeId b3 = g3.addClip(&base);
        GraphNodeId o3 = g3.addClip(&overlay);
        GraphNodeId l3 = g3.addLayer(1.0f);
        g3.connect(b3, l3);
        g3.connect(o3, l3);
        // No setLayerMask call → mask vector is empty.
        GraphNodeId out3 = g3.addOutput();
        g3.connect(l3, out3);
        g3.setOutput(out3);

        Animator a;
        a.setGraph(&g3);
        PoseBuffer buf;
        a.evaluate(EvalContext{0.0f, 0.0f, 1.0f}, buf);
        const auto& js = buf.localPose().joints;
        for (int j = 0; j < 4; ++j) {
            CHECK(nearly(js[j].translation, Vec3{0.0f, 0, 0}));
        }
    }

    EXIT_WITH_RESULT();
}
