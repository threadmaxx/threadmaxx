// ADAPTIVE_TUNING.md T4 + T5 — observe-only overhead gate + adaptive-grain
// convergence gate.
//
// Two modes, selected via `--mode=`:
//
//   --mode=stable        (default; T4)
//       Stable balanced workload. Compares baseline (no policy) to a
//       no-op policy installed; asserts the observe-only overhead
//       does not exceed +2% of baseline mean step time.
//
//   --mode=tiny-fanout   (T5)
//       Tiny-fanout workload — many small sub-jobs, dispatch overhead
//       dominates. Sweeps `preferredGrain` over a fixed ladder to
//       establish the offline optimum, then runs with
//       AdaptiveGrainPolicy and asserts the adaptive mean step is
//       within 10% of that optimum.
//
// The harness deliberately keeps the synthetic workloads simple
// (one ECS system, one parallelFor, fixed work per entity) so the
// bench measures what it advertises and the gate is sensitive to
// dispatch overhead rather than ambient system noise.

#include "common.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace {

using namespace threadmaxx;
using namespace threadmaxx_bench;

// Setup-side game: spawn `entityCount` entities, each with a Transform
// + a Velocity so the bench's parallelFor has something to walk.
class StableSetupGame : public IGame {
public:
    explicit StableSetupGame(std::uint32_t entityCount) : count_(entityCount) {}
    void onSetup(Engine&, World&, CommandBuffer& cb) override {
        for (std::uint32_t i = 0; i < count_; ++i) {
            cb.spawn(Transform{Vec3{static_cast<float>(i), 0, 0},
                               Quat{}, Vec3{1.0f, 1.0f, 1.0f}},
                     Velocity{Vec3{1, 0, 0}, Vec3{}});
        }
    }
private:
    std::uint32_t count_;
};

// Stable system: balanced wave, busy-spin per entity so the bench's
// per-tick cost is dominated by the system body (not the engine's
// scheduling overhead). Reads + writes Transform, so it conflicts with
// nothing else and lands in wave 0 alone.
class StableWaveSystem : public ISystem {
public:
    const char* name() const noexcept override { return "stable"; }
    ComponentSet reads()  const noexcept override {
        return ComponentSet{Component::Transform} |
               ComponentSet{Component::Velocity};
    }
    ComponentSet writes() const noexcept override {
        return ComponentSet{Component::Transform};
    }
    void update(SystemContext& ctx) override {
        const auto xforms  = ctx.world().transforms();
        const auto vels    = ctx.world().velocities();
        const auto ents    = ctx.world().entities();
        const std::uint32_t n = static_cast<std::uint32_t>(ents.size());
        ctx.parallelFor(n, /*grain*/ 0,
            [xData = xforms.data(), vData = vels.data(), eData = ents.data()]
            (Range r, CommandBuffer& cb) {
                for (std::uint32_t i = r.begin; i < r.end; ++i) {
                    Transform t = xData[i];
                    t.position.x += vData[i].linear.x * 0.016f;
                    volatile double acc = 0.0;
                    for (int k = 0; k < 256; ++k) {
                        acc += static_cast<double>(k) * 1.0001;
                    }
                    (void)acc;
                    cb.setTransform(eData[i], t);
                }
            });
    }
};

// Tiny-fanout system: tight per-entity loop (~32 ns) — at the engine's
// default auto-grain heuristic this produces O(60 µs) sub-jobs, deep
// in the dispatch-overhead-dominated regime that T5 targets. The
// adaptive policy should coarsen until sub-jobs reach the hold band.
class TinyFanoutSystem : public ISystem {
public:
    const char* name() const noexcept override { return "tiny-fanout"; }
    ComponentSet reads()  const noexcept override {
        return ComponentSet{Component::Transform} |
               ComponentSet{Component::Velocity};
    }
    ComponentSet writes() const noexcept override {
        return ComponentSet{Component::Transform};
    }
    void update(SystemContext& ctx) override {
        const auto xforms  = ctx.world().transforms();
        const auto vels    = ctx.world().velocities();
        const auto ents    = ctx.world().entities();
        const std::uint32_t n = static_cast<std::uint32_t>(ents.size());
        ctx.parallelFor(n, /*grain*/ 0,
            [xData = xforms.data(), vData = vels.data(), eData = ents.data()]
            (Range r, CommandBuffer& cb) {
                for (std::uint32_t i = r.begin; i < r.end; ++i) {
                    Transform t = xData[i];
                    t.position.x += vData[i].linear.x * 0.016f;
                    // ~32 ns of arithmetic per entity — well below
                    // the policy's target/2 floor at default
                    // auto-grain.
                    volatile double acc = 0.0;
                    for (int k = 0; k < 16; ++k) {
                        acc += static_cast<double>(k) * 1.0001;
                    }
                    (void)acc;
                    cb.setTransform(eData[i], t);
                }
            });
    }
    std::uint32_t preferredGrain() const noexcept override { return preferred_; }
    void setPreferredGrain(std::uint32_t g) noexcept { preferred_ = g; }
private:
    std::uint32_t preferred_ = 0;
};

class NoopPolicy : public ITuningPolicy {
public:
    void observe(const EngineStats&,
                 std::span<const SystemStats>,
                 const JobSystemStats&) override {
        observed_++;
    }
    std::optional<TuningPatch> propose() override {
        return std::nullopt;
    }
    std::uint64_t observedCount() const noexcept { return observed_; }
private:
    std::uint64_t observed_ = 0;
};

struct RunResult {
    double meanStepMs = 0.0;
    double p50Ms      = 0.0;
    double p95Ms      = 0.0;
    std::uint64_t observed = 0;
};

template <typename SystemT>
RunResult runOnce(std::uint32_t entities,
                  std::uint32_t ticks,
                  std::uint32_t warmupTicks,
                  ITuningPolicy* policy,
                  std::uint32_t  preferredGrain = 0) {
    Config cfg;
    cfg.sleepToPace = false;
    cfg.workerCount = 0; // auto
    Engine engine{cfg};

    StableSetupGame g{entities};
    if (!engine.initialize(g)) {
        std::fprintf(stderr, "[adaptive_tuning_bench] initialize failed\n");
        std::exit(1);
    }
    auto sysPtr = std::make_unique<SystemT>();
    SystemT* sys = sysPtr.get();
    (void)sys; // some SystemTs have no setPreferredGrain
    if constexpr (std::is_same_v<SystemT, TinyFanoutSystem>) {
        if (preferredGrain > 0) sys->setPreferredGrain(preferredGrain);
    }
    engine.registerSystem(std::move(sysPtr));

    if (policy) engine.setTuningPolicy(policy);

    for (std::uint32_t i = 0; i < warmupTicks; ++i) engine.step();

    LatencyHistogram h;
    h.reserve(ticks);
    for (std::uint32_t i = 0; i < ticks; ++i) {
        const auto t0 = std::chrono::steady_clock::now();
        engine.step();
        const auto t1 = std::chrono::steady_clock::now();
        h.push(static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()));
    }
    h.finalize();

    RunResult r;
    r.meanStepMs = h.meanNs() / 1e6;
    r.p50Ms      = static_cast<double>(h.p50Ns()) / 1e6;
    r.p95Ms      = static_cast<double>(h.p95Ns()) / 1e6;
    if (auto* np = dynamic_cast<NoopPolicy*>(policy)) {
        r.observed = np->observedCount();
    }

    engine.shutdown();
    return r;
}

int runStableMode(std::uint32_t entities,
                  std::uint32_t ticks,
                  std::uint32_t warmupTicks) {
    std::printf("[adaptive_tuning_bench] mode=stable entities=%u ticks=%u warmup=%u\n",
                entities, ticks, warmupTicks);

    constexpr int kRounds = 3;
    std::vector<double> baselineMs;
    std::vector<double> noopMs;
    baselineMs.reserve(kRounds);
    noopMs.reserve(kRounds);
    RunResult lastNoop{};
    for (int round = 0; round < kRounds; ++round) {
        const auto b = runOnce<StableWaveSystem>(entities, ticks, warmupTicks, nullptr);
        NoopPolicy np;
        const auto n = runOnce<StableWaveSystem>(entities, ticks, warmupTicks, &np);
        baselineMs.push_back(b.meanStepMs);
        noopMs.push_back(n.meanStepMs);
        lastNoop = n;
        lastNoop.observed = np.observedCount();
        std::printf("  round %d: baseline=%.4f ms  noop=%.4f ms\n",
                    round, b.meanStepMs, n.meanStepMs);
    }

    std::sort(baselineMs.begin(), baselineMs.end());
    std::sort(noopMs.begin(), noopMs.end());
    const double baselineMedian = baselineMs[kRounds / 2];
    const double noopMedian     = noopMs[kRounds / 2];
    const double delta = noopMedian - baselineMedian;
    const double pct   = (delta / baselineMedian) * 100.0;

    std::printf("  baseline median mean_ms: %.4f\n", baselineMedian);
    std::printf("  noop     median mean_ms: %.4f\n", noopMedian);
    std::printf("  delta median: %+0.4f ms (%+0.2f%%)\n", delta, pct);

    if (lastNoop.observed != ticks + warmupTicks) {
        std::fprintf(stderr,
            "[adaptive_tuning_bench] FAIL: expected %u observes, got %llu\n",
            ticks + warmupTicks,
            static_cast<unsigned long long>(lastNoop.observed));
        return 2;
    }

    if (pct > 2.0) {
        std::fprintf(stderr,
            "[adaptive_tuning_bench] FAIL: observe-only overhead %+0.2f%% "
            "exceeds the +2%% gate.\n", pct);
        return 1;
    }

    std::printf("[adaptive_tuning_bench] PASS — observe-only overhead within +2%%\n");
    return 0;
}

int runTinyFanoutMode(std::uint32_t entities,
                      std::uint32_t ticks,
                      std::uint32_t warmupTicks) {
    // Tiny-fanout needs the policy fully converged before the
    // measurement window opens — first-fire at tick 3, second at
    // tick 63, and any subsequent ones at +60-tick steps. Anything
    // less than ~100 ticks of warmup pollutes the mean with
    // mid-convergence samples.
    if (warmupTicks < 100) warmupTicks = 100;
    std::printf("[adaptive_tuning_bench] mode=tiny-fanout entities=%u ticks=%u warmup=%u\n",
                entities, ticks, warmupTicks);

    // Sweep grain values to establish the offline optimum.
    const std::uint32_t kGrainLadder[] = {64, 128, 256, 512, 1024, 2048, 4096};
    double sweepBestMs = std::numeric_limits<double>::infinity();
    std::uint32_t sweepBestGrain = 0;
    for (std::uint32_t g : kGrainLadder) {
        const auto r = runOnce<TinyFanoutSystem>(entities, ticks, warmupTicks,
                                                 nullptr, g);
        std::printf("  sweep grain=%5u  mean=%.4f ms  p95=%.4f ms\n",
                    g, r.meanStepMs, r.p95Ms);
        if (r.meanStepMs < sweepBestMs) {
            sweepBestMs    = r.meanStepMs;
            sweepBestGrain = g;
        }
    }
    std::printf("  offline optimum: grain=%u  mean=%.4f ms\n",
                sweepBestGrain, sweepBestMs);

    // Adaptive run. We use the default Config; targetSubJobMicros=200
    // is in the hold band where the sweep optimum lives for this
    // workload shape.
    AdaptiveGrainPolicy::Config pcfg;
    AdaptiveGrainPolicy policy{pcfg};
    const auto adaptive = runOnce<TinyFanoutSystem>(entities, ticks, warmupTicks,
                                                    &policy);
    std::printf("  adaptive: mean=%.4f ms  p95=%.4f ms  finalGrain=%u\n",
                adaptive.meanStepMs, adaptive.p95Ms,
                policy.lastAppliedGrain("tiny-fanout").value_or(0));

    const double pct = ((adaptive.meanStepMs - sweepBestMs) / sweepBestMs) * 100.0;
    std::printf("  delta vs optimum: %+0.2f%%\n", pct);

    if (pct > 10.0) {
        std::fprintf(stderr,
            "[adaptive_tuning_bench] FAIL: adaptive %+0.2f%% above offline "
            "optimum (gate: ≤ +10%%).\n", pct);
        return 1;
    }
    std::printf("[adaptive_tuning_bench] PASS — adaptive within +10%% of offline optimum\n");
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    std::string_view mode = "stable";
    std::uint32_t entities    = 50000;
    std::uint32_t ticks       = 200;
    std::uint32_t warmupTicks = 20;
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        if (a.rfind("--entities=", 0) == 0) {
            entities = static_cast<std::uint32_t>(
                std::strtoul(a.data() + 11, nullptr, 10));
        } else if (a.rfind("--ticks=", 0) == 0) {
            ticks = static_cast<std::uint32_t>(
                std::strtoul(a.data() + 8, nullptr, 10));
        } else if (a.rfind("--warmup=", 0) == 0) {
            warmupTicks = static_cast<std::uint32_t>(
                std::strtoul(a.data() + 9, nullptr, 10));
        } else if (a.rfind("--mode=", 0) == 0) {
            mode = a.substr(7);
        }
    }

    if (mode == "stable")      return runStableMode(entities, ticks, warmupTicks);
    if (mode == "tiny-fanout") return runTinyFanoutMode(entities, ticks, warmupTicks);

    std::fprintf(stderr, "[adaptive_tuning_bench] unknown --mode=%.*s\n",
                 static_cast<int>(mode.size()), mode.data());
    return 2;
}
