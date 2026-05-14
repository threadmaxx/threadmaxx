// §3.1 JobSystemStats::jobDurationHistogram: per-job durations land in
// the right log2-µs bins; aggregation across workers preserves totals.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <chrono>
#include <thread>

namespace {

class TimingSystem : public threadmaxx::ISystem {
public:
    const char* name() const noexcept override { return "timing"; }
    threadmaxx::ComponentSet reads()  const noexcept override { return threadmaxx::ComponentSet::none(); }
    threadmaxx::ComponentSet writes() const noexcept override { return threadmaxx::ComponentSet::none(); }
    void update(threadmaxx::SystemContext& ctx) override {
        // 4 jobs that each sleep ~2 ms (lands in bin 11 = [1024µs, 2048µs)
        // or bin 12 = [2048µs, 4096µs)). The exact bin can be one or the
        // other depending on jitter; check that *something* lands in the
        // expected range.
        ctx.parallelFor(4, /*grain*/ 1,
            [](threadmaxx::Range, threadmaxx::CommandBuffer&) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            });
    }
};

} // namespace

int main() {
    using namespace threadmaxx;

    // Test 1: empty histogram on a fresh engine before any jobs run.
    {
        Config cfg; cfg.sleepToPace = false; cfg.workerCount = 2;
        Engine engine(cfg);
        struct G : IGame {
            void onSetup(Engine&, World&, CommandBuffer&) override {}
        } g;
        engine.initialize(g);
        const auto js = engine.jobSystemStats();
        std::uint64_t total = 0;
        for (auto b : js.jobDurationHistogram) total += b;
        CHECK_EQ(total, std::uint64_t{0});
        engine.shutdown();
    }

    // Test 2: a system that submits 4 jobs each sleeping ~2 ms ends up
    // with 4 hits in the histogram, concentrated around bins 10-13
    // (~1ms-8ms range).
    {
        Config cfg; cfg.sleepToPace = false; cfg.workerCount = 2;
        Engine engine(cfg);
        struct G : IGame {
            void onSetup(Engine&, World&, CommandBuffer&) override {}
        } g;
        engine.initialize(g);
        engine.registerSystem(std::make_unique<TimingSystem>());
        engine.step();

        const auto js = engine.jobSystemStats();
        std::uint64_t total = 0;
        for (auto b : js.jobDurationHistogram) total += b;
        CHECK_EQ(total, std::uint64_t{4});

        // Most hits should be in bins 10-14 (roughly 1ms to 32ms). Allow
        // slack for OS scheduler noise.
        std::uint64_t midRange = 0;
        for (std::size_t i = 9; i <= 14 && i < kJobDurationHistogramBins; ++i) {
            midRange += js.jobDurationHistogram[i];
        }
        CHECK(midRange >= std::uint64_t{3});  // at least 3 of 4 land in [512µs, 32ms)

        engine.shutdown();
    }

    // Test 3: counts accumulate across steps.
    {
        Config cfg; cfg.sleepToPace = false; cfg.workerCount = 2;
        Engine engine(cfg);
        struct G : IGame {
            void onSetup(Engine&, World&, CommandBuffer&) override {}
        } g;
        engine.initialize(g);
        engine.registerSystem(std::make_unique<TimingSystem>());
        engine.step();
        engine.step();
        const auto js = engine.jobSystemStats();
        std::uint64_t total = 0;
        for (auto b : js.jobDurationHistogram) total += b;
        CHECK_EQ(total, std::uint64_t{8});
        engine.shutdown();
    }

    EXIT_WITH_RESULT();
}
