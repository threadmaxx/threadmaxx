#pragma once

#include "CommitBreakdown.hpp"
#include "Config.hpp"
#include "Handles.hpp"
#include "Resource.hpp"
#include "SkipPolicy.hpp"
#include "Stats.hpp"
#include "UserComponent.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <type_traits>
#include <typeindex>
#include <vector>

namespace threadmaxx {

class World;
class ISystem;
class IRenderer;
class IGame;
class IResourceLoader;
class ResourceRegistry;
class ILogger;
class ITraceSink;
class ITuningPolicy;
class TuningTrace;
enum class TuningMode;
struct FrameSnapshot;
struct WorldSnapshot;
template <typename Ev> class EventChannel;

namespace internal { class EngineImpl; }

/// §3.4 batch 11 — one row of @ref Engine::taskGraphSnapshot. Describes
/// one registered system, the wave it lands in, and the indices of
/// systems it directly depends on (read/write conflict OR tag
/// dependency). Strings are owned copies — safe to keep across
/// `registerSystem` calls.
struct TaskGraphNode {
    std::size_t              index = 0;
    std::string              name;
    std::size_t              wave  = 0;
    std::vector<std::size_t> dependsOn;
};

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

    /// Re-submit the current front render frame with the given
    /// interpolation `alpha` (0..1). Useful for hosts that drive `step()`
    /// manually and need to keep the renderer presenting even while
    /// `setPaused(true)` — `step()` is a no-op when paused (no
    /// `submitFrame` call), so without this hook the swapchain freezes
    /// on the pre-pause image. Renderer-side textures updated between
    /// paused ticks (e.g. a UI overlay bitmap) become visible only once
    /// a fresh frame is submitted; calling this every paused tick
    /// guarantees that. World state is not rebuilt; only `RenderFrame::alpha`
    /// is overwritten.
    /// @thread_safety Sim thread only.
    void submitInterpolatedFrame(float alpha) noexcept;

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

    /// Register a system at a specific registration-order index. Useful
    /// for tests and mod loaders that need to inject a system at a
    /// known point in the pipeline (e.g. immediately before the
    /// hierarchy system). Clamped to `[0, registeredSystemCount()]`;
    /// passing `registeredSystemCount()` is equivalent to
    /// `registerSystem`. Recomputes the wave schedule.
    /// @return The actual registration index used.
    std::size_t registerSystemAt(std::size_t position,
                                 std::unique_ptr<ISystem> system);

    /// Number of currently-registered systems.
    std::size_t registeredSystemCount() const noexcept;

    /// §3.4 batch 11 — debug snapshot of the wave scheduler's DAG. One
    /// @ref TaskGraphNode per registered system, in registration order,
    /// carrying its wave assignment and direct predecessor indices.
    ///
    /// Useful for visualization (feed into a Graphviz / Mermaid
    /// generator) and for HUD assertions ("two systems that depend on
    /// each other must not share a wave"). Computed from the engine's
    /// current state — call after every `registerSystem` if you keep a
    /// running graph in your tooling.
    ///
    /// @thread_safety Sim thread only.
    std::vector<TaskGraphNode> taskGraphSnapshot() const;

    /// Set the renderer (optional). `nullptr` is allowed (headless mode);
    /// the engine simply skips `submitFrame()` calls.
    /// @warning The engine does NOT take ownership: the renderer must
    ///          outlive the engine.
    void setRenderer(IRenderer* renderer) noexcept;

    /// §3.6.5 batch 15a — forward a host-window resize to the
    /// installed renderer's @ref IRenderer::onResize hook. A no-op
    /// when no renderer is installed. The engine never independently
    /// watches the host window; game code (typically a platform
    /// integration system or the main loop) is expected to forward
    /// resize events here.
    ///
    /// Calls happen synchronously on the calling thread; the renderer
    /// must be ready to handle the resize on whichever thread invokes
    /// `notifyResize`. The default (sim-thread) path matches
    /// `submitFrame`'s context.
    /// @thread_safety Sim thread by convention. If you need to call
    ///                from a UI thread, the renderer's `onResize` must
    ///                tolerate it.
    void notifyResize(std::uint32_t width, std::uint32_t height) noexcept;

    /// Install a log sink. `nullptr` restores the engine's default
    /// `std::cerr`-backed logger. The engine does NOT take ownership —
    /// the logger must outlive the engine. Lifecycle messages, system
    /// registration, and loader errors are routed here.
    /// @thread_safety Safe to call before `initialize`. Mid-run swaps
    ///                are honored on the next call site.
    void setLogger(ILogger* logger) noexcept;

    /// Non-null pointer to the currently-installed logger (or the
    /// engine's default sink if @ref setLogger was never called).
    /// @thread_safety Safe from any thread.
    ILogger& logger() const noexcept;

    /// The engine-owned `World`. Lifetime matches the engine and the
    /// reference never reseats. Use for read-only inspection from the
    /// sim thread or for mutation via @ref CommandBuffer; direct
    /// mutation of `World` is reserved for the commit phase.
    /// @thread_safety Read-only access is safe between waves. During
    /// a wave, only the `worldView()` snapshot through `SystemContext`
    /// is the supported reader.
    World&       world() noexcept;
    /// @copydoc world
    const World& world() const noexcept;

    /// The `Config` the engine was constructed with. Pinned for the
    /// engine's lifetime; runtime knobs (`setTickBudget`,
    /// `setSkipPolicy`, `setStallTimeout`) live as their own setters.
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

    /// Bundled snapshot of `stats()`, `systemStats()` and
    /// `jobSystemStats()` (§3.2). Combine with
    /// `threadmaxx::writeJsonLines(os, snap)` from
    /// `<threadmaxx/Trace.hpp>` to spool one JSON-Lines record per tick.
    /// @warning The `systems` span shares the lifetime caveat of
    ///          @ref systemStats.
    FrameSnapshot frameSnapshot() const noexcept;

    /// SHARDED_OPTIMISATION.md S0 — per-step Pass A / B / C breakdown of
    /// the sharded commit path. Reset at the top of every `step()`;
    /// accumulated across all `commitBuffersSharded` calls in the step
    /// (one call per system with a non-empty buffer set). When the
    /// sharded path early-outs to serial, `fallbackCalls` is incremented
    /// and `nsTotal` records the serial-commit cost; otherwise every
    /// field is populated. Always-on; ~30–60 ns/call overhead.
    /// @thread_safety Sim thread; read after `step()` returns.
    CommitBreakdown lastCommitBreakdown() const noexcept;

    /// Engine-owned, thread-safe typed resource registry. Lifetime
    /// matches the engine and the registry never reseats.
    /// @thread_safety Safe from any thread, including worker jobs.
    ResourceRegistry&       resources()       noexcept;
    const ResourceRegistry& resources() const noexcept;

    /// Register an asset loader. The engine takes ownership and
    /// pumps `loader->update(*this)` once per tick, on the simulation
    /// thread, after every `postStep` hook commits. The engine does
    /// not spawn worker threads for the loader; its own pool (if any)
    /// is its concern.
    /// @return A non-owning pointer to the loader, for tests / inspection.
    ///         Loaders are torn down in reverse-registration order
    ///         during `shutdown()` after each has been notified via
    ///         @ref IResourceLoader::onShutdown.
    IResourceLoader* addResourceLoader(std::unique_ptr<IResourceLoader> loader);

    /// Number of currently-registered resource loaders. Stable until the
    /// next @ref addResourceLoader or @ref shutdown.
    std::size_t resourceLoaderCount() const noexcept;

    /// Sum of every registered loader's @ref LoaderStats — pendingLoads,
    /// inFlight, ready, failed, memoryFootprint, memoryBudget. Useful
    /// for HUD readouts and capacity alerts.
    LoaderStats aggregateLoaderStats() const noexcept;

    /// Hot-reload notification (§3.2 batch 7). Iterates registered
    /// loaders in registration order and calls
    /// `loader->markStale(index, generation, typeid(T))` on each. The
    /// loader that owns the asset is expected to queue a reload; on a
    /// later tick's `update()` it installs the new value via
    /// `engine.resources().add(...)` and publishes an
    /// @ref AssetReloaded event on `events<AssetReloaded>()` so
    /// subscribers can rewire their cached ids.
    ///
    /// @thread_safety Currently sim-thread only — calls into the loader
    ///                vector are unsynchronized. Wrap with your own
    ///                queue if you need to call from worker jobs.
    template <typename T>
    void markResourceStale(ResourceId<T> id) {
        markResourceStaleRaw_(id.index, id.generation,
                              std::type_index(typeid(T)));
    }

    /// Block the calling thread until `done()` returns true or `timeout`
    /// elapses, pumping all registered loaders' `update()` once per
    /// iteration. Useful for splash-screen / boot-time preloads where
    /// the game shouldn't enter its main loop until a named asset set
    /// is ready.
    ///
    /// The engine yields between iterations rather than calling
    /// `step()` — the simulation does not advance, no waves run, no
    /// events drain. Pair with `engine.resources()` lookups inside the
    /// predicate to check readiness.
    /// @return `true` if `done()` returned true within the timeout;
    ///         `false` on timeout.
    /// @thread_safety Sim thread only; mirrors `step()` / `run()`.
    bool preloadUntil(std::function<bool()> done,
                      std::chrono::milliseconds timeout =
                          std::chrono::milliseconds(5000));

    /// Aggregate worker-pool counters (jobs submitted, own-pops, steals).
    /// Cheap to call; safe from any thread.
    JobSystemStats jobSystemStats() const noexcept;

    /// §3.6.5 batch 15a — number of worker threads owned by the engine's
    /// `JobSystem`. Resolved from `Config::workerCount` at construction
    /// (with `0` mapped to `max(1, hardware_concurrency - 1)`) and
    /// stable thereafter. Cheaper than `jobSystemStats().workerCount`
    /// (no atomic loads / no histogram merge); use this for sizing
    /// per-worker scratch storage and instance ring buffers.
    /// @thread_safety Safe from any thread.
    std::uint32_t workerCount() const noexcept;

    /// §3.7 batch 14 — install a per-tick @ref ITraceSink. The engine
    /// calls @ref ITraceSink::onFrame once per `step()` on the sim
    /// thread, after the frame is built and published, with the same
    /// snapshot @ref frameSnapshot would return. Pass `nullptr` to
    /// detach. The engine never takes ownership — the sink must outlive
    /// the engine.
    ///
    /// Per-tick overhead with no sink installed is zero. With a sink
    /// installed, the cost is whatever the sink's `onFrame` does on
    /// the sim thread; budget it accordingly. The included
    /// @ref FileTraceSink and @ref HudTraceSink are both designed to
    /// stay under a few microseconds per call.
    /// @thread_safety Sim thread only.
    void setTraceSink(ITraceSink* sink) noexcept;

    /// ADAPTIVE_TUNING.md T4 — install an adaptive tuning policy. The
    /// engine calls @ref ITuningPolicy::observe once per @ref step
    /// AFTER the commit phase and BEFORE the next tick begins, then
    /// @ref ITuningPolicy::propose once per step. A returned
    /// @ref TuningPatch is staged and applied at the next tick
    /// boundary, before any @ref ISystem::preStep hook runs — never
    /// mid-wave. Pass `nullptr` to detach (the default state); any
    /// pending unapplied patch is discarded.
    ///
    /// The engine never takes ownership; the policy must outlive every
    /// subsequent @ref step call. Overhead with no policy installed is
    /// a single null-pointer check per tick.
    ///
    /// @thread_safety Sim thread only.
    void setTuningPolicy(ITuningPolicy* policy) noexcept;

    /// ADAPTIVE_TUNING.md T4 — current adaptive tuning policy, or
    /// `nullptr` when none is installed. See @ref setTuningPolicy.
    /// @thread_safety Sim thread only.
    ITuningPolicy* tuningPolicy() const noexcept;

    /// ADAPTIVE_TUNING.md T6 — select the adaptive-tuner runtime mode.
    /// Defaults to @ref TuningMode::Off (matches v1.3 behaviour exactly).
    /// @ref setTuningPolicy implicitly transitions to @ref TuningMode::Active
    /// when a non-null policy is installed and to @ref TuningMode::Off
    /// when @c nullptr is installed; explicit @ref setTuningMode calls
    /// override that default — e.g. set @ref TuningMode::Scripted after
    /// installing a policy (or none at all) to route patches from the
    /// trace instead.
    ///
    /// Switching modes does not discard any pending patch staged by a
    /// previous mode. Switching to @ref TuningMode::Off leaves the
    /// installed policy and trace pointers untouched.
    /// @thread_safety Sim thread only.
    void setTuningMode(TuningMode mode) noexcept;

    /// ADAPTIVE_TUNING.md T6 — current adaptive-tuner runtime mode.
    /// @thread_safety Sim thread only.
    TuningMode tuningMode() const noexcept;

    /// ADAPTIVE_TUNING.md T6 — attach a @ref TuningTrace. In
    /// @ref TuningMode::Active mode the engine appends every applied
    /// @ref TuningPatch to the trace (keyed by the proposing tick). In
    /// @ref TuningMode::Scripted mode the engine pulls patches from the
    /// trace using @ref TuningTrace::tryGet keyed by the current tick.
    ///
    /// The engine never takes ownership; the trace must outlive every
    /// subsequent @ref step call. Pass @c nullptr to detach.
    /// @thread_safety Sim thread only.
    void setTuningTrace(TuningTrace* trace) noexcept;

    /// ADAPTIVE_TUNING.md T6 — current trace attached via
    /// @ref setTuningTrace, or @c nullptr.
    /// @thread_safety Sim thread only.
    TuningTrace* tuningTrace() const noexcept;

    /// §3.7 batch 14 — install a stall watchdog with the given
    /// timeout, in seconds. When `seconds > 0` the engine spawns a
    /// background thread that wakes periodically and checks how long
    /// the current `step()` has been running; if the running tick has
    /// exceeded the threshold AND no @ref EngineStall has been emitted
    /// for it yet, the watchdog emits one through
    /// `events<EngineStall>()`. Events drain on the sim thread at the
    /// usual tick boundary.
    ///
    /// Pass `0` to disable; the engine joins the watchdog thread.
    /// Re-calling with a new positive value replaces the old timeout
    /// without joining/re-spawning. Per-tick overhead when disabled
    /// (the default) is zero.
    /// @thread_safety Sim thread only.
    void setStallTimeout(double seconds) noexcept;
    /// Current stall-watchdog timeout in seconds; `0.0` means disabled.
    /// See @ref setStallTimeout for the setter's full contract.
    double stallTimeout() const noexcept;

    /// §3.9.5 batch 20 — capture the world snapshot synchronously on
    /// the sim thread, then invoke @p callback on a dedicated
    /// background writer thread. The sim thread keeps ticking; the
    /// callback's I/O (typically `serialize(...)` to a file) is off
    /// the per-tick budget.
    ///
    /// The snapshot copy itself is the only sim-thread work — bounded
    /// by dense-array size (~ a few ms for 100k entities). Use this
    /// for quick-saves under tight tick budgets where the synchronous
    /// `world().snapshot()` + `serialize(...)` flow would risk a
    /// `FrameBudgetWatcher` alert.
    ///
    /// @par Consistency contract
    ///      The snapshot reflects the state at the moment this
    ///      method was called (i.e. the last committed wave). Commits
    ///      that happen after this method returns do not retroactively
    ///      appear in the snapshot. Same model as
    ///      @ref RenderFrame double-buffering.
    ///
    /// @par Lifetime
    ///      The background writer is engine-owned, lazily spawned on
    ///      the first call, joined in @ref shutdown. Multiple in-flight
    ///      callbacks queue in submission order. The user's callback
    ///      runs on the writer thread; it must not call back into the
    ///      engine's mutation API.
    ///
    /// @thread_safety Sim thread only.
    void snapshotAsync(std::function<void(WorldSnapshot)> callback);

    /// Reserve an entity handle ahead of any spawn command (§3.5). Use
    /// during `IGame::onSetup` to seed entities whose handles are needed
    /// before commit; inside a system body, prefer
    /// `SystemContext::reserveHandle()`.
    ///
    /// Reservations not consumed by a `CommandBuffer::spawn(handle, ...)`
    /// commit are reaped at the end of the next `step()`.
    /// @thread_safety Safe from any thread, including worker jobs.
    EntityHandle reserveEntityHandle();

    /// Batch form of @ref reserveEntityHandle: takes one acquisition of
    /// the reservation mutex and fills the provided span with up to
    /// `out.size()` fresh handles. Returns the number of handles
    /// actually written (always `min(count, out.size())`).
    /// @thread_safety Safe from any thread, including worker jobs.
    std::uint32_t reserveEntityHandles(std::uint32_t count,
                                       std::span<EntityHandle> out);

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
    /// `true` iff @ref setPaused was last called with `true`.
    bool paused() const noexcept;

    /// §3.5 batch 12 — wall-clock budget per tick, in seconds. Default
    /// `0.0` means "no budget — never skip". Negative values are
    /// clamped to zero.
    ///
    /// When the engine's `step()` elapsed time exceeds @p seconds at a
    /// wave boundary, subsequent waves' `ISystem::skippable()` systems
    /// have their `update()` skipped this tick (under
    /// @ref SkipPolicy::Budget). `preStep` / `postStep` /
    /// `buildRenderFrame` are NEVER skipped — they're load-bearing for
    /// engine bookkeeping.
    ///
    /// Skipping is reported on the `events<SystemSkipped>()` channel.
    /// @thread_safety Sim thread only.
    void setTickBudget(double seconds) noexcept;
    /// Current per-tick budget in seconds; `0.0` means "no budget".
    /// See @ref setTickBudget for the setter's full contract.
    double tickBudget() const noexcept;

    /// §3.5 batch 12 — how the engine decides which systems to skip.
    /// Default @ref SkipPolicy::Budget (uses @ref setTickBudget).
    /// @ref SkipPolicy::Scripted ignores the budget and consults the
    /// scripted-skip queue (@ref pushScriptedSkip) for deterministic
    /// replay (lockstep networking).
    /// @thread_safety Sim thread only.
    void setSkipPolicy(SkipPolicy p) noexcept;
    SkipPolicy skipPolicy() const noexcept;

    /// §3.5 batch 12 — append one `(tick, systemName)` entry to the
    /// scripted-skip queue. When @ref skipPolicy is
    /// @ref SkipPolicy::Scripted, the engine skips system named
    /// @p systemName on tick @p tick (provided that system's
    /// `skippable()` returns true). Multiple entries for the same tick
    /// are allowed.
    ///
    /// The engine copies the name into internal storage, so callers
    /// can pass temporaries. Entries persist until @ref clearScriptedSkips
    /// or engine teardown; the engine never auto-prunes.
    /// @thread_safety Sim thread only.
    void pushScriptedSkip(std::uint64_t tick, std::string_view systemName);

    /// §3.5 batch 12 — wipe the scripted-skip queue. Call at session
    /// boundaries to release the memory; never required for
    /// correctness.
    /// @thread_safety Sim thread only.
    void clearScriptedSkips() noexcept;

    /// @internal Engine-internal access. Used by `EventChannel<T>` to
    /// install or recover the channel for type `T`. Not part of the
    /// stable public surface.
    void* getEventChannelRaw(std::type_index type,
                             void* (*factory)(),
                             void (*deleter)(void*),
                             void (*drainFn)(void*));

    /// @internal Per-instance non-zero serial assigned at construction
    /// from a process-global atomic counter. Used by `events<T>()`'s
    /// `thread_local` cache (§3.10.3 batch 24 / F8) to invalidate
    /// when a fresh engine lands at the same memory address as a
    /// destroyed one. Never reused; uniquely identifies the engine
    /// instance for the life of the process.
    std::uint64_t engineSerial() const noexcept;

    /// Get (or lazily create) the engine-owned event channel for type
    /// `Ev`. Same instance is returned across calls and across threads.
    /// Definition lives in `EventChannel.hpp` — include that header to
    /// instantiate.
    ///
    /// @par Warm cross-thread channels at setup (§3.6.5 batch 15b)
    ///      First call for a new `Ev` performs a map insert under the
    ///      internal `eventChannelsMtx_` (audit-added 2026-05-15).
    ///      Subsequent calls are lookup-hits and uncontended. If
    ///      worker threads or the stall-watchdog thread will be the
    ///      first to call `events<Ev>()`, call it once on the sim
    ///      thread at setup (e.g. inside `IGame::onSetup`) to avoid
    ///      paying the contended-insert cost on a worker.
    template <typename Ev>
    EventChannel<Ev>& events();

    /// §3.1 batch 6b: register a user-side POD component type and
    /// receive a @ref UserComponentId for use with
    /// @ref addUserComponent / @ref user::has / @ref user::tryGet /
    /// @ref user::chunkSpan.
    ///
    /// Idempotent: re-registering the same `typeid(T)` returns the same
    /// token. Bit assignment is registration-order stable across runs
    /// of the same binary; the engine never persists it across
    /// processes.
    ///
    /// @tparam T  Must be trivially copyable. The engine memcpys the
    ///            value into chunked storage and never invokes
    ///            constructors or destructors on user values.
    /// @return A valid @ref UserComponentId. Returns an invalid id if
    ///         the registry has exhausted user-bit space (48 bits
    ///         available between built-ins and the 64-bit ComponentSet
    ///         width).
    /// @thread_safety Safe to call from any thread before the first
    ///                spawn that uses the bit — register at setup time
    ///                to keep bit assignment fully deterministic.
    template <typename T>
    UserComponentId registerUserComponent() {
        static_assert(std::is_trivially_copyable_v<T>,
            "registerUserComponent<T>: T must be trivially copyable.");
        return registerUserComponentRaw_(
            std::type_index(typeid(T)),
            static_cast<std::uint32_t>(sizeof(T)));
    }

    /// §3.10.3 batch 23 — look up the @ref UserComponentId for a
    /// previously-registered type without re-registering. Returns an
    /// invalid id (test with `.valid()`) when @c T was never
    /// registered via @ref registerUserComponent — there is NO
    /// auto-registration on lookup. Designed for systems that want
    /// to fetch their tokens lazily on demand rather than threading
    /// a `UserComponentIds*` struct through every constructor.
    ///
    /// @tparam T  Must be trivially copyable. The lookup keys off
    ///            `typeid(T)`, so passing a different `T` with the
    ///            same name but different stride returns the
    ///            originally-registered id (with the original
    ///            stride) — be careful in dynamic-library setups.
    ///
    /// @thread_safety Safe to call from any thread once registration
    ///                is complete. The internal registry mutex
    ///                serializes against concurrent registrations.
    template <typename T>
    UserComponentId userComponent() const noexcept {
        static_assert(std::is_trivially_copyable_v<T>,
            "userComponent<T>: T must be trivially copyable.");
        return findUserComponentRaw_(std::type_index(typeid(T)));
    }

private:
    void markResourceStaleRaw_(std::uint32_t index,
                               std::uint32_t generation,
                               std::type_index type);

    UserComponentId registerUserComponentRaw_(std::type_index type,
                                              std::uint32_t stride);

    /// §3.10.3 batch 23 — type-erased lookup helper for
    /// @ref userComponent.
    UserComponentId findUserComponentRaw_(std::type_index type) const noexcept;

    std::unique_ptr<internal::EngineImpl> impl_;
};

} // namespace threadmaxx
