// ADAPTIVE_TUNING.md T3 — SystemStats::avgSubJobMicros + subJobsLastStep.
// The engine wraps every parallelFor sub-job's user lambda with a
// chrono::steady_clock pair and folds the resulting nanos into an
// EWMA on the per-system stats. This test pins the contract:
//
//   1. A system that calls parallelFor with a known per-sub-job
//      duration sees avgSubJobMicros converge to that duration.
//   2. subJobsLastStep equals the number of parallelFor sub-jobs
//      dispatched in the most recent step.
//   3. A quiet system (no parallelFor) keeps both fields at zero.
//   4. avgSubJobMicros persists as an EWMA across steps where the
//      system did dispatch sub-jobs; subJobsLastStep is per-step.
//
// The truth value is enforced by a busy-spin (not sleep_for) so the
// test isn't subject to the kernel's sleep-rounding behavior. Timing
// harness overhead per sub-job is ~100 ns (two steady_clock::now()
// calls + atomic add) — well under 1% of the 1000 µs busy-spin, so
// a wide ±30% acceptance window comfortably covers worker
// descheduling on loaded CI hardware while still failing if the
// signal is missing.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <chrono>

namespace {

class BusySpinSystem : public threadmaxx::ISystem {
public:
    BusySpinSystem(const char*               n,
                   std::chrono::microseconds work,
                   std::uint32_t             subjobs)
        : name_(n), work_(work), subjobs_(subjobs) {}

    const char* name() const noexcept override { return name_; }
    threadmaxx::ComponentSet reads()  const noexcept override {
        return threadmaxx::ComponentSet::none();
    }
    threadmaxx::ComponentSet writes() const noexcept override {
        return threadmaxx::ComponentSet::none();
    }

    void update(threadmaxx::SystemContext& ctx) override {
        const auto work = work_;
        // grain=1 + count=subjobs_ → exactly subjobs_ sub-jobs, one
        // row each. The engine times the lambda only (count_down /
        // latch bookkeeping is NOT charged to avgSubJobMicros).
        ctx.parallelFor(subjobs_, /*grain*/ 1,
            [work](threadmaxx::Range, threadmaxx::CommandBuffer&) {
                const auto end = std::chrono::steady_clock::now() + work;
                while (std::chrono::steady_clock::now() < end) {
                    // busy-spin; deterministic vs sleep_for's HZ jitter.
                }
            });
    }

private:
    const char*               name_;
    std::chrono::microseconds work_;
    std::uint32_t             subjobs_;
};

class QuietSystem : public threadmaxx::ISystem {
public:
    const char* name() const noexcept override { return "quiet"; }
    threadmaxx::ComponentSet reads()  const noexcept override {
        return threadmaxx::ComponentSet::none();
    }
    threadmaxx::ComponentSet writes() const noexcept override {
        return threadmaxx::ComponentSet::none();
    }
    void update(threadmaxx::SystemContext&) override {}
};

} // namespace

int main() {
    using namespace threadmaxx;

    Config cfg;
    cfg.sleepToPace = false;
    cfg.workerCount = 4;
    Engine engine(cfg);
    struct G : IGame {
        void onSetup(Engine&, World&, CommandBuffer&) override {}
    } game;
    engine.initialize(game);

    constexpr std::uint32_t kSubJobs        = 4;
    constexpr std::int64_t  kTruthMicros    = 1000;  // 1 ms busy-spin
    engine.registerSystem(std::make_unique<BusySpinSystem>(
        "busy", std::chrono::microseconds(kTruthMicros), kSubJobs));
    engine.registerSystem(std::make_unique<QuietSystem>());

    // 40 ticks > 30-sample warm-up. EWMA with 1/16 decay reaches
    // ~98% of steady state after ~32 samples; first-sample init makes
    // convergence essentially immediate for a constant truth.
    for (int i = 0; i < 40; ++i) engine.step();

    const auto stats = engine.systemStats();
    CHECK_EQ(stats.size(), std::size_t{2});

    // --- BusySpinSystem ---------------------------------------------
    // subJobsLastStep tracks the per-step count, not lifetime totals.
    CHECK_EQ(stats[0].subJobsLastStep, kSubJobs);

    // The two semantically-aligned counts agree under the current
    // implementation (every parallelFor sub-job IS a JobSystem job),
    // but the test asserts both fields independently so a future
    // divergence would surface here.
    CHECK_EQ(stats[0].jobsSubmittedLastStep,
             static_cast<std::uint64_t>(kSubJobs));

    // avgSubJobMicros should converge near the 1000 µs truth. ±30%
    // window covers worker descheduling jitter (one-sided: the busy-
    // spin can over-run but cannot under-run). If we ever see this
    // window violated on healthy hardware, suspect the timing wrap
    // around userFn(...) drifted.
    const double avgUs = stats[0].avgSubJobMicros;
    CHECK(avgUs >= static_cast<double>(kTruthMicros) * 0.70);
    CHECK(avgUs <= static_cast<double>(kTruthMicros) * 1.30);

    // --- QuietSystem ------------------------------------------------
    // No parallelFor → both new fields stay at zero.
    CHECK_EQ(stats[1].subJobsLastStep, std::uint32_t{0});
    CHECK_EQ(stats[1].avgSubJobMicros, 0.0);

    // --- Persistence + per-step reset --------------------------------
    // One more step keeps avgSubJobMicros pinned (EWMA persists) while
    // subJobsLastStep still equals the per-step count.
    const double avgBefore = stats[0].avgSubJobMicros;
    engine.step();
    const auto stats2 = engine.systemStats();
    CHECK_EQ(stats2[0].subJobsLastStep, kSubJobs);
    // EWMA absorbs a single new sample at 1/16 weight — still within
    // ±30% of the prior reading.
    CHECK(stats2[0].avgSubJobMicros >= avgBefore * 0.70);
    CHECK(stats2[0].avgSubJobMicros <= avgBefore * 1.30);

    engine.shutdown();
    EXIT_WITH_RESULT();
}
