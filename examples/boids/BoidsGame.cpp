#include "BoidsGame.hpp"

#include "BoidsConfig.hpp"
#include "BoidsSystem.hpp"
#include "MoveSystem.hpp"

#include <threadmaxx/Engine.hpp>

#include <cmath>
#include <cstdio>
#include <numbers>
#include <random>

void BoidsGame::onSetup(threadmaxx::Engine& engine,
                        threadmaxx::World&,
                        threadmaxx::CommandBuffer& seed) {
    renderer_ = std::make_unique<SDLRenderer>(engine_);
    engine.setRenderer(renderer_.get());

    // Two systems: steer then integrate. Their read/write sets conflict on
    // both Velocity and Transform, so the scheduler places them in separate
    // waves — alignment/cohesion/separation see the previous tick's velocity,
    // and integration sees the steered velocity from this tick.
    engine.registerSystem(std::make_unique<BoidsSystem>());
    engine.registerSystem(std::make_unique<BoidsMoveSystem>());

    // Deterministic seed so each run looks the same.
    std::mt19937 rng{0xB01D5u};
    std::uniform_real_distribution<float> uxd(0.0f, boids::kWindowW);
    std::uniform_real_distribution<float> uyd(0.0f, boids::kWindowH);
    std::uniform_real_distribution<float> ang(0.0f, 2.0f * std::numbers::pi_v<float>);

    threadmaxx::RenderTag tag;
    tag.meshId = 0;
    tag.materialId = 0;

    for (std::uint32_t i = 0; i < boids::kBoidCount; ++i) {
        threadmaxx::Transform t;
        t.position = {uxd(rng), 0.0f, uyd(rng)};

        const float a = ang(rng);
        threadmaxx::Velocity v;
        v.linear = {std::cos(a) * boids::kMaxSpeed * 0.5f,
                    0.0f,
                    std::sin(a) * boids::kMaxSpeed * 0.5f};

        seed.spawn(t, v, tag);
    }

    std::printf("[BoidsGame] seeded %u boids\n", boids::kBoidCount);
}
