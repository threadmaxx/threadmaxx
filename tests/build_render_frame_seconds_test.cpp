// §3.6.5 batch 15a — `SystemStats::buildRenderFrameSeconds`.
//
// The engine times each system's `buildRenderFrame` hook so a slow
// render-prep step is visible in `systemStats()` without being
// silently bundled into `lastStepSeconds`. Asserts:
//
//   (1) For a system with no hook (default empty `buildRenderFrame`),
//       the recorded duration is non-negative and small.
//   (2) For a system whose hook does meaningful work, the duration
//       is strictly positive.
//   (3) Per-system durations are independent of each other.
//   (4) Counter resets every step (no carryover from prior tick).

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <chrono>
#include <thread>

namespace {

class SleepyRenderer : public threadmaxx::IRenderer {
public:
    bool initialize() override { return true; }
    void submitFrame(const threadmaxx::RenderFrame&) override {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    void shutdown() override {}
};

using namespace threadmaxx;

class NoHookSystem : public ISystem {
public:
    const char* name() const noexcept override { return "no-hook"; }
    ComponentSet reads()  const noexcept override { return ComponentSet::none(); }
    ComponentSet writes() const noexcept override { return ComponentSet::none(); }
    void update(SystemContext&) override {}
};

class HeavyHookSystem : public ISystem {
public:
    const char* name() const noexcept override { return "heavy-hook"; }
    ComponentSet reads()  const noexcept override { return ComponentSet::none(); }
    ComponentSet writes() const noexcept override { return ComponentSet::none(); }
    void update(SystemContext&) override {}
    void buildRenderFrame(RenderFrameBuilder& b) override {
        // ~1ms of busywork. Sleep is OK here — we're measuring the
        // engine's timer plumbing, not benchmarking the system.
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        b.addDebugLine(DebugLine{});
    }
};

} // namespace

int main() {
    Config cfg; cfg.sleepToPace = false; cfg.workerCount = 1;
    Engine engine(cfg);

    struct G : IGame {
        void onSetup(Engine& eng, World&, CommandBuffer&) override {
            eng.registerSystem(std::make_unique<NoHookSystem>());
            eng.registerSystem(std::make_unique<HeavyHookSystem>());
        }
    } g;
    CHECK(engine.initialize(g));
    engine.step();

    const auto stats = engine.systemStats();
    CHECK_EQ(stats.size(), std::size_t{2});

    // (1) The no-hook system records a non-negative value (could be
    // 0 on a fast machine — the timer's resolution is OS-dependent).
    CHECK(stats[0].buildRenderFrameSeconds >= 0.0);
    CHECK(stats[0].buildRenderFrameSeconds < 0.001);

    // (2) Heavy-hook system records >= 1ms (we slept 2ms; allow
    // slack for low-res clocks).
    CHECK(stats[1].buildRenderFrameSeconds > 0.0005);

    // (4) After a paused step, the counter resets to 0.
    engine.setPaused(true);
    engine.step();
    const auto pausedStats = engine.systemStats();
    CHECK_EQ(pausedStats[0].buildRenderFrameSeconds, 0.0);
    CHECK_EQ(pausedStats[1].buildRenderFrameSeconds, 0.0);

    engine.setPaused(false);
    engine.step();
    const auto resumedStats = engine.systemStats();
    // Heavy hook ran again; counter should be > 0 again.
    CHECK(resumedStats[1].buildRenderFrameSeconds > 0.0005);

    // §3.8 batch 32 — `EngineStats::engineBuildRenderFrameSeconds` and
    // `renderSubmitSeconds` split. Attach a renderer that sleeps in
    // submitFrame and assert the split is reported and bounded by the
    // total step time.
    {
        Config cfg2; cfg2.sleepToPace = false; cfg2.workerCount = 1;
        Engine eng2(cfg2);
        SleepyRenderer renderer;
        eng2.setRenderer(&renderer);
        struct G2 : IGame {
            void onSetup(Engine& e, World&, CommandBuffer&) override {
                e.registerSystem(std::make_unique<HeavyHookSystem>());
            }
        } g2;
        CHECK(eng2.initialize(g2));
        eng2.step();

        const auto& es = eng2.stats();
        // Both fields are reported.
        CHECK(es.engineBuildRenderFrameSeconds >= 0.0);
        CHECK(es.renderSubmitSeconds          >= 0.0);
        // Renderer slept ~2ms; submit time should reflect that.
        CHECK(es.renderSubmitSeconds > 0.0005);
        // Both fit inside the total step time (with a small margin
        // for clock noise).
        CHECK(es.engineBuildRenderFrameSeconds <= es.lastStepSeconds + 0.001);
        CHECK(es.renderSubmitSeconds          <= es.lastStepSeconds + 0.001);

        eng2.shutdown();
    }

    engine.shutdown();
    EXIT_WITH_RESULT();
}
