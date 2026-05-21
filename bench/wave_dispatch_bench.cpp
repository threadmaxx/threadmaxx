// B31 diagnostic — wave-scheduler dispatch overhead.
//
// B27's downgrade rationale was: at 100k entities + 1-3 waves the
// wave-rebuild + per-wave dispatch is below 1% of step time. The
// re-eligibility criterion was "20+ systems and 8+ waves." This
// bench builds that scenario synthetically: N systems all writing
// the same component (forcing one-system-per-wave registration-
// order serial schedule) on either an empty world or a small
// realistic world. Trivial system bodies isolate the dispatch
// cost from per-system work.
//
// What we measure:
//   - `step` mean ns across a sweep of N (number of waves).
//   - Derived: per-wave overhead = (step_ns_at_N - step_ns_at_1) / (N - 1).
//
// What we control for:
//   - Each system's `update()` does a single `ctx.single([](Range,
//     CommandBuffer& cb){})` no-op so the wave still goes through
//     a real submit / latch / commit cycle (the actual codepath
//     B31 would optimize).
//   - All systems write Transform, so the wave scheduler must put
//     each in its own wave (W∩W conflict). With N systems we get
//     N waves of 1 system each.
//
// Two scaffolds:
//   (a) Empty world (0 entities). Pure dispatch overhead — no
//       commit work, no chunk rebuilds, just the framework cost.
//   (b) 10k entities in 1 archetype. Adds WorldView::rebuild +
//       chunk hash dirty work per tick. Tells us whether the
//       dispatch cost compounds with the per-wave WorldView
//       rebuild.

#include "common.hpp"
#include "scene_workloads.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace {

using namespace threadmaxx;
using namespace threadmaxx_bench;

// Trivial per-wave system. Declares `writes = {Transform}` so the
// scheduler forces it into its own wave. The update body does a
// single `ctx.single` no-op — submits one job through the same
// dispatch path a real system would use, but with zero work.
// `update()` submits one no-op job via `ctx.single`. This is the
// realistic per-wave cost: framework dispatch + one inner job
// submit + latch.
class TracerSystem : public ISystem {
public:
    explicit TracerSystem(int id) {
        nameBuf_ = "tracer-" + std::to_string(id);
    }
    const char* name() const noexcept override { return nameBuf_.c_str(); }
    ComponentSet reads()  const noexcept override { return ComponentSet::none(); }
    ComponentSet writes() const noexcept override {
        return ComponentSet{Component::Transform};
    }
    void update(SystemContext& ctx) override {
        ctx.single([](Range, CommandBuffer&) {});
    }

private:
    std::string nameBuf_;
};

// `update()` does literally nothing — no `ctx.single`, no
// `parallelFor`. Isolates the per-wave engine-framework overhead
// (WorldView rebuild, ctx construction, commit walk over zero
// buffers, post-wave budget check) from the inner-job submit cost.
class EmptyUpdateSystem : public ISystem {
public:
    explicit EmptyUpdateSystem(int id) {
        nameBuf_ = "empty-" + std::to_string(id);
    }
    const char* name() const noexcept override { return nameBuf_.c_str(); }
    ComponentSet reads()  const noexcept override { return ComponentSet::none(); }
    ComponentSet writes() const noexcept override {
        return ComponentSet{Component::Transform};
    }
    void update(SystemContext&) override { /* literally nothing */ }

private:
    std::string nameBuf_;
};

// Optional seeding game for the "with-entities" variant. Spawns a
// single archetype with N Transform-only entities.
class TransformSeedGame : public IGame {
public:
    std::uint32_t count = 0;
    void onSetup(Engine&, World&, CommandBuffer& cb) override {
        for (std::uint32_t i = 0; i < count; ++i) {
            cb.spawn(Transform{});
        }
    }
};

struct WaveSweepResult {
    int      systems = 0;
    int      waves   = 0;
    std::uint64_t entities = 0;
    LatencyHistogram step;
};

enum class SystemKind { Tracer, EmptyUpdate };

WaveSweepResult runSweep(int numSystems, std::uint32_t entityCount,
                         std::uint32_t workers, int warmup, int iters,
                         SystemKind kind) {
    Config cfg = benchConfig(workers, entityCount + 16, /*sharded=*/false);
    Engine engine(cfg);

    TransformSeedGame g;
    g.count = entityCount;
    if (!engine.initialize(g)) {
        std::printf("  init failed (systems=%d entities=%u)\n",
                    numSystems, entityCount);
        engine.shutdown();
        return {};
    }

    for (int i = 0; i < numSystems; ++i) {
        if (kind == SystemKind::Tracer) {
            engine.registerSystem(std::make_unique<TracerSystem>(i));
        } else {
            engine.registerSystem(std::make_unique<EmptyUpdateSystem>(i));
        }
    }

    // Sanity check the scheduler actually produced numSystems waves.
    const std::size_t waveCount = engine.taskGraphSnapshot().size() > 0
        ? [&engine]() {
              const auto graph = engine.taskGraphSnapshot();
              std::size_t maxWave = 0;
              for (const auto& n : graph) {
                  if (n.wave > maxWave) maxWave = n.wave;
              }
              return maxWave + 1;
          }()
        : 0;

    WaveSweepResult res;
    res.systems  = numSystems;
    res.waves    = static_cast<int>(waveCount);
    res.entities = engine.world().size();
    res.step.reserve(static_cast<std::size_t>(iters));

    runIters(res.step, warmup, iters, [&] { engine.step(); });

    engine.shutdown();
    return res;
}

void emit(CsvWriter& csv, const WaveSweepResult& r,
          const char* workload, std::uint32_t workers) {
    BenchRow row;
    row.label    = "step";
    row.workload = workload;
    row.entities = static_cast<std::size_t>(r.entities);
    row.workers  = workers;
    row.grain    = 0;
    row.mean_ns  = r.step.meanNs();
    row.stddev   = r.step.stddev();
    row.p50_ns   = r.step.p50Ns();
    row.p95_ns   = r.step.p95Ns();
    row.p99_ns   = r.step.p99Ns();
    char buf[96];
    std::snprintf(buf, sizeof(buf),
                  "systems=%d waves=%d ns_per_wave=%.1f",
                  r.systems, r.waves,
                  r.step.meanNs() / static_cast<double>(r.waves));
    row.note = buf;
    csv.row(row);
}

} // namespace

int main(int argc, char** argv) {
    const char* csvPath = (argc >= 2) ? argv[1] : nullptr;
    CsvWriter csv(csvPath);

    constexpr int kWarmup = 8;
    constexpr int kIters  = 256;
    constexpr std::uint32_t kWorkers = 4;

    // System counts to sweep. Includes well below the B27 downgrade
    // threshold (1, 2) and well above the re-eligibility criterion
    // (20, 32).
    const std::vector<int> systemCounts = {1, 2, 4, 8, 16, 24, 32};

    // (a) Empty world, tracer system → realistic per-wave cost
    //     (framework dispatch + one inner ctx.single submit).
    for (int n : systemCounts) {
        const auto r = runSweep(n, /*entities=*/0, kWorkers, kWarmup, kIters,
                                SystemKind::Tracer);
        emit(csv, r, "Tracer/empty", kWorkers);
    }

    // (b) 10k entities, tracer system → adds WorldView::rebuild
    //     + chunk-hash dirty work per tick.
    for (int n : systemCounts) {
        const auto r = runSweep(n, /*entities=*/10'000u, kWorkers,
                                kWarmup, kIters, SystemKind::Tracer);
        emit(csv, r, "Tracer/10k", kWorkers);
    }

    // (c) Empty world, EmptyUpdate system → framework-only floor.
    //     No inner job submit; pure wave-loop overhead in step().
    for (int n : systemCounts) {
        const auto r = runSweep(n, /*entities=*/0, kWorkers, kWarmup, kIters,
                                SystemKind::EmptyUpdate);
        emit(csv, r, "Empty/empty", kWorkers);
    }

    return 0;
}
