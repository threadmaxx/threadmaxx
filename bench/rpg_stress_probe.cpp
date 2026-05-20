// §3.10.4 batch 27 — Diagnostic probe for the RPG-stress workload.
//
// `rpg_stress_bench` (B26) reports per-phase decomposition for the
// production gate; this probe goes deeper. Three diagnostic passes,
// each surfacing a different attribution question:
//
//   Pass A — Per-system breakdown. Runs the full 3-system mix at each
//            scale; reports each system's `lastUpdateSeconds` +
//            `waitSeconds` + `peakQueueDepth` averaged over the
//            measurement window. Answers "which system in the update
//            phase is the long pole?"
//
//   Pass B — System-mix ablation. Runs the workload with each system
//            individually disabled; reports the delta against the
//            baseline. Answers "if we deleted system X entirely, how
//            much would step time drop?"
//
//   Pass C — Commit-cost dependency on command volume. Runs Movement
//            alone at varying NPC counts; reports commit_s as a
//            function of cmd count (NPC count). Confirms the
//            per-command FNV-1a-64 hash cost is the dominant factor at
//            scale.
//
// Output is plain text (not CSV) — this is a one-shot diagnostic, not
// a regression gate. The findings feed into `bench/profile_report.md`
// which is the actual deliverable of batch 27.

#include "rpg_systems.hpp"
#include "scene_workloads.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace {

using namespace threadmaxx;
using namespace threadmaxx_bench;

// ---- Pass A: per-system breakdown -------------------------------------------

struct PerSysSample {
    std::string  name;
    double       lastUpdate_s     = 0.0;
    double       wait_s           = 0.0;
    std::uint32_t peakQueueDepth   = 0;
    std::uint64_t commandsLastStep = 0;
};

struct PassAResult {
    std::uint32_t npcCount = 0;
    std::size_t   total    = 0;
    double        step_avg_s    = 0.0;
    double        update_avg_s  = 0.0;
    double        commit_avg_s  = 0.0;
    std::vector<PerSysSample> systems; // averaged across measurement window
};

PassAResult runPassA(std::uint32_t npcCount,
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
        std::printf("  pass A init failed (npc=%u)\n", npcCount);
        engine.shutdown();
        return {};
    }
    engine.registerSystem(std::make_unique<MovementSystem>());
    engine.registerSystem(std::make_unique<BrainSystem>());
    engine.registerSystem(std::make_unique<RenderPrepSystem>());

    PassAResult r;
    r.npcCount = npcCount;
    r.total    = engine.world().size();
    r.systems.resize(3);
    r.systems[0].name = "movement";
    r.systems[1].name = "brain";
    r.systems[2].name = "renderprep";

    for (int i = 0; i < warmup; ++i) engine.step();
    double stepAcc = 0.0, commitAcc = 0.0;
    std::vector<double> updAcc(3, 0.0);
    std::vector<double> waitAcc(3, 0.0);
    std::vector<std::uint32_t> peakMax(3, 0);
    std::vector<std::uint64_t> cmdsAcc(3, 0);
    for (int i = 0; i < iters; ++i) {
        engine.step();
        const auto es  = engine.stats();
        const auto sys = engine.systemStats();
        stepAcc   += es.lastStepSeconds;
        commitAcc += es.commitDurationSeconds;
        for (std::size_t s = 0; s < sys.size() && s < 3; ++s) {
            updAcc[s]  += sys[s].lastUpdateSeconds;
            waitAcc[s] += sys[s].waitSeconds;
            cmdsAcc[s] += sys[s].commandsCommittedLastStep;
            if (sys[s].peakQueueDepth > peakMax[s])
                peakMax[s] = sys[s].peakQueueDepth;
        }
    }
    r.step_avg_s   = stepAcc / iters;
    r.commit_avg_s = commitAcc / iters;
    for (int s = 0; s < 3; ++s) {
        r.systems[s].lastUpdate_s     = updAcc[s] / iters;
        r.systems[s].wait_s           = waitAcc[s] / iters;
        r.systems[s].peakQueueDepth   = peakMax[s];
        r.systems[s].commandsLastStep = cmdsAcc[s] / static_cast<std::uint64_t>(iters);
        r.update_avg_s += r.systems[s].lastUpdate_s;
    }
    engine.shutdown();
    return r;
}

// ---- Pass B: system-mix ablation --------------------------------------------

enum class SystemMix : std::uint8_t {
    AllThree   = 0,
    NoMovement = 1,
    NoBrain    = 2,
    NoRenderPrep = 3,
    MovementOnly = 4,
    BrainOnly    = 5,
    RenderPrepOnly = 6,
};

const char* mixName(SystemMix m) {
    switch (m) {
        case SystemMix::AllThree:      return "all";
        case SystemMix::NoMovement:    return "no-movement";
        case SystemMix::NoBrain:       return "no-brain";
        case SystemMix::NoRenderPrep:  return "no-renderprep";
        case SystemMix::MovementOnly:  return "movement-only";
        case SystemMix::BrainOnly:     return "brain-only";
        case SystemMix::RenderPrepOnly: return "renderprep-only";
    }
    return "?";
}

struct PassBSample {
    std::string mix;
    double      step_avg_s   = 0.0;
    double      update_avg_s = 0.0;
    double      commit_avg_s = 0.0;
};

PassBSample runPassB(SystemMix m,
                     std::uint32_t npcCount,
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
        engine.shutdown();
        return {};
    }
    const bool wantMove = (m == SystemMix::AllThree)
                       || (m == SystemMix::NoBrain)
                       || (m == SystemMix::NoRenderPrep)
                       || (m == SystemMix::MovementOnly);
    const bool wantBrain = (m == SystemMix::AllThree)
                        || (m == SystemMix::NoMovement)
                        || (m == SystemMix::NoRenderPrep)
                        || (m == SystemMix::BrainOnly);
    const bool wantRP = (m == SystemMix::AllThree)
                     || (m == SystemMix::NoMovement)
                     || (m == SystemMix::NoBrain)
                     || (m == SystemMix::RenderPrepOnly);
    if (wantMove)  engine.registerSystem(std::make_unique<MovementSystem>());
    if (wantBrain) engine.registerSystem(std::make_unique<BrainSystem>());
    if (wantRP)    engine.registerSystem(std::make_unique<RenderPrepSystem>());

    for (int i = 0; i < warmup; ++i) engine.step();
    double stepAcc = 0.0, commitAcc = 0.0, updAcc = 0.0;
    for (int i = 0; i < iters; ++i) {
        engine.step();
        const auto es  = engine.stats();
        const auto sys = engine.systemStats();
        stepAcc   += es.lastStepSeconds;
        commitAcc += es.commitDurationSeconds;
        for (const auto& s : sys) updAcc += s.lastUpdateSeconds;
    }
    PassBSample s;
    s.mix         = mixName(m);
    s.step_avg_s  = stepAcc / iters;
    s.commit_avg_s = commitAcc / iters;
    s.update_avg_s = updAcc / iters;
    engine.shutdown();
    return s;
}

// ---- Pass C: commit cost vs command volume ----------------------------------

struct PassCSample {
    std::uint32_t npcCount = 0;
    std::uint64_t cmdsPerStep = 0;
    double        commit_avg_s = 0.0;
    double        step_avg_s   = 0.0;
};

PassCSample runPassC(std::uint32_t npcCount,
                     std::uint32_t workers,
                     int warmup,
                     int iters) {
    // Movement only, varying NPC count. Pickup stays at 0 so the only
    // cmd-emitting workload scales linearly with npcCount.
    Config cfg = benchConfig(workers, npcCount + 64, false);
    Engine engine(cfg);
    RpgStressWorkload workload;
    workload.npcCount    = npcCount;
    workload.pickupCount = 0;
    if (!engine.initialize(workload)) { engine.shutdown(); return {}; }
    engine.registerSystem(std::make_unique<MovementSystem>());

    for (int i = 0; i < warmup; ++i) engine.step();
    double stepAcc = 0.0, commitAcc = 0.0;
    std::uint64_t cmdsAcc = 0;
    for (int i = 0; i < iters; ++i) {
        engine.step();
        const auto es = engine.stats();
        stepAcc   += es.lastStepSeconds;
        commitAcc += es.commitDurationSeconds;
        cmdsAcc   += es.commandsCommittedLastStep;
    }
    PassCSample s;
    s.npcCount     = npcCount;
    s.cmdsPerStep  = cmdsAcc / static_cast<std::uint64_t>(iters);
    s.step_avg_s   = stepAcc / iters;
    s.commit_avg_s = commitAcc / iters;
    engine.shutdown();
    return s;
}

void printPassA(const PassAResult& r) {
    std::printf("  scale npc=%u total=%zu  step=%.3fms upd=%.3fms commit=%.3fms\n",
                r.npcCount, r.total,
                r.step_avg_s * 1e3, r.update_avg_s * 1e3, r.commit_avg_s * 1e3);
    std::printf("    %-12s  upd=%8.3f ms  wait=%8.3f ms  peakQD=%3u  cmds=%llu\n",
                "system", 0.0, 0.0, 0u, 0ull);
    for (const auto& s : r.systems) {
        std::printf("    %-12s  upd=%8.3f ms  wait=%8.3f ms  peakQD=%3u  cmds=%llu\n",
                    s.name.c_str(),
                    s.lastUpdate_s * 1e3,
                    s.wait_s * 1e3,
                    s.peakQueueDepth,
                    static_cast<unsigned long long>(s.commandsLastStep));
    }
}

void printPassB(const std::vector<PassBSample>& rows) {
    std::printf("  %-18s  step=     upd=      commit=\n", "mix");
    for (const auto& s : rows) {
        std::printf("  %-18s  step=%6.2fms upd=%6.2fms commit=%6.2fms\n",
                    s.mix.c_str(),
                    s.step_avg_s * 1e3,
                    s.update_avg_s * 1e3,
                    s.commit_avg_s * 1e3);
    }
}

void printPassC(const std::vector<PassCSample>& rows) {
    std::printf("  %-10s %-14s %-14s %-18s %-18s\n",
                "npc", "cmds/step", "step (ms)", "commit (ms)", "ns/cmd (commit)");
    for (const auto& s : rows) {
        const double nsPerCmd = (s.cmdsPerStep > 0)
            ? (s.commit_avg_s * 1e9) / static_cast<double>(s.cmdsPerStep)
            : 0.0;
        std::printf("  %-10u %-14llu %-14.3f %-18.3f %-18.2f\n",
                    s.npcCount,
                    static_cast<unsigned long long>(s.cmdsPerStep),
                    s.step_avg_s * 1e3,
                    s.commit_avg_s * 1e3,
                    nsPerCmd);
    }
}

} // namespace

int main() {
    constexpr int kWarmup  = 8;
    constexpr int kIters   = 96;
    constexpr std::uint32_t kWorkers  = 4;
    constexpr std::uint32_t kPickup   = 5'000;

    std::printf("=== Pass A — per-system breakdown ===\n");
    for (const auto npc : {10'000u, 50'000u, 100'000u}) {
        const auto r = runPassA(npc, kPickup, kWorkers, kWarmup, kIters);
        printPassA(r);
    }

    std::printf("\n=== Pass B — system-mix ablation (npc=100k) ===\n");
    std::vector<PassBSample> bRows;
    for (auto m : {SystemMix::AllThree,
                   SystemMix::NoMovement,
                   SystemMix::NoBrain,
                   SystemMix::NoRenderPrep,
                   SystemMix::MovementOnly,
                   SystemMix::BrainOnly,
                   SystemMix::RenderPrepOnly}) {
        bRows.push_back(runPassB(m, 100'000u, kPickup, kWorkers, kWarmup, kIters));
    }
    printPassB(bRows);

    std::printf("\n=== Pass C — commit cost vs cmd volume (movement only, pickup=0) ===\n");
    std::vector<PassCSample> cRows;
    for (auto npc : {1'000u, 5'000u, 10'000u, 25'000u, 50'000u,
                     100'000u, 200'000u}) {
        cRows.push_back(runPassC(npc, kWorkers, kWarmup, kIters));
    }
    printPassC(cRows);

    return 0;
}
