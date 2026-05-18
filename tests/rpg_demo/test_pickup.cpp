// §3.11 batch D-audit — pickup collection smoke.
//
// Spawns a pickup right under the player and verifies that
// PickupSystem flips DisabledTag on it within a few ticks AND
// increments PlayerState.pickups.

#include "DemoTestHarness.hpp"

#include <threadmaxx/UserComponent.hpp>
#include <threadmaxx/World.hpp>

#include <cstdio>

namespace {

using namespace rpg;
using namespace rpg::testing;
using namespace threadmaxx;

struct PickupTestGame : DemoGame {
    EntityHandle testPickup;
    void onSetup(Engine& engine, World& w, CommandBuffer& seed) override {
        DemoGame::onSetup(engine, w, seed);
        testPickup = engine.reserveEntityHandle();
        Bundle b{};
        b.transform.position = {0.0f, 0.4f, 0.0f};  // on top of player
        b.transform.scale    = {0.4f, 0.4f, 0.4f};
        b.faction.id         = kFactionNeutral;
        b.boundingVolume     = BoundingVolume{
            {-0.4f, 0.0f, -0.4f}, {0.4f, 0.8f, 0.4f}};
        b.initialMask        = ComponentSet{
            Component::Transform,
            Component::Faction,
            Component::BoundingVolume,
        };
        seed.spawnBundle(testPickup, b);
        addUserComponent(seed, ids().cubeRender, testPickup,
            CubeRender{{1.0f, 0.85f, 0.20f, 1.0f}, 0.7f});
        addUserComponent(seed, ids().pickup, testPickup, Pickup{9u});
    }
};

} // namespace

int main() {
    resetEdges();
    Config cfg;
    cfg.sleepToPace = false;
    cfg.workerCount = 2;
    Engine engine(cfg);
    PickupTestGame game;
    CHECK(engine.initialize(game));

    // §3.11 batch D-audit: PickupSystem runs every tick and the test
    // pickup is INSIDE the kPickupRadius (1.2 units) of the player at
    // spawn time, so collection fires on the very first step. We
    // just verify the post-collection state.
    for (int i = 0; i < 5; ++i) engine.step();

    // The test pickup is right under the player; it should have been
    // flipped to DisabledTag by PickupSystem.
    const bool disabled =
        engine.world().hasTag(game.testPickup, Component::DisabledTag);
    std::printf("[test_pickup] disabled=%d\n", int(disabled));
    CHECK(disabled);

    // Player's PlayerState.pickups should reflect the collection. We
    // injected the test pickup with value=9.
    const auto player = game.worldState().player;
    const PlayerState* ps =
        user::tryGet<PlayerState>(engine.world(), game.ids().playerState, player);
    CHECK(ps != nullptr);
    std::printf("[test_pickup] player.pickups=%u\n", ps->pickups);
    CHECK(ps->pickups >= 9u);
    EXIT_WITH_RESULT();
}
