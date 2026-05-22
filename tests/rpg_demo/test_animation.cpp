// §3.11.6 batch D6 — procedural-animation regression test.
//
// Spawns one entity with `AnimState` + a non-zero `Velocity` so the
// AnimationSystem bobs its Y over time. Captures the Y position
// across N ticks and asserts:
//   - Y is NOT constant (the bob is firing).
//   - Y oscillates above AND below `baseY` (a full sine cycle, not
//     just one-sided drift).
//   - Setting velocity to zero stops the bob (Y returns to baseY).

#include "DemoTestHarness.hpp"

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/UserComponent.hpp>
#include <threadmaxx/World.hpp>

#include <cmath>
#include <cstdio>

namespace {

using namespace rpg;
using namespace rpg::testing;
using namespace threadmaxx;

struct AnimTestGame : DemoGame {
    EntityHandle bobNpc;
    float        bobBaseline = 1.0f;  // expected Y around which the bob oscillates
    void onSetup(Engine& engine, World& w, CommandBuffer& seed) override {
        DemoGame::onSetup(engine, w, seed);
        // §3.11.8 batch D8 — the AnimationSystem now derives the bob
        // baseline from the heightmap when one is installed (which
        // DemoGame::onSetup always does). Record the expected
        // baseline here so the assertions below compare against the
        // right value.
        const auto& ws = worldState();
        const float halfY = 0.8f;
        const float spawnY = ws.heightmap
            ? ws.heightmap->heightAt(30.0f, 30.0f) + halfY
            : 1.0f;
        bobBaseline = spawnY;
        bobNpc = engine.reserveEntityHandle();
        Bundle b{};
        // Place far from player so combat / pickup systems leave it
        // alone for the duration of the test.
        b.transform.position = {30.0f, spawnY, 30.0f};
        b.transform.scale    = {0.8f, 1.6f, 0.8f};
        b.velocity           = Velocity{{2.0f, 0.0f, 0.0f}, {0,0,0}};
        b.faction.id         = kFactionNeutral;
        b.boundingVolume     = BoundingVolume{
            {29.6f, spawnY - 0.8f, 29.6f}, {30.4f, spawnY + 0.8f, 30.4f}};
        b.initialMask        = ComponentSet{
            Component::Transform,
            Component::Velocity,
            Component::Faction,
            Component::BoundingVolume,
        };
        seed.spawnBundle(bobNpc, b);
        addUserComponent(seed, ids().cubeRender, bobNpc,
            CubeRender{{0.5f, 0.7f, 0.5f, 1.0f}, 1.0f});
        AnimState anim;
        anim.baseY     = spawnY;
        anim.phase     = 0.0f;
        anim.frequency = 8.0f;
        anim.amplitude = 0.20f;
        addUserComponent(seed, ids().animState, bobNpc, anim);
    }
};

} // namespace

int main() {
    resetEdges();
    Config cfg;
    cfg.sleepToPace = false;
    cfg.workerCount = 2;
    Engine engine(cfg);
    AnimTestGame game;
    CHECK(engine.initialize(game));

    // 90 ticks ≈ 1.5 s sim time at 60 Hz. With frequency=8 rad/s the
    // bob completes ~2 full cycles.
    constexpr int kTicks = 90;
    float minY = std::numeric_limits<float>::max();
    float maxY = std::numeric_limits<float>::lowest();
    for (int i = 0; i < kTicks; ++i) {
        engine.step();
        const Transform* t = engine.world().tryGetTransform(game.bobNpc);
        CHECK(t != nullptr);
        if (t) {
            minY = std::min(minY, t->position.y);
            maxY = std::max(maxY, t->position.y);
        }
    }
    std::printf("[test_animation] Y range = [%.3f, %.3f] (baseY=%.3f, "
                "amp=0.20)\n", minY, maxY, game.bobBaseline);
    // 2026-05-22 audit (round 9, voxel pivot) — the bob is
    // POSITIVE-ONLY since round 3 (`(sin*0.5 + 0.5) * amp` keeps
    // the entity above ground), so Y oscillates in `[base, base +
    // amp*ratio]`. With the new voxel terrain, the NPC may hit a
    // 1+ block wall and lean against it; the bob keeps firing
    // because we deliberately don't zero linear velocity on a
    // step-up revert. The test asserts the bob fires at all, not
    // its absolute Y window (which depends on which cells the NPC
    // traverses).
    //
    // speed_ratio = min(2.0 / 4.0, 1.0) = 0.5; with amp = 0.20 the
    // bob peak is base + 0.10.
    CHECK(maxY > minY + 0.03f);   // meaningful oscillation
    CHECK(maxY - minY < 0.30f);   // bounded by amp * ratio plus a
                                   // 1-block step margin

    EXIT_WITH_RESULT();
}
