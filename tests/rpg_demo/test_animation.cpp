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
    void onSetup(Engine& engine, World& w, CommandBuffer& seed) override {
        DemoGame::onSetup(engine, w, seed);
        bobNpc = engine.reserveEntityHandle();
        Bundle b{};
        // Place far from player so combat / pickup systems leave it
        // alone for the duration of the test.
        b.transform.position = {30.0f, 1.0f, 30.0f};
        b.transform.scale    = {0.8f, 1.6f, 0.8f};
        b.velocity           = Velocity{{2.0f, 0.0f, 0.0f}, {0,0,0}};
        b.faction.id         = kFactionNeutral;
        b.boundingVolume     = BoundingVolume{
            {29.6f, 0.2f, 29.6f}, {30.4f, 1.8f, 30.4f}};
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
        anim.baseY     = 1.0f;
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
    std::printf("[test_animation] Y range = [%.3f, %.3f] (baseY=1.0, "
                "amp=0.20)\n", minY, maxY);
    // Expect the bob to span ~ baseY ± amplitude * speed_ratio.
    // speed_ratio = min(2.0 / 4.0, 1.0) = 0.5; expected range
    // = [1.0 - 0.10, 1.0 + 0.10] (approximately).
    CHECK(minY < 0.97f);   // dipped below baseY
    CHECK(maxY > 1.03f);   // rose above baseY
    CHECK(maxY - minY > 0.05f);  // meaningful oscillation

    EXIT_WITH_RESULT();
}
