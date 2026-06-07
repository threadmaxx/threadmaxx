#include "Check.hpp"
#include "threadmaxx_animation/clip.hpp"
#include "threadmaxx_animation/eval.hpp"
#include "threadmaxx_animation/graph.hpp"
#include "threadmaxx_animation/pose.hpp"

#include <cstdint>
#include <vector>

using namespace threadmaxx::animation;

namespace {

constexpr bool nearly(float a, float b, float eps = 1e-5f) noexcept {
    const float d = a - b;
    return (d < eps) && (d > -eps);
}

constexpr bool nearly(const Vec3& a, const Vec3& b, float eps = 1e-5f) noexcept {
    return nearly(a.x, b.x, eps) && nearly(a.y, b.y, eps) && nearly(a.z, b.z, eps);
}

ClipDesc makeXSweep() {
    ClipDesc c;
    c.name = "x_sweep";
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
    constexpr std::size_t kAgents = 64;
    constexpr float kDt = 1.0f / 60.0f;

    ClipDesc clip = makeXSweep();
    AnimationGraph g;
    GraphNodeId clipNode = g.addClip(&clip);
    GraphNodeId outNode = g.addOutput();
    g.connect(clipNode, outNode);
    g.setOutput(outNode);

    // Even indices are LOD=0 (every tick), odd indices LOD=1 (skip every
    // other tick). Skip mask is flipped each tick so LOD=1 agents
    // evaluate on alternating ticks only.
    std::vector<Animator> animators(kAgents);
    std::vector<PoseBuffer> poses(kAgents);
    for (auto& a : animators) a.setGraph(&g);

    // === 1. Skip mask honored: at tick 0 with everyone skipped, no
    // agent should have its pose buffer sized — evaluate is a true
    // no-op for masked agents.
    {
        std::vector<std::uint8_t> skipAll(kAgents, 1);
        EvalContext ctx{kDt, 0.0f, 1.0f};
        evaluateBatch(std::span<Animator>(animators),
                      std::span<PoseBuffer>(poses),
                      ctx,
                      {},
                      std::span<const std::uint8_t>(skipAll));
        for (std::size_t i = 0; i < kAgents; ++i) {
            CHECK_EQ(poses[i].size(), std::size_t{0});
            CHECK(nearly(animators[i].nodeTime(clipNode), 0.0f));
        }
    }

    // === 2. Partial mask: even = run, odd = skip. Verify even agents'
    // playheads advanced; odd agents' did not.
    {
        std::vector<std::uint8_t> skipOdd(kAgents);
        for (std::size_t i = 0; i < kAgents; ++i) {
            skipOdd[i] = (i % 2 == 0) ? std::uint8_t{0} : std::uint8_t{1};
        }
        EvalContext ctx{kDt, 0.0f, 1.0f};
        evaluateBatch(std::span<Animator>(animators),
                      std::span<PoseBuffer>(poses),
                      ctx,
                      {},
                      std::span<const std::uint8_t>(skipOdd));

        for (std::size_t i = 0; i < kAgents; ++i) {
            if (i % 2 == 0) {
                // Even — evaluated.
                CHECK(nearly(animators[i].nodeTime(clipNode), kDt));
                CHECK_EQ(poses[i].size(), std::size_t{1});
                // 1/60 of full 2s clip; at t≈0.01667 the x value is
                // ~0.1667 (linear interp between kf0 and kf1).
                CHECK(nearly(poses[i].localPose().joints[0].translation.x,
                             10.0f * kDt));
            } else {
                // Odd — skipped.
                CHECK(nearly(animators[i].nodeTime(clipNode), 0.0f));
                CHECK_EQ(poses[i].size(), std::size_t{0});
            }
        }
    }

    // === 3. 30 Hz half-rate sim: drive 4 ticks of dt = 1/60s; on odd
    // ticks mask every agent's LOD-bit (skip), on even ticks run all.
    // After 4 ticks all agents should have advanced by exactly 2*dt.
    {
        std::vector<Animator> a2(kAgents);
        std::vector<PoseBuffer> p2(kAgents);
        for (auto& a : a2) a.setGraph(&g);

        std::vector<std::uint8_t> skipAll(kAgents, 1);
        std::vector<std::uint8_t> skipNone(kAgents, 0);

        EvalContext ctx{kDt, 0.0f, 1.0f};
        for (std::size_t t = 0; t < 4; ++t) {
            const auto& mask = (t % 2 == 0) ? skipNone : skipAll;
            evaluateBatch(std::span<Animator>(a2),
                          std::span<PoseBuffer>(p2),
                          ctx,
                          {},
                          std::span<const std::uint8_t>(mask));
            ctx.time += kDt;
        }
        for (std::size_t i = 0; i < kAgents; ++i) {
            // Two non-skipped evaluates at dt each → total 2*dt.
            CHECK(nearly(a2[i].nodeTime(clipNode), 2.0f * kDt));
        }
    }

    // === 4. Skip mask preserves the previous pose. After a normal
    // evaluate, a follow-up skipped call must leave the pose buffer
    // contents untouched.
    {
        std::vector<Animator> a3(kAgents);
        std::vector<PoseBuffer> p3(kAgents);
        for (auto& a : a3) a.setGraph(&g);

        EvalContext ctx{kDt, 0.0f, 1.0f};
        // Normal evaluate first to populate poses.
        evaluateBatch(std::span<Animator>(a3),
                      std::span<PoseBuffer>(p3),
                      ctx);
        std::vector<Vec3> snapshot(kAgents);
        for (std::size_t i = 0; i < kAgents; ++i) {
            snapshot[i] = p3[i].localPose().joints[0].translation;
        }
        // Now skip everyone for 5 ticks; pose buffers should stay
        // byte-identical and node times should not advance.
        std::vector<std::uint8_t> skipAll(kAgents, 1);
        for (std::size_t t = 0; t < 5; ++t) {
            ctx.time += kDt;
            evaluateBatch(std::span<Animator>(a3),
                          std::span<PoseBuffer>(p3),
                          ctx,
                          {},
                          std::span<const std::uint8_t>(skipAll));
        }
        for (std::size_t i = 0; i < kAgents; ++i) {
            CHECK(nearly(p3[i].localPose().joints[0].translation, snapshot[i]));
            CHECK(nearly(a3[i].nodeTime(clipNode), kDt));
        }
    }

    EXIT_WITH_RESULT();
}
