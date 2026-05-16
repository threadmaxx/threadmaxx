// §3.11 batch D-audit — round-trip smoke for SaveLoadSystem.
//
// Boots a headless engine + DemoGame, runs N ticks, fires F5 (sync
// save), F9 (load), then verifies the entity count + player handle
// survive the round-trip. Replaces the original
// `tests/rpg_save_load_test.cpp` which re-implemented the wire format
// inline — this version calls into the actual shipped SaveLoadSystem.

#include "DemoTestHarness.hpp"

#include <threadmaxx/UserComponent.hpp>
#include <threadmaxx/World.hpp>

#include <cstdio>

int main() {
    using namespace rpg;
    using namespace rpg::testing;
    using namespace threadmaxx;

    resetEdges();
    auto fx = makeHeadless();

    // Tick once so the seed CommandBuffer commits.
    fx.engine->step();
    const auto entitiesBefore = fx.engine->world().size();
    CHECK(entitiesBefore > 0);
    CHECK(fx.game->worldState().player.valid());

    // Quick-save (F5). Edge will be consumed by SaveLoadSystem in
    // preStep on the next step().
    injectEdge(kEdgeSaveQuick);
    fx.engine->step();

    // Load (F9). Tear down + respawn from the file. The previously
    // saved entity count should match.
    injectEdge(kEdgeLoadQuick);
    fx.engine->step();
    // Give the commit phase a tick to fully apply the destroy + spawn.
    fx.engine->step();

    const auto entitiesAfter = fx.engine->world().size();
    std::printf("[test_round_trip] before=%zu after=%zu\n",
                entitiesBefore, entitiesAfter);
    CHECK_EQ(entitiesAfter, entitiesBefore);
    CHECK(fx.game->worldState().player.valid());
    CHECK(fx.engine->world().alive(fx.game->worldState().player));
    EXIT_WITH_RESULT();
}
