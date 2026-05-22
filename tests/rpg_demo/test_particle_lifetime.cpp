// §3.11.9 batch D9 — Particle lifetime + destroy contract.
//
// Spawns 100 particles with monotonically increasing lifetimes (entry
// i lives for (i+1) * step seconds), then advances ticks and verifies
// that after time T exactly the entries with `initialLifetime < T`
// have been destroyed by `ParticleSystem`. Survivors must still be
// alive and carry the `Particle` UC bit.
//
// The test spawns particles directly through the demo's seed
// CommandBuffer to keep the input deterministic — it isolates
// `ParticleSystem` (aging + destroy) from `ParticleEmitterSystem`
// (event drains + random velocity rolls).

#include "DemoTestHarness.hpp"

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Components.hpp>
#include <threadmaxx/Engine.hpp>
#include <threadmaxx/UserComponent.hpp>
#include <threadmaxx/World.hpp>

#include <cstdio>
#include <vector>

namespace {

using namespace rpg;
using namespace rpg::testing;
using namespace threadmaxx;

constexpr std::uint32_t kParticleCount = 100u;
constexpr float         kLifeStep      = 0.05f;   // entry i lives (i+1)*step

struct ParticleLifetimeGame : DemoGame {
    std::vector<EntityHandle> particles;

    void onSetup(Engine& engine, World& w, CommandBuffer& seed) override {
        // Run the normal demo setup first so the UCs are registered.
        DemoGame::onSetup(engine, w, seed);

        particles.reserve(kParticleCount);
        for (std::uint32_t i = 0; i < kParticleCount; ++i) {
            const auto h = engine.reserveEntityHandle();
            particles.push_back(h);

            const float life = static_cast<float>(i + 1) * kLifeStep;

            Bundle b{};
            b.transform.position = {static_cast<float>(i), 5.0f, 0.0f};
            b.transform.scale    = {kParticleScale,
                                    kParticleScale,
                                    kParticleScale};
            b.velocity           = Velocity{{0, 0, 0}, {0, 0, 0}};
            b.initialMask        = ComponentSet{
                Component::Transform,
                Component::Velocity,
            };
            seed.spawnBundle(h, b);

            CubeRender cr;
            cr.color[0] = 1.0f; cr.color[1] = 1.0f;
            cr.color[2] = 1.0f; cr.color[3] = 1.0f;
            cr.scale    = 1.0f;
            addUserComponent(seed, ids().cubeRender, h, cr);

            Particle p;
            p.spawnTimeSeconds = 0.0f;  // engine starts at tick=0
            p.initialLifetime  = life;
            p.fadeSeconds      = 0.0f;
            p.color[0] = 1.0f; p.color[1] = 1.0f;
            p.color[2] = 1.0f; p.color[3] = 1.0f;
            addUserComponent(seed, ids().particle, h, p);
        }
    }
};

std::uint32_t countAliveParticles(const ParticleLifetimeGame& g,
                                  const Engine& engine) {
    std::uint32_t alive = 0;
    for (auto h : g.particles) {
        if (engine.world().alive(h)) ++alive;
    }
    return alive;
}

} // namespace

int main() {
    resetEdges();
    Config cfg;
    cfg.sleepToPace = false;
    cfg.workerCount = 2;
    Engine engine(cfg);
    ParticleLifetimeGame game;
    CHECK(engine.initialize(game));

    // Tick 0: commit the seed CommandBuffer. After this all 100
    // particles must be alive and carry the Particle bit.
    engine.step();
    CHECK_EQ(countAliveParticles(game, engine), kParticleCount);

    const auto pid = game.ids().particle;
    for (auto h : game.particles) {
        CHECK(threadmaxx::user::has(engine.world(), pid, h));
    }

    // Step until simTime crosses each lifetime boundary. The engine's
    // `dt` is 1/60 by default; we sample after roughly half + full
    // covering most of the spawned range.
    const float dt = static_cast<float>(engine.config().fixedStepSeconds);
    CHECK(dt > 0.0f);

    // Helper that runs until `tick * dt >= T` then checks the
    // alive-set against the expected predicate.
    auto stepUntilSeconds = [&](float seconds) {
        while (static_cast<float>(engine.tick()) * dt < seconds) {
            engine.step();
        }
    };

    // After ~half the maximum lifetime, ~half the particles should be
    // gone. After the full max lifetime, all should be gone.
    const float maxLife = kParticleCount * kLifeStep;

    stepUntilSeconds(maxLife * 0.5f + dt);
    const float t1 = static_cast<float>(engine.tick()) * dt;
    std::uint32_t expectedAlive1 = 0;
    for (std::uint32_t i = 0; i < kParticleCount; ++i) {
        const float life = static_cast<float>(i + 1) * kLifeStep;
        if (life > t1) ++expectedAlive1;
    }
    const std::uint32_t actualAlive1 = countAliveParticles(game, engine);
    std::printf("[particle_lifetime] t=%.3fs expected=%u actual=%u\n",
                static_cast<double>(t1),
                expectedAlive1, actualAlive1);
    CHECK_EQ(actualAlive1, expectedAlive1);

    // After max+dt the engine's `simTime` is strictly larger than
    // every particle's lifetime, so all of the test's known-lifetime
    // entries should have been destroyed by `ParticleSystem`. (A
    // chunk-wide scan would also count particles emitted in the
    // background by the demo's normal combat flow, so we restrict the
    // check to the test's own handle list.)
    stepUntilSeconds(maxLife + 2.0f * dt);
    const float t2 = static_cast<float>(engine.tick()) * dt;
    const std::uint32_t alive2 = countAliveParticles(game, engine);
    std::printf("[particle_lifetime] t=%.3fs alive=%u\n",
                static_cast<double>(t2), alive2);
    CHECK_EQ(alive2, 0u);

    EXIT_WITH_RESULT();
}
