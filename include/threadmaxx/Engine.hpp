#pragma once

#include "Config.hpp"
#include "Stats.hpp"

#include <cstdint>
#include <memory>
#include <span>

namespace threadmaxx {

class World;
class ISystem;
class IRenderer;
class IGame;

namespace internal { class EngineImpl; }

// Top-level engine. Owns the world, the worker pool, the registered systems
// and the renderer (if any). Lifecycle:
//
//     Engine eng(cfg);
//     eng.initialize(game);   // game.onSetup() registers systems/renderer
//     eng.run();              // blocks; returns when requestQuit() called
//     eng.shutdown();         // tears down renderer, systems, workers, world
//
// Or, for embedders that drive their own loop:
//
//     while (!eng.quitRequested()) eng.step();
class Engine {
public:
    explicit Engine(const Config& cfg = {});
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // Build the world, spin up workers, call game.onSetup(). Must be called
    // before step()/run(). Returns false on a fatal error.
    bool initialize(IGame& game);

    // Run one fixed simulation step (one tick). Safe to call directly if you
    // want to drive timing yourself.
    void step();

    // Loop calling step() until requestQuit() is invoked. Honors
    // Config::sleepToPace and Config::maxStepsPerIteration.
    void run();

    // Tear down in reverse-registration order. Idempotent.
    void shutdown();

    // Ask run() to exit after the current tick completes. Thread-safe.
    void requestQuit() noexcept;
    bool quitRequested() const noexcept;

    // Registered from game.onSetup(). The engine takes ownership.
    void registerSystem(std::unique_ptr<ISystem> system);

    // Optional. nullptr is allowed (headless). The engine does NOT take
    // ownership: the renderer must outlive the engine.
    void setRenderer(IRenderer* renderer) noexcept;

    World&       world() noexcept;
    const World& world() const noexcept;

    const Config& config() const noexcept;

    std::uint64_t tick() const noexcept;
    double simulationTime() const noexcept;

    // Per-tick instrumentation. Refreshed at the end of each step().
    EngineStats stats() const noexcept;

    // Per-system snapshots in registration order. Refreshed at the end of
    // each step(). The span points to engine-owned memory and is invalidated
    // by registerSystem() and shutdown(); copy if you need to retain.
    std::span<const SystemStats> systemStats() const noexcept;

private:
    std::unique_ptr<internal::EngineImpl> impl_;
};

} // namespace threadmaxx
