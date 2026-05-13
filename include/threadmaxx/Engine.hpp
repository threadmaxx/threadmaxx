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
class ResourceRegistry;

namespace internal { class EngineImpl; }

/// Top-level engine: owns the world, the worker pool, the registered
/// systems and the renderer (if any).
///
/// Typical lifecycle:
/// @code
///     Engine eng(cfg);
///     eng.initialize(game);   // game.onSetup() registers systems/renderer
///     eng.run();              // blocks; returns when requestQuit() is called
///     eng.shutdown();         // tears down renderer, systems, workers, world
/// @endcode
///
/// Or, for embedders that drive their own loop:
/// @code
///     while (!eng.quitRequested()) eng.step();
/// @endcode
///
/// `shutdown()` is idempotent and is called by the destructor if you
/// forget. `requestQuit()` and `quitRequested()` are the only methods
/// safe to call from a thread other than the simulation thread while
/// `run()` is executing.
class Engine {
public:
    /// Construct an engine. Worker threads are spawned at `initialize()`
    /// time, not here, so this is cheap.
    explicit Engine(const Config& cfg = {});
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    /// Build the world, spin up workers, then call `game.onSetup()`.
    /// Must be called before `step()` / `run()`.
    /// @return false on a fatal error (e.g. the game's `onSetup` failed,
    ///         or the renderer's `initialize()` returned false).
    bool initialize(IGame& game);

    /// Run one fixed simulation step (one tick). Safe to call directly if
    /// you want to drive timing yourself. Advances `tick()` by 1.
    void step();

    /// Loop calling `step()` until `requestQuit()` is invoked. Honors
    /// `Config::sleepToPace` and `Config::maxStepsPerIteration`.
    /// Performs render-frame interpolation between sim ticks.
    void run();

    /// Tear down in reverse-registration order. Idempotent.
    void shutdown();

    /// Ask `run()` to exit after the current tick completes.
    /// @thread_safety Safe from any thread.
    void requestQuit() noexcept;

    /// @thread_safety Safe from any thread.
    bool quitRequested() const noexcept;

    /// Register a system. The engine takes ownership and calls
    /// `system->onRegister(world)` immediately. Recomputes the wave
    /// schedule. Call from `IGame::onSetup`.
    void registerSystem(std::unique_ptr<ISystem> system);

    /// Set the renderer (optional). `nullptr` is allowed (headless mode);
    /// the engine simply skips `submitFrame()` calls.
    /// @warning The engine does NOT take ownership: the renderer must
    ///          outlive the engine.
    void setRenderer(IRenderer* renderer) noexcept;

    World&       world() noexcept;
    const World& world() const noexcept;

    const Config& config() const noexcept;

    /// Monotonically increasing tick counter. 0 before the first
    /// `step()`. Incremented by 1 per fixed step.
    std::uint64_t tick() const noexcept;

    /// Accumulated simulation time in seconds = `tick * fixedStepSeconds`.
    double simulationTime() const noexcept;

    /// Per-tick instrumentation snapshot. Refreshed at the end of each
    /// `step()`. Cheap to copy.
    EngineStats stats() const noexcept;

    /// Per-system snapshots in registration order. Refreshed at the end
    /// of each `step()`.
    /// @warning The returned span points to engine-owned memory and is
    ///          invalidated by `registerSystem()` and `shutdown()`; copy
    ///          if you need to retain.
    std::span<const SystemStats> systemStats() const noexcept;

    /// Engine-owned, thread-safe typed resource registry. Lifetime
    /// matches the engine and the registry never reseats.
    /// @thread_safety Safe from any thread, including worker jobs.
    ResourceRegistry&       resources()       noexcept;
    const ResourceRegistry& resources() const noexcept;

private:
    std::unique_ptr<internal::EngineImpl> impl_;
};

} // namespace threadmaxx
