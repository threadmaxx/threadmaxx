#include "Check.hpp"
#include "threadmaxx_animation/clip.hpp"
#include "threadmaxx_animation/detail/job_batch.hpp"
#include "threadmaxx_animation/eval.hpp"
#include "threadmaxx_animation/graph.hpp"
#include "threadmaxx_animation/pose.hpp"

#include <cstdint>
#include <cstring>
#include <vector>

using namespace threadmaxx::animation;

namespace {

ClipDesc makeWalkClip() {
    ClipDesc c;
    c.name = "walk";
    c.duration = 1.0f;
    c.looping = true;
    c.jointCount = 3;
    c.keyframeTimes = {0.0f, 0.5f, 1.0f};
    c.keyframes.resize(c.keyframeTimes.size() * c.jointCount);

    // Three keyframes × three joints; values picked so blend-mid lands
    // on distinct floats (catches "all zeros" false-positives).
    c.keyframes[0].translation = {0.0f, 0.0f, 0.0f};
    c.keyframes[1].translation = {0.0f, 1.0f, 0.0f};
    c.keyframes[2].translation = {0.0f, 0.0f, 0.0f};

    c.keyframes[3].translation = {1.0f, 0.0f, 0.0f};
    c.keyframes[4].translation = {2.0f, 1.5f, 0.0f};
    c.keyframes[5].translation = {1.0f, 0.0f, 0.0f};

    c.keyframes[6].translation = {0.0f, 2.0f, 0.0f};
    c.keyframes[7].translation = {0.5f, 3.0f, 0.0f};
    c.keyframes[8].translation = {0.0f, 2.0f, 0.0f};
    return c;
}

// Hash a flat span of joint poses as a byte buffer. FNV-1a-64; we only
// care that two byte-identical sequences hash the same.
std::uint64_t hashPoses(std::span<const JointPose> joints) noexcept {
    std::uint64_t h = 14695981039346656037ull;
    const auto* bytes = reinterpret_cast<const unsigned char*>(joints.data());
    const std::size_t n = joints.size() * sizeof(JointPose);
    for (std::size_t i = 0; i < n; ++i) {
        h ^= bytes[i];
        h *= 1099511628211ull;
    }
    return h;
}

struct CrowdRun {
    std::vector<Animator> animators;
    std::vector<PoseBuffer> poses;
    std::vector<EvalResult> results;
};

CrowdRun makeCrowd(const AnimationGraph& g, std::size_t n) {
    CrowdRun run;
    run.animators.resize(n);
    run.poses.resize(n);
    run.results.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        run.animators[i].setGraph(&g);
        // First evaluate ensures the per-Animator pose buffer is sized
        // to the graph joint count; the second-run determinism check
        // depends on every per-tick allocation happening on tick 0.
    }
    return run;
}

} // namespace

int main() {
    constexpr std::size_t kAgents = 256;
    constexpr std::size_t kTicks = 8;
    constexpr float kDt = 1.0f / 60.0f;

    ClipDesc clip = makeWalkClip();
    AnimationGraph g;
    GraphNodeId clipNode = g.addClip(&clip);
    GraphNodeId outNode = g.addOutput();
    g.connect(clipNode, outNode);
    g.setOutput(outNode);
    CHECK_EQ(g.jointCount(), std::uint32_t{3});

    // === 1. Drive 256 NPCs through 8 ticks twice and hash every
    // resulting pose; the two runs must be byte-identical. This is the
    // crowd-determinism contract: same graph + same per-tick dt
    // sequence = same per-agent pose every time, regardless of order.
    auto driveOnce = [&](std::vector<std::uint64_t>& hashesOut) {
        CrowdRun run = makeCrowd(g, kAgents);
        hashesOut.clear();
        hashesOut.reserve(kAgents * kTicks);
        EvalContext ctx{kDt, 0.0f, 1.0f};
        for (std::size_t t = 0; t < kTicks; ++t) {
            evaluateBatch(std::span<Animator>(run.animators),
                          std::span<PoseBuffer>(run.poses),
                          ctx,
                          std::span<EvalResult>(run.results));
            for (std::size_t i = 0; i < kAgents; ++i) {
                hashesOut.push_back(hashPoses(run.poses[i].localPose().joints));
            }
            ctx.time += kDt;
        }
    };
    std::vector<std::uint64_t> runA, runB;
    driveOnce(runA);
    driveOnce(runB);
    CHECK_EQ(runA.size(), kAgents * kTicks);
    CHECK_EQ(runA.size(), runB.size());
    for (std::size_t k = 0; k < runA.size(); ++k) {
        CHECK_EQ(runA[k], runB[k]);
    }

    // === 2. After warm-up, the per-agent PoseBuffer sized to the graph
    // joint count (3); first evaluate auto-resizes.
    {
        CrowdRun run = makeCrowd(g, kAgents);
        EvalContext ctx{kDt, 0.0f, 1.0f};
        evaluateBatch(std::span<Animator>(run.animators),
                      std::span<PoseBuffer>(run.poses),
                      ctx);
        for (std::size_t i = 0; i < kAgents; ++i) {
            CHECK_EQ(run.poses[i].size(), std::size_t{3});
        }
    }

    // === 3. Slice-range dispatch: split 256 agents into 4 chunks of 64,
    // call evaluateBatchRange on each, compare against a single
    // whole-slice evaluateBatch. The two must produce byte-identical
    // poses — the contract that makes worker partitioning safe.
    {
        CrowdRun whole = makeCrowd(g, kAgents);
        CrowdRun parts = makeCrowd(g, kAgents);
        EvalContext ctx{kDt, 0.0f, 1.0f};
        for (std::size_t t = 0; t < kTicks; ++t) {
            evaluateBatch(std::span<Animator>(whole.animators),
                          std::span<PoseBuffer>(whole.poses),
                          ctx);
            for (std::size_t shard = 0; shard < 4; ++shard) {
                const std::size_t lo = shard * 64;
                const std::size_t hi = lo + 64;
                detail::evaluateBatchRange(std::span<Animator>(parts.animators),
                                           std::span<PoseBuffer>(parts.poses),
                                           lo, hi, ctx);
            }
            for (std::size_t i = 0; i < kAgents; ++i) {
                const auto a = whole.poses[i].localPose().joints;
                const auto b = parts.poses[i].localPose().joints;
                CHECK_EQ(hashPoses(a), hashPoses(b));
            }
            ctx.time += kDt;
        }
    }

    // === 4. Empty slice is a no-op (covers the boundary case for
    // forEachChunk callers with an empty chunk range).
    {
        std::vector<Animator> empty;
        std::vector<PoseBuffer> emptyPoses;
        EvalContext ctx{kDt, 0.0f, 1.0f};
        evaluateBatch(std::span<Animator>(empty),
                      std::span<PoseBuffer>(emptyPoses),
                      ctx);
        CHECK_EQ(empty.size(), std::size_t{0});
    }

    // === 5. outResults: when supplied, populated per-agent. When empty
    // span, the per-agent EvalResult is discarded.
    {
        CrowdRun run = makeCrowd(g, kAgents);
        EvalContext ctx{kDt, 0.0f, 1.0f};

        // With outResults — first tick should mark every agent dirty.
        evaluateBatch(std::span<Animator>(run.animators),
                      std::span<PoseBuffer>(run.poses),
                      ctx,
                      std::span<EvalResult>(run.results));
        for (std::size_t i = 0; i < kAgents; ++i) {
            CHECK(run.results[i].dirty);
        }

        // Without outResults — pre-set sentinel values must stay
        // untouched.
        for (std::size_t i = 0; i < kAgents; ++i) {
            run.results[i].dirty = false;
        }
        evaluateBatch(std::span<Animator>(run.animators),
                      std::span<PoseBuffer>(run.poses),
                      ctx);
        for (std::size_t i = 0; i < kAgents; ++i) {
            CHECK(!run.results[i].dirty);
        }
    }

    EXIT_WITH_RESULT();
}
