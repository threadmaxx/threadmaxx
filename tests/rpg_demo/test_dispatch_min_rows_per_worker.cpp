// ADAPTIVE_TUNING.md T1 — verify the `effectiveSubJobCount` heuristic
// in `ParallelDispatch.hpp`. The formula is:
//   subJobs = min(workerCount * kLoadBalanceMultiplier, count / kMinRowsPerWorker)
// clamped to [1, ∞). The constraint pair preserves B28's 4×-workers
// load-balancing slack for big counts while flooring per-sub-job
// row count for small ones.

#include "../Check.hpp"

#include "ParallelDispatch.hpp"

#include <algorithm>
#include <cstdint>

namespace {

constexpr std::uint32_t kMinRows = rpg::kMinRowsPerWorker;
constexpr std::uint32_t kMult    = rpg::kLoadBalanceMultiplier;

// Brute-force reference: encode the spec directly, compare against
// the constexpr implementation. Lets us bench many (count, workers)
// pairs without hand-listing expected outputs.
std::uint32_t referenceSubJobs(std::uint32_t count,
                               std::uint32_t workerCount) {
    if (count == 0 || workerCount == 0) return 1u;
    const std::uint32_t loadBalance = workerCount * kMult;
    const std::uint32_t minRowsBound = count / kMinRows;
    const std::uint32_t bounded =
        (minRowsBound < 1u) ? 1u : minRowsBound;
    return std::min(loadBalance, bounded);
}

}

int main() {
    using rpg::effectiveSubJobCount;

    // --- Edge cases -------------------------------------------------
    CHECK_EQ(effectiveSubJobCount(0u, 8u), 1u);
    CHECK_EQ(effectiveSubJobCount(1000u, 0u), 1u);
    CHECK_EQ(effectiveSubJobCount(0u, 0u), 1u);

    // --- Single-worker pool ---------------------------------------
    // Load-balance target = 1*4 = 4, but with a single worker we
    // still allow B28's 4× slack — workers can steal from each other
    // even when there's only one of them (they just won't). Stay
    // consistent with the engine's pickGrain.
    CHECK_EQ(effectiveSubJobCount(1u, 1u), 1u);
    CHECK_EQ(effectiveSubJobCount(kMinRows * 8u, 1u), 4u);  // min(4, 8) = 4
    CHECK_EQ(effectiveSubJobCount(1'000'000u, 1u), 4u);

    // --- Sub-`kMinRowsPerWorker` counts: floor binds to 1 ---------
    CHECK_EQ(effectiveSubJobCount(1u, 71u), 1u);
    CHECK_EQ(effectiveSubJobCount(kMinRows - 1u, 71u), 1u);
    // Exactly at the floor: 1 sub-job; loadBalance=284 vs bound=1 → 1.
    CHECK_EQ(effectiveSubJobCount(kMinRows, 71u), 1u);

    // --- Just above the floor: each additional kMinRows unlocks one
    //     more sub-job, until the load-balance target binds. -------
    CHECK_EQ(effectiveSubJobCount(kMinRows + 1u, 71u), 1u);   // 1 row over → still 1
    CHECK_EQ(effectiveSubJobCount(2u * kMinRows, 71u), 2u);
    CHECK_EQ(effectiveSubJobCount(2u * kMinRows + 1u, 71u), 2u);
    CHECK_EQ(effectiveSubJobCount(3u * kMinRows, 71u), 3u);

    // --- Crossover point: when count = workers*kMult*kMinRows, both
    //     constraints bind at the same value. ----------------------
    const std::uint32_t crossover = 71u * kMult * kMinRows;
    CHECK_EQ(effectiveSubJobCount(crossover, 71u), 71u * kMult);

    // --- Above the crossover: load-balance target binds. ---------
    //     This is the cube-render regime — 140k entities at workers=71:
    //     loadBalance=284, minRowsBound=140000/256=546 → 284.
    CHECK_EQ(effectiveSubJobCount(140'000u, 71u), 284u);

    // --- 8-worker box, 100k rows: loadBalance=32, bound=390 → 32 -
    CHECK_EQ(effectiveSubJobCount(100'000u, 8u), 32u);

    // --- Small-count regime that T1 is meant to protect ----------
    //     At workers=71 and count=10000, baseline auto-grain would
    //     give 284 sub-jobs of 35 rows each (way below the floor).
    //     T1 reduces to 39.
    CHECK_EQ(effectiveSubJobCount(10'000u, 71u), 10'000u / kMinRows);

    // --- Sweep: reference vs implementation agree everywhere -----
    for (std::uint32_t workers : {1u, 2u, 4u, 8u, 16u, 32u, 71u, 256u}) {
        for (std::uint32_t count :
             {1u, 10u, 100u, 257u, 1000u, 2000u, 5000u, 10000u,
              50000u, 100000u, 200000u, 1'000'000u}) {
            CHECK_EQ(effectiveSubJobCount(count, workers),
                     referenceSubJobs(count, workers));
        }
    }

    EXIT_WITH_RESULT();
}
