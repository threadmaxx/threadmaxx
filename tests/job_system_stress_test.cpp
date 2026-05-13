// Stress test for the sharded JobSystem: many jobs across many batches,
// uneven per-job costs (forces the work-stealing path), and high producer
// pressure. The contract under test is just "every submitted job runs
// exactly once and waitIdle terminates" — the only thing the new sharded
// implementation should change is throughput, not semantics.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <atomic>
#include <chrono>

namespace {

// Submits N parallelFor chunks per step, with cost varying per chunk so
// some workers finish first and have to steal. The work is just an atomic
// increment plus a tight loop for "high cost" chunks.
class StressSystem : public threadmaxx::ISystem {
public:
    std::atomic<std::uint64_t> count{0};
    std::uint32_t chunks = 0;

    const char* name() const noexcept override { return "stress"; }
    void update(threadmaxx::SystemContext& ctx) override {
        ctx.parallelFor(chunks, /*grain*/ 1,
            [this](threadmaxx::Range r, threadmaxx::CommandBuffer&) {
                // First few chunks burn extra cycles to create per-chunk
                // cost variance. Total tick budget stays small.
                const bool heavy = (r.begin % 8 == 0);
                if (heavy) {
                    // ~50us of work — small enough that 1000 chunks/step
                    // keep the test fast.
                    const auto deadline = std::chrono::steady_clock::now()
                                        + std::chrono::microseconds(50);
                    volatile std::uint64_t acc = 0;
                    while (std::chrono::steady_clock::now() < deadline) {
                        for (int i = 0; i < 100; ++i) acc += static_cast<std::uint64_t>(i);
                    }
                    (void)acc;
                }
                for (auto i = r.begin; i < r.end; ++i) {
                    count.fetch_add(1, std::memory_order_relaxed);
                }
            });
    }
    threadmaxx::ComponentSet reads()  const noexcept override { return threadmaxx::ComponentSet::none(); }
    threadmaxx::ComponentSet writes() const noexcept override { return threadmaxx::ComponentSet::none(); }
};

class EmptyGame : public threadmaxx::IGame {
    void onSetup(threadmaxx::Engine&, threadmaxx::World&,
                 threadmaxx::CommandBuffer&) override {}
};

} // namespace

int main() {
    threadmaxx::Config cfg;
    cfg.sleepToPace = false;
    cfg.workerCount = 4;
    threadmaxx::Engine engine(cfg);
    EmptyGame game;
    CHECK(engine.initialize(game));

    auto sys = std::make_unique<StressSystem>();
    sys->chunks = 1000;
    auto* ptr = sys.get();
    engine.registerSystem(std::move(sys));

    constexpr int kSteps = 25;
    for (int s = 0; s < kSteps; ++s) {
        ptr->count.store(0, std::memory_order_relaxed);
        engine.step();
        CHECK_EQ(ptr->count.load(), std::uint64_t{1000});
    }

    // Engine-level totals should match the per-step sum exactly. This
    // exercises the outstanding/waitIdle path under many consecutive
    // batches; any decrement-without-notify bug would tend to manifest
    // as a hang during one of the engine.step() calls above.
    CHECK_EQ(engine.stats().totalJobsSubmitted, std::uint64_t{kSteps * 1000});

    engine.shutdown();
    EXIT_WITH_RESULT();
}
