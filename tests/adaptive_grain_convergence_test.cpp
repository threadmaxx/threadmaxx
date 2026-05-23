// ADAPTIVE_TUNING.md T5 — convergence gate for AdaptiveGrainPolicy.
//
// We drive the policy through a *synthetic* feedback loop (no engine
// in the loop) so the test is hardware-independent and the
// "convergence within ±1 step of the offline optimum" assertion can
// be evaluated against an analytic expected value.
//
// Each simulated tick:
//   - The harness computes `avgSubJobMicros` from the simulated grain
//     and per-entity work.
//   - It packs the value into a `SystemStats` snapshot and hands it
//     to the policy via `observe(...)`.
//   - `propose()` runs. If it returns a patch, the harness adopts
//     the new grain immediately (no EWMA lag — the engine's lag
//     simply delays steady-state but does not change which grain the
//     policy settles on, and the lag is exercised by the engine-side
//     T4 test).
//
// Invariants asserted after 600 simulated ticks:
//   1. The policy fired at least once (the synthetic workload
//      starts with sub-jobs well below the policy's hold band so
//      coarsening must trigger).
//   2. The final grain places sub-job duration inside the hold band
//      `[target/2, target*4]`.
//   3. The policy has been quiet for at least `2 × cooldownTicks`
//      consecutive ticks at the end (proves it settled).
//
// The engine-side wiring (observe/propose cadence + patch
// application) is exercised by `tuning_patch_application_test`; the
// no-oscillation invariant is exercised by `adaptive_no_oscillation_test`.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <cstdint>

namespace {

using namespace threadmaxx;

constexpr std::uint32_t kEntities    = 5000;
constexpr double        kWorkPerEntNs = 100.0;  // synthetic
constexpr std::uint32_t kWorkerCount = 4;
constexpr std::uint32_t kInitialGrain = 12;     // engine auto-pick at
                                                // count=5000, workers=4
                                                // (count / (workers*4))

struct FakeWorld {
    AdaptiveGrainPolicy* policy;
    std::uint32_t grain;
    std::uint64_t tick;
};

void tick_once(FakeWorld& w) {
    w.tick += 1;
    SystemStats s;
    s.name              = "sys";
    s.subJobsLastStep   = (kEntities + w.grain - 1) / w.grain;
    s.avgSubJobMicros   = (static_cast<double>(w.grain) * kWorkPerEntNs)
                          / 1000.0;
    s.peakQueueDepth    = s.subJobsLastStep;
    EngineStats es;
    es.tick = w.tick;
    JobSystemStats js;
    js.workerCount = kWorkerCount;
    w.policy->observe(es, std::span{&s, 1}, js);
    if (auto p = w.policy->propose()) {
        for (const auto& ov : p->grainOverrides) {
            if (ov.systemName == "sys" && ov.preferredGrain > 0) {
                w.grain = ov.preferredGrain;
            }
        }
    }
}

} // namespace

int main() {
    using namespace threadmaxx;

    AdaptiveGrainPolicy::Config pcfg;
    pcfg.explorationEpsilon = 0.0;
    AdaptiveGrainPolicy policy{pcfg};

    FakeWorld w{&policy, kInitialGrain, 0};

    // Sanity: starting sub-job duration must be in the "coarsen"
    // direction (mic < target/2), otherwise the convergence test
    // would be vacuous.
    const double startMic = (kInitialGrain * kWorkPerEntNs) / 1000.0;
    CHECK(startMic < pcfg.targetSubJobMicros * 0.5);

    constexpr std::uint32_t kTicks = 600;
    std::uint64_t lastFireTick = 0;
    std::uint32_t totalFires   = 0;
    std::uint32_t priorGrain   = w.grain;
    for (std::uint32_t i = 0; i < kTicks; ++i) {
        tick_once(w);
        if (w.grain != priorGrain) {
            ++totalFires;
            lastFireTick = w.tick;
            priorGrain   = w.grain;
        }
    }

    // (1) Policy fired at least once.
    CHECK(totalFires >= 1);

    const auto applied = policy.lastAppliedGrain("sys");
    CHECK(applied.has_value());

    // (2) Final sub-job duration lands in the hold band.
    const double finalMic = (w.grain * kWorkPerEntNs) / 1000.0;
    CHECK(finalMic >= pcfg.targetSubJobMicros * 0.5);
    CHECK(finalMic <= pcfg.targetSubJobMicros * 4.0);

    // (3) Policy quiet for at least 2 × cooldownTicks at the end.
    const std::uint64_t silentTail = kTicks - lastFireTick;
    CHECK(silentTail >= 2 * pcfg.cooldownTicks);

    std::printf("[adaptive_grain_convergence] fires=%u finalGrain=%u "
                "finalMic=%.2f us silentTail=%llu\n",
                totalFires, w.grain, finalMic,
                static_cast<unsigned long long>(silentTail));

    EXIT_WITH_RESULT();
}
