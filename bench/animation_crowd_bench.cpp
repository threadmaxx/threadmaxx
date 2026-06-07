// A7 — crowd-evaluation throughput.
//
// Drives 1k / 8k / 32k agents sharing one AnimationGraph (Clip →
// Output, three joints) through a fixed number of ticks and reports
// wall-clock per agent per tick. Useful for sizing the engine-side
// `forEachChunk` integration; the scalar baseline here is the floor
// any SIMD/parallel variant must beat.
//
// Single-threaded, deliberately — the parallel-dispatch knob is the
// engine integration (`forEachChunk`'s sub-job partitioning); this
// bench measures the per-agent kernel cost so the engine integration
// can size its grain.

#include "threadmaxx_animation/clip.hpp"
#include "threadmaxx_animation/eval.hpp"
#include "threadmaxx_animation/graph.hpp"
#include "threadmaxx_animation/pose.hpp"

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <vector>

using namespace threadmaxx::animation;

namespace {

ClipDesc makeClip() {
    ClipDesc c;
    c.name = "walk";
    c.duration = 1.0f;
    c.looping = true;
    c.jointCount = 3;
    c.keyframeTimes = {0.0f, 0.5f, 1.0f};
    c.keyframes.resize(c.keyframeTimes.size() * c.jointCount);
    for (std::size_t k = 0; k < 3; ++k) {
        for (std::size_t j = 0; j < 3; ++j) {
            auto& p = c.keyframes[k * 3 + j];
            p.translation = {0.1f * static_cast<float>(j),
                             0.2f * static_cast<float>(k),
                             0.0f};
        }
    }
    return c;
}

void runScale(std::size_t agents, std::size_t ticks) {
    ClipDesc clip = makeClip();
    AnimationGraph g;
    GraphNodeId clipNode = g.addClip(&clip);
    GraphNodeId outNode = g.addOutput();
    g.connect(clipNode, outNode);
    g.setOutput(outNode);

    std::vector<Animator> animators(agents);
    std::vector<PoseBuffer> poses(agents);
    for (auto& a : animators) a.setGraph(&g);

    // Warm-up: first tick pays the per-Animator PoseBuffer allocation
    // + scratch-pool warm-up. We exclude it from the timed window so
    // the reported number is steady-state.
    EvalContext ctx{1.0f / 60.0f, 0.0f, 1.0f};
    evaluateBatch(std::span<Animator>(animators),
                  std::span<PoseBuffer>(poses),
                  ctx);

    using clock = std::chrono::steady_clock;
    const auto t0 = clock::now();
    for (std::size_t t = 0; t < ticks; ++t) {
        evaluateBatch(std::span<Animator>(animators),
                      std::span<PoseBuffer>(poses),
                      ctx);
        ctx.time += ctx.dt;
    }
    const auto t1 = clock::now();
    const double seconds =
        std::chrono::duration<double>(t1 - t0).count();
    const double perTick = seconds / static_cast<double>(ticks);
    const double perAgent =
        perTick / static_cast<double>(agents) * 1e6;  // µs/agent
    std::printf(
        "[crowd] agents=%zu ticks=%zu wall=%.3fms/tick agent=%.3fµs\n",
        agents, ticks, perTick * 1e3, perAgent);
}

} // namespace

int main() {
    std::printf("threadmaxx_animation crowd bench — A7\n");
    runScale(1024, 240);
    runScale(8192, 60);
    runScale(32768, 30);
    return 0;
}
