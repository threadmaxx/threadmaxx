#include "MyGame.hpp"

#include "MovementSystem.hpp"
#include "SpawnerSystem.hpp"

#include <threadmaxx/Engine.hpp>
#include <threadmaxx/World.hpp>

#include <cstdio>

void MyGame::onSetup(threadmaxx::Engine& engine,
                     threadmaxx::World&,
                     threadmaxx::CommandBuffer& seed) {
    // Renderer. Owned by the game; the engine just borrows it.
    renderer_ = std::make_unique<ConsoleRenderer>(/*printEvery=*/60);
    engine.setRenderer(renderer_.get());

    // Systems run every tick in registration order.
    engine.registerSystem(std::make_unique<SpawnerSystem>());
    engine.registerSystem(std::make_unique<MovementSystem>());

    // Seed a few entities so the world isn't empty at t=0.
    threadmaxx::Transform t;
    threadmaxx::Velocity v;
    threadmaxx::RenderTag tag;
    tag.meshId = 0;
    tag.materialId = 0;

    t.position = {0.0f, 0.0f, 0.0f};   v.linear = { 1.0f, 0.0f,  0.0f}; seed.spawn(t, v, tag);
    t.position = {1.0f, 0.0f, 1.0f};   v.linear = {-0.5f, 0.0f,  0.5f}; seed.spawn(t, v, tag);
    t.position = {-2.0f, 0.0f, 0.5f};  v.linear = { 0.2f, 0.0f, -0.3f}; seed.spawn(t, v, tag);

    std::printf("[MyGame] setup complete\n");
}

void MyGame::onTeardown(threadmaxx::Engine&, threadmaxx::World&) {
    std::printf("[MyGame] teardown\n");
}
