#pragma once

#include "Config.hpp"
#include "Handles.hpp"
#include "Stats.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <typeindex>

namespace threadmaxx {

class World;
class ISystem;
class IRenderer;
class IGame;
class ResourceRegistry;
template <typename Ev> class EventChannel;

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

    /// Aggregate worker-pool counters (jobs submitted, own-pops, steals).
    /// Cheap to call; safe from any thread.
    JobSystemStats jobSystemStats() const noexcept;

    /// Reserve an entity handle ahead of any spawn command (§3.5). Use
    /// during `IGame::onSetup` to seed entities whose handles are needed
    /// before commit; inside a system body, prefer
    /// `SystemContext::reserveHandle()`.
    ///
    /// Reservations not consumed by a `CommandBuffer::spawn(handle, ...)`
    /// commit are reaped at the end of the next `step()`.
    /// @thread_safety Safe from any thread, including worker jobs.
    EntityHandle reserveEntityHandle();

    /// Multiply the `dt` seen by systems by `scale`. Negative values are
    /// clamped to zero. The engine's wall-clock pacing (and the
    /// integer `tick()`) is unaffected — only what game logic computes
    /// from `dt` changes. See `Engine::setPaused` for stopping
    /// simulation entirely.
    void setTimeScale(double scale) noexcept;
    /// Current time scale; default `1.0`.
    double timeScale() const noexcept;

    /// When `true`, `step()` is a no-op and `run()` keeps rendering the
    /// current world without advancing it. Default `false`.
    /// @thread_safety Safe from any thread.
    void setPaused(bool paused) noexcept;
    bool paused() const noexcept;

    /// @internal Engine-internal access. Used by `EventChannel<T>` to
    /// install or recover the channel for type `T`. Not part of the
    /// stable public surface.
    void* getEventChannelRaw(std::type_index type,
                             void* (*factory)(),
                             void (*deleter)(void*),
                             void (*drainFn)(void*));

    /// Get (or lazily create) the engine-owned event channel for type
    /// `Ev`. Same instance is returned across calls and across threads.
    /// Definition lives in `EventChannel.hpp` — include that header to
    /// instantiate.
    template <typename Ev>
    EventChannel<Ev>& events();

private:
    std::unique_ptr<internal::EngineImpl> impl_;
};

} // namespace threadmaxx
