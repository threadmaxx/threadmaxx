// §3.11 batch D12 audit — headless per-system profiler.
//
// Boots the shipped DemoGame at the full 96 m / ~92k-block voxel
// world (NO terrainCellsPerSide override), runs `kTicks` ticks
// without Vulkan / GLFW, then prints the top-N systems by mean
// per-tick cost so we can spot D12-induced regressions and pick
// optimization targets.
//
// Usage: `./perf_audit_rpg_demo [ticks] [--stress]`
// Defaults: 300 ticks, normal mode.
//
// Linked into the build only when `rpg_demo_core` is present.

#include <DemoGame.hpp>

#include <threadmaxx/Config.hpp>
#include <threadmaxx/Engine.hpp>
#include <threadmaxx/Stats.hpp>
#include <threadmaxx/Trace.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <vector>

namespace {

struct Acc {
    const char* name = nullptr;
    double sumUpdate = 0.0;
    double sumWait   = 0.0;
    double sumBrf    = 0.0;
    double maxUpdate = 0.0;
    std::uint64_t totalJobs = 0;
    std::uint64_t totalCmds = 0;
};

} // namespace

int main(int argc, char** argv) {
    std::uint64_t ticks      = 300;
    bool          stressMode = false;
    std::uint32_t workers    = 0; // 0 = auto
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        if (a == "--stress" || a == "-s") { stressMode = true; continue; }
        if (a.rfind("--workers=", 0) == 0) {
            workers = static_cast<std::uint32_t>(
                std::strtoul(a.data() + 10, nullptr, 10));
            continue;
        }
        ticks = static_cast<std::uint64_t>(std::strtoull(argv[i], nullptr, 10));
    }

    threadmaxx::Config cfg;
    cfg.sleepToPace = false;
    cfg.workerCount = workers; // 0 = auto = hw_concurrency - 1
    threadmaxx::Engine engine(cfg);

    rpg::DemoGame game;
    game.worldState().stressMode = stressMode;
    if (!engine.initialize(game)) {
        std::fprintf(stderr, "[perf_audit] initialize failed\n");
        return 1;
    }

    // Warm-up tick so the JobSystem pool, EventChannels, and chunked
    // archetypes settle before we sample.
    engine.step();

    const auto t0 = std::chrono::steady_clock::now();

    std::vector<Acc> acc;
    double sumStep   = 0.0;
    double sumCommit = 0.0;
    double maxStep   = 0.0;

    for (std::uint64_t t = 0; t < ticks; ++t) {
        engine.step();
        const auto snap = engine.frameSnapshot();

        sumStep   += snap.engine.lastStepSeconds;
        sumCommit += snap.engine.commitDurationSeconds;
        maxStep    = std::max(maxStep, snap.engine.lastStepSeconds);

        if (acc.size() < snap.systems.size()) acc.resize(snap.systems.size());
        for (std::size_t i = 0; i < snap.systems.size(); ++i) {
            const auto& s = snap.systems[i];
            acc[i].name       = s.name;
            acc[i].sumUpdate += s.lastUpdateSeconds;
            acc[i].sumWait   += s.waitSeconds;
            acc[i].sumBrf    += s.buildRenderFrameSeconds;
            acc[i].maxUpdate  = std::max(acc[i].maxUpdate, s.lastUpdateSeconds);
            acc[i].totalJobs += s.jobsSubmittedLastStep;
            acc[i].totalCmds += s.commandsCommittedLastStep;
        }
    }

    const auto wall = std::chrono::duration_cast<std::chrono::duration<double>>(
        std::chrono::steady_clock::now() - t0).count();

    const auto snap = engine.frameSnapshot();
    const double meanStep = sumStep / static_cast<double>(ticks);
    std::printf("[perf_audit] mode=%s ticks=%llu walltime=%.3fs entities=%zu\n",
                stressMode ? "stress" : "normal",
                static_cast<unsigned long long>(ticks),
                wall, snap.engine.aliveEntities);
    std::printf("[perf_audit] meanStep=%.3fms maxStep=%.3fms commitMean=%.3fms\n",
                meanStep * 1e3, maxStep * 1e3,
                (sumCommit / static_cast<double>(ticks)) * 1e3);
    std::printf("[perf_audit] workers=%u jobs/tick=%.1f cmds/tick=%.1f\n",
                snap.jobs.workerCount,
                snap.jobs.totalJobs / std::max<double>(1.0, static_cast<double>(ticks + 1)),
                snap.engine.totalCommandsCommitted /
                    std::max<double>(1.0, static_cast<double>(ticks + 1)));

    std::sort(acc.begin(), acc.end(),
              [](const Acc& a, const Acc& b) {
                  return (a.sumUpdate + a.sumBrf) > (b.sumUpdate + b.sumBrf);
              });

    std::printf("\n[perf_audit] top systems (per-tick mean, ms):\n");
    std::printf("  %-32s %10s %10s %10s %10s %10s\n",
                "system", "upd_ms", "brf_ms", "wait_ms", "max_ms", "cmds/tick");
    const double ticksD = static_cast<double>(ticks);
    for (std::size_t i = 0; i < acc.size() && i < 16; ++i) {
        const auto& a = acc[i];
        if (!a.name) continue;
        if (a.sumUpdate + a.sumBrf < 1e-6) continue;
        std::printf("  %-32s %10.3f %10.3f %10.3f %10.3f %10.1f\n",
                    a.name,
                    (a.sumUpdate / ticksD) * 1e3,
                    (a.sumBrf    / ticksD) * 1e3,
                    (a.sumWait   / ticksD) * 1e3,
                    a.maxUpdate * 1e3,
                    static_cast<double>(a.totalCmds) / ticksD);
    }

    engine.shutdown();
    return 0;
}
