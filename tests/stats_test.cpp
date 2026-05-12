// Engine stats: counts increase across steps, alive count tracks size().

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

namespace {

class TickJobSystem : public threadmaxx::ISystem {
    const char* name() const noexcept override { return "tickjob"; }
    void update(threadmaxx::SystemContext& ctx) override {
        // 8 chunks, each emits one setUserData on entity 0 if it exists.
        const auto entities = ctx.world().entities();
        if (entities.empty()) return;
        const auto e = entities[0];
        ctx.parallelFor(8, /*grain*/ 1,
            [e](threadmaxx::Range, threadmaxx::CommandBuffer& cb) {
                cb.setUserData(e, threadmaxx::UserData{42});
            });
    }
};

class StatsGame : public threadmaxx::IGame {
    void onSetup(threadmaxx::Engine& engine, threadmaxx::World&,
                 threadmaxx::CommandBuffer& seed) override {
        engine.registerSystem(std::make_unique<TickJobSystem>());
        seed.spawn({});
    }
};

} // namespace

int main() {
    threadmaxx::Config cfg;
    cfg.sleepToPace = false;
    cfg.workerCount = 2;
    threadmaxx::Engine engine(cfg);
    StatsGame game;
    CHECK(engine.initialize(game));

    // Before any step.
    auto s0 = engine.stats();
    CHECK_EQ(s0.totalTicks, std::uint64_t{0});
    CHECK_EQ(s0.totalJobsSubmitted, std::uint64_t{0});

    engine.step();
    auto s1 = engine.stats();
    CHECK_EQ(s1.tick, std::uint64_t{1});
    CHECK_EQ(s1.totalTicks, std::uint64_t{1});
    // 8 parallelFor chunks submitted.
    CHECK_EQ(s1.jobsSubmittedLastStep, std::uint64_t{8});
    CHECK_EQ(s1.totalJobsSubmitted, std::uint64_t{8});
    // 8 setUserData commands committed (one per chunk).
    CHECK_EQ(s1.commandsCommittedLastStep, std::uint64_t{8});
    CHECK_EQ(s1.aliveEntities, std::size_t{1});
    CHECK(s1.lastStepSeconds >= 0.0);
    CHECK(s1.avgStepSeconds >= 0.0);

    // Run more steps; totals accumulate.
    for (int i = 0; i < 9; ++i) engine.step();
    auto s10 = engine.stats();
    CHECK_EQ(s10.totalTicks, std::uint64_t{10});
    CHECK_EQ(s10.totalJobsSubmitted, std::uint64_t{80});
    CHECK_EQ(s10.totalCommandsCommitted, std::uint64_t{80});

    engine.shutdown();
    EXIT_WITH_RESULT();
}
