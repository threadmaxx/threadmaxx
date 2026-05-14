// §3.1 SystemStats::waitSeconds and peakQueueDepth. A system that
// submits jobs has non-zero wait time and a non-zero peak depth; a
// system that does nothing has both zeroed.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <chrono>
#include <thread>

namespace {

class BusySystem : public threadmaxx::ISystem {
public:
    const char* name() const noexcept override { return "busy"; }
    threadmaxx::ComponentSet reads()  const noexcept override { return threadmaxx::ComponentSet::none(); }
    threadmaxx::ComponentSet writes() const noexcept override { return threadmaxx::ComponentSet::none(); }
    void update(threadmaxx::SystemContext& ctx) override {
        // Submit 8 jobs that each sleep ~1ms — gives the JobSystem queue
        // something to back up while the system thread sits in done.wait().
        ctx.parallelFor(8, /*grain*/ 1,
            [](threadmaxx::Range, threadmaxx::CommandBuffer&) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            });
    }
};

class QuietSystem : public threadmaxx::ISystem {
public:
    const char* name() const noexcept override { return "quiet"; }
    threadmaxx::ComponentSet reads()  const noexcept override { return threadmaxx::ComponentSet::none(); }
    threadmaxx::ComponentSet writes() const noexcept override { return threadmaxx::ComponentSet::none(); }
    void update(threadmaxx::SystemContext&) override {}
};

} // namespace

int main() {
    using namespace threadmaxx;

    Config cfg; cfg.sleepToPace = false; cfg.workerCount = 2;
    Engine engine(cfg);
    struct G : IGame {
        void onSetup(Engine&, World&, CommandBuffer&) override {}
    } g;
    engine.initialize(g);
    engine.registerSystem(std::make_unique<BusySystem>());
    engine.registerSystem(std::make_unique<QuietSystem>());

    engine.step();
    const auto stats = engine.systemStats();
    CHECK_EQ(stats.size(), std::size_t{2});

    // BusySystem submitted 8 jobs of ~1ms each; on 2 workers that's
    // ~4ms wall — even with significant scheduler noise the system
    // should report waitSeconds > 0.
    CHECK(stats[0].waitSeconds > 0.0);
    CHECK(stats[0].peakQueueDepth > 0u);
    // waitSeconds is a subset of update time.
    CHECK(stats[0].waitSeconds <= stats[0].lastUpdateSeconds + 1e-6);

    // QuietSystem submitted no jobs.
    CHECK_EQ(stats[1].waitSeconds, 0.0);
    CHECK_EQ(stats[1].peakQueueDepth, std::uint32_t{0});

    // After a fresh step, last-step fields reset (lifetime totals already
    // covered by other tests; we just check the reset).
    engine.step();
    const auto stats2 = engine.systemStats();
    // Busy still has waitSeconds > 0 (steady state).
    CHECK(stats2[0].waitSeconds > 0.0);
    // Quiet remains zero.
    CHECK_EQ(stats2[1].waitSeconds, 0.0);
    CHECK_EQ(stats2[1].peakQueueDepth, std::uint32_t{0});

    engine.shutdown();
    EXIT_WITH_RESULT();
}
