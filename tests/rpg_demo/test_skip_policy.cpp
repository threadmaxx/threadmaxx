// §3.11.5 batch D5 — verifies `Engine::setTickBudget` +
// `SkipPolicy::Budget` actually drop the cosmetic systems
// (`DebugOverlaySystem`, `DayNightSystem`, `HudSystem`) when a tick
// runs over the configured per-frame budget. Also verifies that the
// `BudgetExceeded` event channel surfaces alerts and HudSystem's
// subscription tallies them.
//
// To force a budget breach without spawning 60k entities (slow in
// CI), the test uses a microscopic budget (10µs/tick) and the
// default scene. Even a 153-entity tick easily blows that, so every
// step should produce skips.

#include "DemoTestHarness.hpp"

#include <threadmaxx/EventChannel.hpp>
#include <threadmaxx/SkipPolicy.hpp>
#include <threadmaxx/Telemetry.hpp>

#include <cstdio>

int main() {
    using namespace rpg;
    using namespace rpg::testing;
    using namespace threadmaxx;

    resetEdges();
    Config cfg;
    cfg.sleepToPace = false;
    cfg.workerCount = 2;
    Engine engine(cfg);
    DemoGame game;
    // §3.11.5 batch D5 — flip stress mode OFF (we want the small
    // scene), but install a 10-µs tick budget so a normal tick blows
    // it. The setup runs through `engine.initialize` below.
    game.worldState().stressMode = false;
    CHECK(engine.initialize(game));

    // §3.11.5 batch D5 — pre-warm `BudgetExceeded` on the sim thread
    // so HudSystem's later `subscribeScoped` already targets the
    // initialized channel (without the warm-up the first emit could
    // race with a stranger subscribe path).
    (void)engine.events<SystemSkipped>();
    (void)engine.events<BudgetExceeded>();

    // Force the budget below typical tick cost, even for a tiny scene.
    engine.setTickBudget(0.0000001);  // 100 ns — effectively impossible
    engine.setSkipPolicy(SkipPolicy::Budget);

    // Manually attach a FrameBudgetWatcher so `BudgetExceeded` events
    // actually emit (DemoGame::onSetup only registers it in stress
    // mode).
    engine.registerSystem(std::make_unique<FrameBudgetWatcher>(
        &engine, 0.0000001));

    for (int i = 0; i < 30; ++i) engine.step();

    std::printf("[test_skip_policy] OVER=%u skips[hud=%u,ovr=%u,dn=%u]\n",
                game.worldState().budgetExceededCount,
                game.worldState().totalSkippedHud,
                game.worldState().totalSkippedOverlay,
                game.worldState().totalSkippedDayNight);

    // With a 100ns budget every tick is over → many BudgetExceeded
    // events expected. At least one tick should have fired the
    // FrameBudgetWatcher (which emits a BudgetExceeded event from
    // its postStep).
    CHECK(game.worldState().budgetExceededCount > 0);
    // Cosmetic systems get skipped on over-budget ticks. The skip
    // policy runs the early waves and then bails on later
    // skippable() systems once `overBudget_` flips. The exact
    // tally depends on how many cosmetic systems land in late waves,
    // but at least ONE skip should fire across 30 ticks.
    const auto totalSkips = game.worldState().totalSkippedHud
                          + game.worldState().totalSkippedOverlay
                          + game.worldState().totalSkippedDayNight;
    CHECK(totalSkips > 0);

    EXIT_WITH_RESULT();
}
