// Per-system stats: name preserved, per-system jobs/commands attributed
// correctly, lifetime totals accumulate, and reset to zero each step.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <chrono>
#include <string_view>

namespace {

// Submits exactly `chunks` parallelFor chunks per step, each chunk emitting
// one setUserData command on entity 0.
class FixedJobSystem : public threadmaxx::ISystem {
public:
    FixedJobSystem(const char* n, std::uint32_t chunks)
        : name_(n), chunks_(chunks) {}

    const char* name() const noexcept override { return name_; }
    void update(threadmaxx::SystemContext& ctx) override {
        const auto entities = ctx.world().entities();
        if (entities.empty()) return;
        const auto e = entities[0];
        ctx.parallelFor(chunks_, /*grain*/ 1,
            [e](threadmaxx::Range, threadmaxx::CommandBuffer& cb) {
                cb.setUserData(e, threadmaxx::UserData{1});
            });
    }
    // Distinct write category so both systems can share a wave.
    threadmaxx::ComponentSet reads() const noexcept override {
        return threadmaxx::ComponentSet{threadmaxx::Component::UserData};
    }
    threadmaxx::ComponentSet writes() const noexcept override {
        return threadmaxx::ComponentSet{threadmaxx::Component::UserData};
    }

private:
    const char* name_;
    std::uint32_t chunks_;
};

// Burns CPU on the sim thread so per-system timing is measurably non-zero
// (avoids flaky CHECK on lastUpdateSeconds > 0 on a fast machine).
class SlowSystem : public threadmaxx::ISystem {
public:
    const char* name() const noexcept override { return "slow"; }
    void update(threadmaxx::SystemContext&) override {
        // ~1ms of busy work — small enough not to bog the suite, large
        // enough to leave a positive lastUpdateSeconds.
        const auto deadline = std::chrono::steady_clock::now()
                            + std::chrono::milliseconds(1);
        volatile std::uint64_t acc = 0;
        while (std::chrono::steady_clock::now() < deadline) {
            for (int i = 0; i < 1000; ++i) acc += static_cast<std::uint64_t>(i);
        }
        (void)acc;
    }
    threadmaxx::ComponentSet reads() const noexcept override {
        return threadmaxx::ComponentSet::none();
    }
    threadmaxx::ComponentSet writes() const noexcept override {
        return threadmaxx::ComponentSet::none();
    }
};

class StatsGame : public threadmaxx::IGame {
public:
    void onSetup(threadmaxx::Engine& engine, threadmaxx::World&,
                 threadmaxx::CommandBuffer& seed) override {
        engine.registerSystem(std::make_unique<FixedJobSystem>("alpha", 3));
        engine.registerSystem(std::make_unique<FixedJobSystem>("beta", 5));
        engine.registerSystem(std::make_unique<SlowSystem>());
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

    // Three systems registered.
    auto ss0 = engine.systemStats();
    CHECK_EQ(ss0.size(), std::size_t{3});
    CHECK(std::string_view(ss0[0].name) == "alpha");
    CHECK(std::string_view(ss0[1].name) == "beta");
    CHECK(std::string_view(ss0[2].name) == "slow");
    // No step run yet — everything zero.
    CHECK_EQ(ss0[0].totalJobsSubmitted,      std::uint64_t{0});
    CHECK_EQ(ss0[0].totalCommandsCommitted,  std::uint64_t{0});

    engine.step();
    auto ss1 = engine.systemStats();
    CHECK_EQ(ss1.size(), std::size_t{3});
    // alpha: 3 chunks → 3 jobs, 3 commands.
    CHECK_EQ(ss1[0].jobsSubmittedLastStep,     std::uint64_t{3});
    CHECK_EQ(ss1[0].commandsCommittedLastStep, std::uint64_t{3});
    CHECK_EQ(ss1[0].totalJobsSubmitted,        std::uint64_t{3});
    CHECK_EQ(ss1[0].totalCommandsCommitted,    std::uint64_t{3});
    // beta: 5 chunks → 5 jobs, 5 commands.
    CHECK_EQ(ss1[1].jobsSubmittedLastStep,     std::uint64_t{5});
    CHECK_EQ(ss1[1].commandsCommittedLastStep, std::uint64_t{5});
    // slow: parallelFor never called → zero jobs/commands, but it ran.
    CHECK_EQ(ss1[2].jobsSubmittedLastStep,     std::uint64_t{0});
    CHECK_EQ(ss1[2].commandsCommittedLastStep, std::uint64_t{0});
    CHECK(ss1[2].lastUpdateSeconds > 0.0);
    // Engine-level totals should match the sum.
    CHECK_EQ(engine.stats().jobsSubmittedLastStep,
             ss1[0].jobsSubmittedLastStep + ss1[1].jobsSubmittedLastStep
           + ss1[2].jobsSubmittedLastStep);
    CHECK_EQ(engine.stats().commandsCommittedLastStep,
             ss1[0].commandsCommittedLastStep + ss1[1].commandsCommittedLastStep
           + ss1[2].commandsCommittedLastStep);

    // Lifetime totals accumulate across steps; per-step counters reset.
    for (int i = 0; i < 9; ++i) engine.step();
    auto ss10 = engine.systemStats();
    CHECK_EQ(ss10[0].totalJobsSubmitted,       std::uint64_t{30});
    CHECK_EQ(ss10[0].totalCommandsCommitted,   std::uint64_t{30});
    CHECK_EQ(ss10[1].totalJobsSubmitted,       std::uint64_t{50});
    CHECK_EQ(ss10[1].totalCommandsCommitted,   std::uint64_t{50});
    CHECK_EQ(ss10[0].jobsSubmittedLastStep,    std::uint64_t{3});
    CHECK_EQ(ss10[1].jobsSubmittedLastStep,    std::uint64_t{5});

    engine.shutdown();
    EXIT_WITH_RESULT();
}
