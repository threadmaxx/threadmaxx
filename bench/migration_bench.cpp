// §3.9.1 batch 16 — Per-archetype-pair migration benchmark.
//
// Drives a controlled add/remove-Health workload that forces a known
// number of entities to migrate between two archetypes each tick.
// Reports the per-tick cost as ns / (entity migration) — the metric
// §3.9.4 batch 19 (migration batching by archetype pair) must
// improve.
//
// Two sweeps:
//   - density   — fixed scene size, vary the rows migrated per tick
//                 (1 → all)
//   - scene     — fixed migration density (50%), vary the scene size
//
// On the bench's standard 4-worker engine the migrations all flow
// through the single-threaded commit phase today; the bench is
// path-agnostic, so post-batch-19 we can rerun and compare.

#include "common.hpp"
#include "scene_workloads.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <cstdio>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace {

using namespace threadmaxx;
using namespace threadmaxx_bench;

class HealthFlipSystem : public ISystem {
public:
    HealthFlipSystem(std::vector<EntityHandle> targets)
        : targets_(std::move(targets)) {}
    const char* name() const noexcept override { return "healthFlip"; }
    ComponentSet reads()  const noexcept override { return ComponentSet::none(); }
    ComponentSet writes() const noexcept override { return ComponentSet::all(); }
    void update(SystemContext& ctx) override {
        const bool add = (ctx.tick() & 1) == 0;
        const auto n = static_cast<std::uint32_t>(targets_.size());
        ctx.parallelFor(n, 256,
            [this, add](Range r, CommandBuffer& cb) {
                for (auto i = r.begin; i < r.end; ++i) {
                    if (add) cb.setHealth(targets_[i], Health{10.0f, 10.0f});
                    else     cb.removeTag(targets_[i], Component::Health);
                }
            });
    }
    std::size_t migrationsPerTick() const { return targets_.size(); }
private:
    std::vector<EntityHandle> targets_;
};

struct MigrationSeedGame : IGame {
    std::uint32_t count = 0;
    void onSetup(Engine&, World&, CommandBuffer& cb) override {
        for (std::uint32_t i = 0; i < count; ++i) {
            Transform tr{};
            tr.position.x = static_cast<float>(i);
            cb.spawn(tr);
        }
    }
};

LatencyHistogram runOne(std::uint32_t entityCount,
                        std::uint32_t migrationsPerTick,
                        int warmup, int iters,
                        std::uint32_t batchThreshold = 16u) {
    Config cfg = benchConfig(/*workers=*/4, entityCount);
    cfg.batchMigrateThreshold = batchThreshold;
    Engine engine(cfg);
    MigrationSeedGame seed; seed.count = entityCount;
    if (!engine.initialize(seed)) {
        std::printf("  init failed\n");
        return {};
    }

    std::vector<EntityHandle> targets;
    {
        const auto all = engine.world().entities();
        const std::size_t take = std::min<std::size_t>(
            migrationsPerTick, all.size());
        targets.assign(all.begin(), all.begin() + take);
    }
    engine.registerSystem(std::make_unique<HealthFlipSystem>(std::move(targets)));

    LatencyHistogram h;
    runIters(h, warmup, iters, [&] { engine.step(); });
    engine.shutdown();
    return h;
}

void emit(CsvWriter& csv, const char* sweep, std::size_t entities,
          std::size_t migPerTick, const LatencyHistogram& h) {
    BenchRow r;
    r.label    = "healthFlip";
    r.workload = sweep;
    r.entities = entities;
    r.workers  = 4;
    r.mean_ns  = h.meanNs();
    r.stddev   = h.stddev();
    r.p50_ns   = h.p50Ns();
    r.p95_ns   = h.p95Ns();
    r.p99_ns   = h.p99Ns();
    if (h.meanNs() > 0.0 && migPerTick > 0) {
        r.throughput =
            static_cast<double>(migPerTick) / (h.meanNs() / 1e9);
    }
    char buf[96];
    std::snprintf(buf, sizeof(buf), "mig_per_tick=%zu ns_per_mig=%.2f",
                  migPerTick,
                  migPerTick > 0
                      ? h.meanNs() / static_cast<double>(migPerTick)
                      : 0.0);
    r.note = buf;
    csv.row(r);
}

} // namespace

int main(int argc, char** argv) {
    const char* csvPath = (argc >= 2) ? argv[1] : nullptr;
    CsvWriter csv(csvPath);

    constexpr int kWarmup = 16;
    constexpr int kIters  = 128;

    // Density sweep — fix scene at 32k, vary migration count.
    {
        const std::uint32_t N = 32'000;
        for (std::uint32_t m : {16u, 256u, 1024u, 4096u, 16'000u, 32'000u}) {
            const auto h = runOne(N, m, kWarmup, kIters);
            emit(csv, "density-32k", N, m, h);
        }
    }
    // Scene sweep — fix migration density at 50%, vary N.
    {
        for (std::uint32_t N : {1'024u, 8'192u, 32'768u, 100'000u}) {
            const auto h = runOne(N, N / 2, kWarmup, kIters);
            emit(csv, "scene-50pct", N, N / 2, h);
        }
    }
    // SHARDED_OPTIMISATION.md S6 — A/B sweep at N ∈ {1, 8, 64, 512}
    // comparing the batch path (default threshold = 16) against the
    // per-cmd path (threshold = UINT32_MAX). Measures ONLY the commit
    // phase wall-clock (`EngineStats::commitDurationSeconds`) — the
    // per-step total is dominated by parallelFor setup and would
    // bury sub-millisecond migration savings under host noise.
    //
    // The gate is ≥30% reduction at N ≥ 64 (the batch threshold is
    // 16; runs above it take the batched path). N=1 and N=8 are
    // baseline controls — both are below the threshold so they take
    // the per-cmd path under either config and MUST stay flat.
    {
        std::printf("\n# S6 batch-vs-per-cmd sweep — commit phase only\n");
        std::printf("# %-6s %-14s %-14s %-10s\n",
                    "N", "commit_batch_ns", "commit_perCmd_ns", "delta");
        constexpr std::uint32_t kSceneN = 32'000;
        constexpr std::uint32_t kNoBatch = std::numeric_limits<std::uint32_t>::max();
        constexpr int kCommitWarmup = 32;
        constexpr int kCommitIters  = 512;
        for (std::uint32_t m : {1u, 8u, 64u, 512u, 4096u, 32'000u}) {
            auto runWithThreshold = [&](std::uint32_t thresh) -> double {
                Config cfg = benchConfig(/*workers=*/4, kSceneN);
                cfg.batchMigrateThreshold = thresh;
                Engine engine(cfg);
                MigrationSeedGame seed; seed.count = kSceneN;
                engine.initialize(seed);
                std::vector<EntityHandle> targets;
                const auto all = engine.world().entities();
                const std::size_t take = std::min<std::size_t>(m, all.size());
                targets.assign(all.begin(), all.begin() + take);
                engine.registerSystem(std::make_unique<HealthFlipSystem>(
                    std::move(targets)));
                for (int i = 0; i < kCommitWarmup; ++i) engine.step();
                double accumNs = 0.0;
                for (int i = 0; i < kCommitIters; ++i) {
                    engine.step();
                    accumNs += engine.stats().commitDurationSeconds * 1e9;
                }
                engine.shutdown();
                return accumNs / kCommitIters;
            };
            const double batchNs  = runWithThreshold(16u);
            const double perCmdNs = runWithThreshold(kNoBatch);
            const double batchPerMig  = m > 0 ? batchNs  / m : 0.0;
            const double perCmdPerMig = m > 0 ? perCmdNs / m : 0.0;
            const double deltaPct = perCmdNs > 0.0
                ? (batchNs - perCmdNs) / perCmdNs * 100.0
                : 0.0;
            std::printf("# %-6u %-14.0f %-14.0f %+.2f%%\n",
                        m, batchNs, perCmdNs, deltaPct);
            char buf[160];
            std::snprintf(buf, sizeof(buf),
                          "mig_per_tick=%u commit_batch_ns=%.0f commit_percmd_ns=%.0f"
                          " ns_per_mig_batch=%.2f ns_per_mig_percmd=%.2f delta=%+.2f%%",
                          m, batchNs, perCmdNs, batchPerMig, perCmdPerMig, deltaPct);
            BenchRow r;
            r.label    = "healthFlip";
            r.workload = "s6-batch-vs-percmd";
            r.entities = kSceneN;
            r.workers  = 4;
            r.mean_ns  = batchNs;
            r.note     = buf;
            csv.row(r);
        }
    }
    return 0;
}
