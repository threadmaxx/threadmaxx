// §3.9.1 batch 16 — preferredGrain sweep.
//
// Existing `job_stealing_bench.cpp` (batch 15b) sweeps grain by hand
// inside one workload. This bench sweeps `ISystem::preferredGrain` —
// the public hint the engine picks up when a `parallelFor` call
// passes `grain=0` — across the AI-only and Render+AI canonical
// scenes, emitting one CSV row per (workload, grain) pair so the
// downstream tuning step in §3.9.2 batch 17 has a per-scene
// recommendation.
//
// Reports per-tick wall-clock with full percentile breakdown, plus
// the stealing ratio observed during the measurement window.

#include "common.hpp"
#include "scene_workloads.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <cstdio>
#include <memory>
#include <string>
#include <utility>

namespace {

using namespace threadmaxx;
using namespace threadmaxx_bench;

class GrainAwareSystem : public ISystem {
public:
    explicit GrainAwareSystem(std::uint32_t grain) : grain_(grain) {}
    const char* name() const noexcept override { return "grainAware"; }
    ComponentSet reads()  const noexcept override {
        return ComponentSet{Component::Transform} |
               ComponentSet{Component::Velocity};
    }
    ComponentSet writes() const noexcept override {
        return ComponentSet{Component::Transform};
    }
    std::uint32_t preferredGrain() const noexcept override { return grain_; }
    void update(SystemContext& ctx) override {
        // Pass grain=0 so the engine uses `preferredGrain()`.
        forEachWith<Transform, Velocity>(ctx,
            [](EntityHandle e, const Transform& t, const Velocity& v,
               CommandBuffer& cb) {
                Transform n = t;
                n.position.x += v.linear.x * 0.016f;
                cb.setTransform(e, n);
            });
    }
private:
    std::uint32_t grain_;
};

struct Result {
    LatencyHistogram h;
    double stealPct = 0.0;
};

template <typename Workload>
Result measure(std::uint32_t entityCount, std::uint32_t grain,
               int warmup, int iters) {
    Workload w;
    w.count = entityCount;
    Engine engine(benchConfig(/*workers=*/4, entityCount));
    if (!engine.initialize(w)) return {};
    engine.registerSystem(std::make_unique<GrainAwareSystem>(grain));

    LatencyHistogram h;
    h.reserve(static_cast<std::size_t>(iters));
    for (int i = 0; i < warmup; ++i) engine.step();
    const auto preStats = engine.jobSystemStats();
    Stopwatch sw;
    for (int i = 0; i < iters; ++i) {
        sw.start();
        engine.step();
        h.push(sw.elapsedNs());
    }
    const auto postStats = engine.jobSystemStats();
    h.finalize();

    Result r;
    r.h = std::move(h);
    const auto deltaJobs   = postStats.totalJobs  - preStats.totalJobs;
    const auto deltaStolen = postStats.stolenJobs - preStats.stolenJobs;
    r.stealPct = deltaJobs > 0
        ? static_cast<double>(deltaStolen) /
          static_cast<double>(deltaJobs) * 100.0
        : 0.0;
    engine.shutdown();
    return r;
}

void emit(CsvWriter& csv, const std::string& workload, std::size_t entities,
          std::uint32_t grain, const Result& r) {
    BenchRow row;
    row.label    = "grainAware";
    row.workload = workload;
    row.entities = entities;
    row.workers  = 4;
    row.grain    = grain;
    row.mean_ns  = r.h.meanNs();
    row.stddev   = r.h.stddev();
    row.p50_ns   = r.h.p50Ns();
    row.p95_ns   = r.h.p95Ns();
    row.p99_ns   = r.h.p99Ns();
    if (r.h.meanNs() > 0.0 && entities > 0) {
        row.throughput =
            static_cast<double>(entities) / (r.h.meanNs() / 1e9);
    }
    row.steal_pct = r.stealPct;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "ns_per_entity=%.2f",
                  r.h.meanNs() / static_cast<double>(entities));
    row.note = buf;
    csv.row(row);
}

} // namespace

int main(int argc, char** argv) {
    const char* csvPath = (argc >= 2) ? argv[1] : nullptr;
    CsvWriter csv(csvPath);

    constexpr int kWarmup = 8;
    constexpr int kIters  = 64;
    constexpr std::uint32_t kGrains[] = {8u, 16u, 32u, 64u, 128u, 256u, 512u};

    for (auto g : kGrains) {
        emit(csv, "AI", kAiCount, g,
             measure<AiOnlyWorkload>(kAiCount, g, kWarmup, kIters));
    }
    for (auto g : kGrains) {
        emit(csv, "Render+AI", kRenderCount, g,
             measure<RenderAiWorkload>(kRenderCount, g, kWarmup, kIters));
    }
    return 0;
}
