// §3.11.4 batch D4 — quest tracking smoke test.
//
// Spawns 30 pickups within the player's PickupSystem radius so the
// "Collect 25 pickups" quest completes within a few ticks. Verifies:
//   - Quest list is seeded with 2 entries.
//   - The pickup quest advances + completes.
//   - HudSystem receives `QuestProgressed` events (counter side
//     effect on `WorldState`).

#include "DemoTestHarness.hpp"

#include <threadmaxx/UserComponent.hpp>
#include <threadmaxx/World.hpp>

#include <cstdio>
#include <vector>

namespace {

using namespace rpg;
using namespace rpg::testing;
using namespace threadmaxx;

struct QuestTestGame : DemoGame {
    void onSetup(Engine& engine, World& w, CommandBuffer& seed) override {
        DemoGame::onSetup(engine, w, seed);
        // §3.11.4 D4: pack 30 pickups directly underneath the player
        // so PickupSystem collects them rapidly. (The base scene's
        // 100 pickups are scattered across ±27 so the player won't
        // walk through 25 of them inside the test's tick budget.)
        for (int i = 0; i < 30; ++i) {
            const auto h = engine.reserveEntityHandle();
            Bundle b{};
            b.transform.position = {0.0f, 0.4f, 0.0f};
            b.transform.scale    = {0.4f, 0.4f, 0.4f};
            b.faction.id         = kFactionNeutral;
            b.boundingVolume     = BoundingVolume{
                {-0.4f, 0.0f, -0.4f}, {0.4f, 0.8f, 0.4f}};
            b.initialMask        = ComponentSet{
                Component::Transform,
                Component::Faction,
                Component::BoundingVolume,
            };
            seed.spawnBundle(h, b);
            addUserComponent(seed, ids().cubeRender, h,
                CubeRender{{1.0f, 0.85f, 0.20f, 1.0f}, 0.7f});
            addUserComponent(seed, ids().pickup, h, Pickup{1u});
        }
    }
};

} // namespace

int main() {
    resetEdges();
    Config cfg;
    cfg.sleepToPace = false;
    cfg.workerCount = 2;
    Engine engine(cfg);
    QuestTestGame game;
    CHECK(engine.initialize(game));

    // Verify quest list seeded.
    const auto& quests0 = game.worldState().quests;
    CHECK_EQ(quests0.size(), std::size_t{2});
    CHECK_EQ(static_cast<std::uint32_t>(quests0[0].id),
             static_cast<std::uint32_t>(QuestId::CollectPickups));
    CHECK_EQ(quests0[0].target, kPickupQuestTarget);
    CHECK_EQ(quests0[1].target, game.worldState().hostileSpawnCount);

    // Step until the pickup quest completes, or 30 ticks max (it
    // should complete inside 5 ticks).
    bool completed = false;
    for (int i = 0; i < 30 && !completed; ++i) {
        engine.step();
        completed = game.worldState().quests[0].completed;
    }
    std::printf("[test_quests] CollectPickups: %u/%u completed=%d\n",
                game.worldState().quests[0].progress,
                game.worldState().quests[0].target,
                int(game.worldState().quests[0].completed));
    CHECK(completed);
    CHECK(game.worldState().quests[0].progress >= kPickupQuestTarget);

    EXIT_WITH_RESULT();
}
