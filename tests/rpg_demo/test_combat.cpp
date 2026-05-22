// §3.11 batch D-audit — combat smoke test.
//
// Subclasses `DemoGame::onSetup` to spawn a controlled hostile NPC at
// a known position relative to the player, then injects the F-key
// edge and verifies the NPC takes damage. This test exists because a
// manual playtest after batch D1 showed combat never connecting; the
// goal here is to capture WHY in a reproducible test before fixing.
//
// Expected outcome with the post-fix code:
//   tick 0: player swings → CombatSystem fires DamageDealt event.
//   tick 1: DamageSystem applies cb.setHealth → NPC HP drops.
//
// Failure modes the test pinpoints:
//   - `takeEdges()` consuming all edges (kEdgeAttack bit never seen).
//   - Player Transform.orientation not updated → sword extends in
//     world +Z direction regardless of player facing.
//   - Sword tip computed at local +Z * length (BEHIND player) instead
//     of local -Z * length (in front).

#include "DemoTestHarness.hpp"

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/UserComponent.hpp>
#include <threadmaxx/World.hpp>

#include <cstdio>

namespace {

using namespace rpg;
using namespace rpg::testing;
using namespace threadmaxx;

/// Spawns a single hostile NPC right next to the player so the
/// guaranteed near-hit pass in `CombatSystem` lands a hit on the
/// first swing.
///
/// 2026-05-22 (round 9, voxel pivot) — pre-pivot the test placed
/// the NPC at the sword tip's expected world coordinate. With the
/// voxel terrain that's brittle: the NPC's Y depends on
/// `heightAt(npcXZ)` (via TerrainAttachSystem), which may not
/// align with the sword tip's swing arc. Instead we place the NPC
/// 0.5 m to the right of the player (same column → same heightAt →
/// no Y mismatch after snap) so the always-on near-hit pass fires.
struct CombatTestGame : DemoGame {
    EntityHandle testNpc;

    void onSetup(Engine& engine, World& w, CommandBuffer& seed) override {
        DemoGame::onSetup(engine, w, seed);
        const auto& ws = worldState();
        const float playerY = ws.heightmap
            ? ws.heightmap->heightAt(0.0f, 0.0f) + 1.0f
            : 1.0f;
        testNpc = engine.reserveEntityHandle();
        Bundle b{};
        // Co-located with the player (same heightmap cell), 0.5 m
        // to the right. Within `kNearHitRadius = 1.4 m`.
        b.transform.position = {0.5f, playerY, 0.0f};
        b.transform.scale    = {0.8f, 1.6f, 0.8f};
        b.faction.id         = kFactionHostile;
        b.boundingVolume     = BoundingVolume{
            {b.transform.position.x - 0.4f,
             b.transform.position.y - 0.4f,
             b.transform.position.z - 0.4f},
            {b.transform.position.x + 0.4f,
             b.transform.position.y + 0.4f,
             b.transform.position.z + 0.4f}};
        b.health             = Health{kHostileMaxHP, kHostileMaxHP};
        b.initialMask        = ComponentSet{
            Component::Transform,
            Component::Velocity,
            Component::Faction,
            Component::BoundingVolume,
            Component::Health,
        };
        seed.spawnBundle(testNpc, b);

        CubeRender cr;
        cr.color[0] = 0.95f; cr.color[1] = 0.20f;
        cr.color[2] = 0.20f; cr.color[3] = 1.0f;
        cr.scale    = 1.0f;
        addUserComponent(seed, ids().cubeRender, testNpc, cr);

        NpcState ns;
        ns.aoiRadius = 0.01f;  // tiny so it doesn't auto-engage and walk
        addUserComponent(seed, ids().npcState, testNpc, ns);
    }
};

} // namespace

int main() {
    resetEdges();
    Config cfg;
    cfg.sleepToPace = false;
    cfg.workerCount = 2;
    Engine engine(cfg);
    CombatTestGame game;
    CHECK(engine.initialize(game));

    // Pump one tick to let the seed CommandBuffer commit (spawns the
    // player + sword + 50 NPCs + 100 pickups + the test NPC).
    engine.step();
    CHECK(engine.world().alive(game.testNpc));

    // Snapshot starting HP.
    const Health* hp0 = engine.world().tryGetHealth(game.testNpc);
    CHECK(hp0 != nullptr);
    const float startingHp = hp0->current;
    CHECK_EQ(startingHp, kHostileMaxHP);

    // Trigger an attack.
    injectEdge(kEdgeAttack);

    // Step enough ticks for the damage to propagate:
    //   tick 1: PlayerInputSystem consumes edge, sets swingTimer;
    //           CombatSystem fires DamageDealt; events drain at tick end.
    //   tick 2: DamageSystem.preStep drains, update applies setHealth.
    //   tick 3+: HP visible.
    for (int i = 0; i < 4; ++i) engine.step();

    const Health* hp1 = engine.world().tryGetHealth(game.testNpc);
    CHECK(hp1 != nullptr);
    std::printf("[test_combat] HP before=%.1f after=%.1f expected drop=%.1f\n",
                startingHp, hp1->current, kSwordDamage);
    CHECK(hp1->current < startingHp);
    CHECK_EQ(hp1->current, startingHp - kSwordDamage);
    EXIT_WITH_RESULT();
}
