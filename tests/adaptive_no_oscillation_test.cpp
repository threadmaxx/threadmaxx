// ADAPTIVE_TUNING.md T5 — no-oscillation gate for AdaptiveGrainPolicy.
//
// Hardware-independent synthetic harness (the engine wiring is
// validated by `tuning_patch_application_test`; convergence by
// `adaptive_grain_convergence_test`). We exercise the two
// no-oscillation invariants the spec calls for:
//
//   1. **Cooldown gate.** With sub-job duration pinned far below the
//      hold band's floor on every tick, the heuristic screams
//      "coarsen" continuously. The policy must still emit at most one
//      change per `cooldownTicks` window — i.e. consecutive fires
//      are at least `cooldownTicks` ticks apart.
//
//   2. **Direction-change suppression.** When the observed sub-job
//      duration alternates between "coarsen" and "split" each tick,
//      the streak counter resets on every direction change, so the
//      streak gate never opens and the policy never fires —
//      regardless of cooldown.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <cstdint>
#include <vector>

namespace {

using namespace threadmaxx;

void drive(AdaptiveGrainPolicy&    policy,
           std::uint64_t           ticks,
           double                  micEvenTick,
           double                  micOddTick,
           std::uint32_t           workers,
           std::vector<std::uint64_t>& fires) {
    std::uint32_t lastGrain = 0; // pre-fire sentinel
    for (std::uint64_t t = 1; t <= ticks; ++t) {
        SystemStats s;
        s.name            = "sys";
        s.subJobsLastStep = 16;        // enough to satisfy peakQueueDepth
        s.peakQueueDepth  = workers;   // satisfies split's queue gate
        s.avgSubJobMicros = (t % 2 == 0) ? micEvenTick : micOddTick;
        EngineStats es;
        es.tick = t;
        JobSystemStats js;
        js.workerCount = workers;
        policy.observe(es, std::span{&s, 1}, js);
        if (auto p = policy.propose()) {
            for (const auto& ov : p->grainOverrides) {
                if (ov.systemName == "sys" && ov.preferredGrain != lastGrain) {
                    fires.push_back(t);
                    lastGrain = ov.preferredGrain;
                }
            }
        }
    }
}

} // namespace

int main() {
    using namespace threadmaxx;

    // ----- Part 1: Cooldown gate ------------------------------------
    {
        AdaptiveGrainPolicy::Config pcfg;
        pcfg.explorationEpsilon = 0.0;
        AdaptiveGrainPolicy policy{pcfg};

        // Sub-job duration pinned at 10 µs every tick. Way below
        // target/2 = 100 µs — heuristic always votes "coarsen".
        std::vector<std::uint64_t> fires;
        drive(policy, /*ticks*/ 400, /*even*/ 10.0, /*odd*/ 10.0,
              /*workers*/ 4, fires);

        // Multiple fires happened (proves the policy is firing under
        // sustained pressure, not silently dead).
        CHECK(fires.size() >= 3);

        // Pair-wise gap must be at least cooldownTicks. This is the
        // explicit no-oscillation invariant.
        for (std::size_t i = 1; i < fires.size(); ++i) {
            CHECK(fires[i] - fires[i - 1] >= pcfg.cooldownTicks);
        }

        // Sliding cooldown-sized windows contain at most one fire.
        // Same invariant, expressed as the spec literally requires
        // ("no more than one change per 60-tick window").
        for (std::uint64_t winStart = 0; winStart + pcfg.cooldownTicks <= 400;
             ++winStart) {
            const std::uint64_t winEnd = winStart + pcfg.cooldownTicks;
            std::uint32_t hits = 0;
            for (auto t : fires) {
                if (t > winStart && t <= winEnd) ++hits;
            }
            CHECK(hits <= 1);
        }

        std::printf("[no_oscillation/cooldown] fires=%zu\n", fires.size());
    }

    // ----- Part 2: Direction-change suppression ---------------------
    {
        AdaptiveGrainPolicy::Config pcfg;
        pcfg.explorationEpsilon = 0.0;
        AdaptiveGrainPolicy policy{pcfg};

        // Alternate between deep-coarsen (10 µs, dir=+1) and
        // deep-split (5000 µs, dir=-1) signals every tick. The
        // streak counter should reset on every direction flip, so
        // the streak gate never opens.
        std::vector<std::uint64_t> fires;
        drive(policy, /*ticks*/ 400, /*even*/ 10.0, /*odd*/ 5000.0,
              /*workers*/ 4, fires);

        CHECK(fires.empty());
        std::printf("[no_oscillation/direction] fires=%zu (expected 0)\n",
                    fires.size());
    }

    EXIT_WITH_RESULT();
}
