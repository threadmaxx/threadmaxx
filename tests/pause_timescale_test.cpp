// §3.4 Pause + time-scale: setPaused freezes tick advancement; setTimeScale
// scales the dt seen by systems but not the wall-clock tick rate.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <cmath>

namespace {

// Records the dt the system saw on its most recent update.
class DtRecorder : public threadmaxx::ISystem {
public:
    double lastDt = -1.0;
    int updateCount = 0;
    const char* name() const noexcept override { return "dt_recorder"; }
    void update(threadmaxx::SystemContext& ctx) override {
        lastDt = ctx.dt();
        updateCount++;
    }
    threadmaxx::ComponentSet reads()  const noexcept override { return threadmaxx::ComponentSet::none(); }
    threadmaxx::ComponentSet writes() const noexcept override { return threadmaxx::ComponentSet::none(); }
};

bool nearly(double a, double b, double eps = 1e-9) {
    return std::fabs(a - b) <= eps;
}

} // namespace

int main() {
    using namespace threadmaxx;

    // Test 1: default scale = 1.0 → dt == fixedStepSeconds.
    {
        Config cfg; cfg.sleepToPace = false; cfg.workerCount = 1;
        cfg.fixedStepSeconds = 1.0 / 60.0;
        Engine e(cfg);
        struct G : IGame {
            DtRecorder* p = nullptr;
            void onSetup(Engine& eng, World&, CommandBuffer&) override {
                auto s = std::make_unique<DtRecorder>();
                p = s.get();
                eng.registerSystem(std::move(s));
            }
        } g;
        CHECK(e.initialize(g));
        e.step();
        CHECK(nearly(g.p->lastDt, 1.0 / 60.0));
        e.shutdown();
    }

    // Test 2: setTimeScale halves and doubles dt; tick and simTime unaffected.
    {
        Config cfg; cfg.sleepToPace = false; cfg.workerCount = 1;
        cfg.fixedStepSeconds = 1.0 / 60.0;
        Engine e(cfg);
        struct G : IGame {
            DtRecorder* p = nullptr;
            void onSetup(Engine& eng, World&, CommandBuffer&) override {
                auto s = std::make_unique<DtRecorder>();
                p = s.get();
                eng.registerSystem(std::move(s));
            }
        } g;
        CHECK(e.initialize(g));

        e.setTimeScale(0.5);
        e.step();
        CHECK(nearly(g.p->lastDt, (1.0 / 60.0) * 0.5));
        CHECK_EQ(e.tick(), std::uint64_t{1});
        CHECK(nearly(e.simulationTime(), 1.0 / 60.0));

        e.setTimeScale(2.0);
        e.step();
        CHECK(nearly(g.p->lastDt, (1.0 / 60.0) * 2.0));
        CHECK_EQ(e.tick(), std::uint64_t{2});
        CHECK(nearly(e.simulationTime(), 2.0 / 60.0));

        // Negative scale is clamped to zero.
        e.setTimeScale(-1.0);
        e.step();
        CHECK(nearly(g.p->lastDt, 0.0));

        e.shutdown();
    }

    // Test 3: setPaused freezes tick and update().
    {
        Config cfg; cfg.sleepToPace = false; cfg.workerCount = 1;
        Engine e(cfg);
        struct G : IGame {
            DtRecorder* p = nullptr;
            void onSetup(Engine& eng, World&, CommandBuffer&) override {
                auto s = std::make_unique<DtRecorder>();
                p = s.get();
                eng.registerSystem(std::move(s));
            }
        } g;
        CHECK(e.initialize(g));

        e.step();
        const auto tickAfterOne = e.tick();
        const auto updatesAfterOne = g.p->updateCount;

        e.setPaused(true);
        e.step();
        e.step();
        e.step();
        // Tick must not have advanced while paused; update() not called.
        CHECK_EQ(e.tick(), tickAfterOne);
        CHECK_EQ(g.p->updateCount, updatesAfterOne);
        CHECK(e.paused());

        e.setPaused(false);
        e.step();
        CHECK_EQ(e.tick(), tickAfterOne + 1);
        CHECK_EQ(g.p->updateCount, updatesAfterOne + 1);

        e.shutdown();
    }

    EXIT_WITH_RESULT();
}
