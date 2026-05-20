// §3.10.4 batch 26 — RPG-shaped 100k-entity tick decomposition.
//
// The post-renderer-fix profile (2026-05-20) flagged `update` as the
// new dominant cost in `rpg_demo --stress`: at 100k entities, the
// frame breaks down roughly as
//     step ≈ 17 ms / upd ≈ 11 ms / commit ≈ 1 ms / engBRF ≈ 0.5 ms /
//     render ≈ 4 ms / other ≈ 0.5 ms
// This bench reproduces the engine-side shape of that workload —
// no Vulkan, no GLFW, no user components — so subsequent Phase 8
// batches can diff per-phase numbers (step / upd / commit / engBRF /
// other) against this baseline without needing a real screen.
//
// Methodology:
//   - Mirror `examples/rpg_demo` `--stress` at the engine level via
//     `RpgStressWorkload` (built-in components only).
//   - Register three systems that mirror the rpg_demo system mix:
//       * `MovementSystem`   — parallel `forEachChunk<Transform,
//         Velocity>` integrator (the parallel hot path).
//       * `BrainSystem`      — `ctx.single` body that walks every
//         Faction-bearing chunk serially (mirrors NPCBrainSystem; the
//         RNG-bound serial-only work).
//       * `RenderPrepSystem` — parallel `forEachChunk<Transform>`
//         accumulator that touches every visible entity (mirrors
//         CubeRenderSystem's snapshot pass without the BRF push).
//   - Run 300 ticks per scale; capture `EngineStats::lastStepSeconds`,
//     `commitDurationSeconds`, `engineBuildRenderFrameSeconds`, plus
//     the sum of `SystemStats::lastUpdateSeconds`.
//   - Emit one CSV row per (entity-count × phase). `note` carries
//     `ns_per_entity` for the row so a glance at the CSV gives the
//     headline derived metric.
//
// Three entity-count rows by default: 10k / 50k / 100k. Override via
// `argv[2..]` for ad-hoc sweeps; argv[1] is the CSV output path as in
// every other §3.9 bench.

#include "common.hpp"
#include "rpg_systems.hpp"
#include "scene_workloads.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace {

using namespace threadmaxx;
using namespace threadmaxx_bench;

// Per-tick snapshot. Captures the engine-stats fields the rpg_demo HUD
// reports + the sum of per-system update durations.
struct PhaseSample {
    double step_s     = 0.0;
    double update_s   = 0.0;  // sum of SystemStats.lastUpdateSeconds
    double commit_s   = 0.0;
    double engBRF_s   = 0.0;
    double other_s    = 0.0;  // residual
    std::uint64_t jobs = 0;
};

// Pull a phase sample after `engine.step()` has returned.
PhaseSample sample(const Engine& engine) {
    const auto es = engine.stats();
    const auto sys = engine.systemStats();
    PhaseSample s;
    s.step_s   = es.lastStepSeconds;
    s.commit_s = es.commitDurationSeconds;
    s.engBRF_s = es.engineBuildRenderFrameSeconds;
    s.jobs     = es.jobsSubmittedLastStep;
    for (const auto& sr : sys) {
        s.update_s += sr.lastUpdateSeconds;
    }
    s.other_s = s.step_s - s.update_s - s.commit_s - s.engBRF_s;
    if (s.other_s < 0.0) s.other_s = 0.0;
    return s;
}

// Run the workload at a given NPC scale and collect a histogram per
// phase. Returns one row per phase.
struct ScaleResult {
    std::uint32_t npcCount    = 0;
    std::uint32_t pickupCount = 0;
    std::size_t   totalEntities = 0;
    LatencyHistogram step;
    LatencyHistogram update;
    LatencyHistogram commit;
    LatencyHistogram engBRF;
    LatencyHistogram other;
};

ScaleResult runScale(std::uint32_t npcCount,
                     std::uint32_t pickupCount,
                     std::uint32_t workers,
                     int warmup,
                     int iters) {
    Config cfg = benchConfig(workers, npcCount + pickupCount + 16, false);
    Engine engine(cfg);
    RpgStressWorkload workload;
    workload.npcCount    = npcCount;
    workload.pickupCount = pickupCount;
    if (!engine.initialize(workload)) {
        std::printf("  scale init failed (npc=%u)\n", npcCount);
        engine.shutdown();
        return {};
    }
    engine.registerSystem(std::make_unique<MovementSystem>());
    engine.registerSystem(std::make_unique<BrainSystem>());
    engine.registerSystem(std::make_unique<RenderPrepSystem>());

    ScaleResult res;
    res.npcCount      = npcCount;
    res.pickupCount   = pickupCount;
    res.totalEntities = engine.world().size();

    res.step.reserve(static_cast<std::size_t>(iters));
    res.update.reserve(static_cast<std::size_t>(iters));
    res.commit.reserve(static_cast<std::size_t>(iters));
    res.engBRF.reserve(static_cast<std::size_t>(iters));
    res.other.reserve(static_cast<std::size_t>(iters));

    for (int i = 0; i < warmup; ++i) engine.step();
    for (int i = 0; i < iters; ++i) {
        engine.step();
        const auto s = sample(engine);
        res.step.push(static_cast<std::uint64_t>(s.step_s   * 1e9));
        res.update.push(static_cast<std::uint64_t>(s.update_s * 1e9));
        res.commit.push(static_cast<std::uint64_t>(s.commit_s * 1e9));
        res.engBRF.push(static_cast<std::uint64_t>(s.engBRF_s * 1e9));
        res.other.push(static_cast<std::uint64_t>(s.other_s  * 1e9));
    }
    res.step.finalize();
    res.update.finalize();
    res.commit.finalize();
    res.engBRF.finalize();
    res.other.finalize();

    engine.shutdown();
    return res;
}

void emitPhase(CsvWriter& csv, const char* phase, const ScaleResult& r,
               LatencyHistogram& h, std::uint32_t workers) {
    BenchRow row;
    row.label    = phase;
    row.workload = "RpgStress";
    row.entities = r.totalEntities;
    row.workers  = workers;
    row.grain    = 0;
    row.mean_ns  = h.meanNs();
    row.stddev   = h.stddev();
    row.p50_ns   = h.p50Ns();
    row.p95_ns   = h.p95Ns();
    row.p99_ns   = h.p99Ns();
    char buf[96];
    if (r.totalEntities > 0) {
        std::snprintf(buf, sizeof(buf), "ns_per_entity=%.2f npc=%u pickup=%u",
                      h.meanNs() / static_cast<double>(r.totalEntities),
                      r.npcCount, r.pickupCount);
    } else {
        std::snprintf(buf, sizeof(buf), "npc=%u pickup=%u",
                      r.npcCount, r.pickupCount);
    }
    row.note = buf;
    csv.row(row);
}

} // namespace

int main(int argc, char** argv) {
    const char* csvPath = (argc >= 2) ? argv[1] : nullptr;
    CsvWriter csv(csvPath);

    constexpr int kWarmup = 8;
    constexpr int kIters  = 128;
    constexpr std::uint32_t kWorkers = 4;

    struct Scale { std::uint32_t npc; std::uint32_t pickup; };
    std::vector<Scale> scales = {
        { 10'000u,  5'000u},
        { 50'000u,  5'000u},
        {100'000u,  5'000u},
    };

    // CLI sweep override: `rpg_stress_bench out.csv 25000 80000 ...`
    if (argc >= 3) {
        scales.clear();
        for (int i = 2; i < argc; ++i) {
            const auto npc = static_cast<std::uint32_t>(std::atoi(argv[i]));
            if (npc == 0) continue;
            scales.push_back({npc, 5'000u});
        }
    }

    for (const auto& s : scales) {
        auto r = runScale(s.npc, s.pickup, kWorkers, kWarmup, kIters);
        emitPhase(csv, "step",   r, r.step,   kWorkers);
        emitPhase(csv, "update", r, r.update, kWorkers);
        emitPhase(csv, "commit", r, r.commit, kWorkers);
        emitPhase(csv, "engBRF", r, r.engBRF, kWorkers);
        emitPhase(csv, "other",  r, r.other,  kWorkers);
    }
    return 0;
}
