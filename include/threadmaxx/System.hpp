#pragma once

#include "CommandBuffer.hpp"
#include "ScratchArena.hpp"
#include "TaskTag.hpp"
#include "render/RenderFrameBuilder.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <span>

namespace threadmaxx {

class World;

/// Half-open index range `[begin, end)` into the dense entity arrays.
struct Range {
    std::uint32_t begin = 0;
    std::uint32_t end = 0;

    constexpr std::uint32_t size() const noexcept { return end - begin; }
    constexpr bool empty()        const noexcept { return end <= begin; }
};

/// Context handed to a system's `update()`.
///
/// Systems schedule parallel work by calling `parallelFor`; the engine
/// slices the range, hands each slice to a worker, and waits for
/// completion before `update()` returns. Inside the callable a worker
/// sees the world as `const` and writes mutations into its own private
/// @ref CommandBuffer.
class SystemContext {
public:
    using JobFn       = std::function<void(Range, CommandBuffer&)>;
    using JobFnArena  = std::function<void(Range, CommandBuffer&, ScratchArena&)>;

    virtual ~SystemContext() = default;

    virtual const World& world() const noexcept = 0;

    /// Fixed-step delta in seconds. Equal to `Config::fixedStepSeconds`
    /// multiplied by the engine's current time scale (see
    /// `Engine::setTimeScale`). The engine still advances `tick()` by
    /// exactly one and `simulationTime()` by `fixedStepSeconds` per
    /// step; only the `dt` game logic sees changes.
    virtual double       dt()    const noexcept = 0;

    /// Tick this step is computing (post-increment compared to
    /// `Engine::tick()` returned during this step).
    virtual std::uint64_t tick() const noexcept = 0;

    /// Splits `[0, count)` into chunks of about `grain` items each,
    /// schedules one job per chunk on the worker pool, and waits.
    /// Each job receives its slice and a private @ref CommandBuffer.
    /// Pass `grain = 0` to let the engine pick.
    /// @pre `update()` is on the call stack.
    virtual void parallelFor(std::uint32_t count,
                             std::uint32_t grain,
                             JobFn fn) = 0;

    /// As above, but each job additionally receives a private
    /// @ref ScratchArena for short-lived bump-allocated scratch memory.
    /// The arena is reset at wave end; allocations do not survive
    /// across ticks.
    virtual void parallelFor(std::uint32_t count,
                             std::uint32_t grain,
                             JobFnArena fn) = 0;

    /// Run on the simulation thread, single-threaded. Useful for setup
    /// work or systems that fundamentally cannot be parallelized.
    /// The callable receives a zero-length @ref Range and a single
    /// @ref CommandBuffer.
    virtual void single(JobFn fn) = 0;

    /// As above, with a @ref ScratchArena alongside the command buffer.
    virtual void single(JobFnArena fn) = 0;

    /// Reserve an entity handle up front, before any spawn command is
    /// recorded. The reserved handle is usable in a same-recording
    /// `cb.spawn(handle, ...)` overload and as the target of a
    /// `Parent{handle, ...}` so a single job can spawn a parent and its
    /// children atomically.
    ///
    /// Reservations get reaped at the end of the commit phase if no
    /// matching `cb.spawn(handle, ...)` materialized them — a job that
    /// bails does not leak slots. The reservation is taken under a
    /// single fast mutex on storage; cheap to call.
    ///
    /// @thread_safety Safe to call from any worker job under this `update()`.
    virtual EntityHandle reserveHandle() = 0;

    /// Batch reservation form. Reserves up to `out.size()` handles under
    /// a single acquisition of the storage-side mutex. Returns the
    /// number of handles written. Use when a single job knows up front
    /// it needs many slots (e.g. spawning N children of a parent).
    /// @thread_safety Safe to call from any worker job under this `update()`.
    virtual std::uint32_t reserveHandles(std::uint32_t count,
                                         std::span<EntityHandle> out) = 0;
};

/// User-implemented unit of gameplay/physics/AI.
///
/// Stateless or with state owned by the implementation; registered with
/// the engine via @ref IGame. Subclasses override `update()` (required)
/// and optionally `reads()` / `writes()` to opt in to parallel
/// scheduling, plus `onRegister()` / `onUnregister()` for one-shot
/// setup/teardown.
class ISystem {
public:
    virtual ~ISystem() = default;

    /// Stable name. Returned pointer must outlive the system — string
    /// literals are the convention. The engine does NOT copy it.
    virtual const char* name() const noexcept = 0;

    /// Called once after the engine has constructed the world, in
    /// registration order.
    virtual void onRegister(World&) {}

    /// Called once at engine shutdown, before the world is torn down.
    virtual void onUnregister(World&) {}

    /// Called every fixed step. Use `ctx.parallelFor` / `ctx.single`
    /// to do work.
    virtual void update(SystemContext& ctx) = 0;

    /// Optional hook invoked once per `Engine::step()` BEFORE any wave
    /// starts, on the simulation thread, single-threaded. Called in
    /// registration order across all systems. Use it to pump per-tick
    /// input queues, snapshot state for a trace consumer, or reset
    /// per-tick scratch state that `update` will fill.
    ///
    /// Commands emitted through `ctx.single()` here are committed in
    /// registration order before any wave runs, so wave systems observe
    /// them. `ctx.parallelFor` works too but typically isn't useful at
    /// this stage.
    virtual void preStep(SystemContext& /*ctx*/)  {}

    /// Optional hook invoked once per `Engine::step()` AFTER the last
    /// wave's commit phase has finished, on the simulation thread,
    /// single-threaded. Called in registration order. Use it to publish
    /// per-tick events, refresh a HUD, or finalize accumulators.
    ///
    /// Commands emitted here are committed after every wave; they are
    /// visible to the next tick's preStep hooks but not to this tick's
    /// systems.
    virtual void postStep(SystemContext& /*ctx*/) {}

    /// §3.2 render-prep hook invoked once per `Engine::step()` AFTER
    /// every @ref postStep has run, on the simulation thread, single-
    /// threaded. Called in registration order with a per-system
    /// @ref RenderFrameBuilder; the engine merges each system's
    /// contribution into the next published @ref RenderFrame in
    /// registration order. This is the natural place for view-
    /// dependent passes: camera setup, light gathering, visibility
    /// culling, debug-overlay emission, skinned-pose upload.
    ///
    /// The builder is exclusive to this system for the duration of
    /// the call; do not retain pointers into it past return.
    virtual void buildRenderFrame(RenderFrameBuilder& /*builder*/) {}

    /// Read set the engine consults when deciding which systems can run
    /// concurrently within a wave. Default: `ComponentSet::all()` —
    /// every pair of systems conflicts, forcing strict registration-
    /// order serial. Override to enable parallel scheduling.
    virtual ComponentSet reads()  const noexcept { return ComponentSet::all(); }

    /// Write set; see @ref reads. Two systems S1 and S2 conflict iff
    /// `S1.writes ∩ S2.writes ≠ ∅` OR
    /// `S1.writes ∩ S2.reads  ≠ ∅` OR
    /// `S1.reads  ∩ S2.writes ≠ ∅`.
    virtual ComponentSet writes() const noexcept { return ComponentSet::all(); }

    /// §3.4 batch 11 — tags this system *consumes*. Each tag becomes
    /// a directed edge "this-system runs after every system whose
    /// @ref provides set contains the same tag", in addition to the
    /// component-mask conflict edges from @ref reads / @ref writes.
    /// Use it when reads/writes alone can't capture a producer →
    /// consumer order (e.g. two systems write the same component but
    /// one must happen first).
    ///
    /// Default: empty. The returned span must outlive the call; the
    /// engine never copies it. Static storage (a `constexpr` array
    /// member) is the convention.
    virtual std::span<const TaskTag> dependencies() const noexcept { return {}; }

    /// §3.4 batch 11 — tags this system *produces*. Each tag becomes
    /// a directed edge "every system whose @ref dependencies set
    /// contains the same tag runs after this one".
    ///
    /// Default: empty. Same lifetime rule as @ref dependencies.
    virtual std::span<const TaskTag> provides() const noexcept { return {}; }

    /// §3.4 batch 11 — hint to the engine's default chunk-grain
    /// picker. When a `parallelFor` call passes `grain == 0`, the
    /// engine normally picks `(count + workers*4 - 1) / (workers*4)`
    /// to fan out 4 chunks per worker. Overriding @ref preferredGrain
    /// to a non-zero value pins the per-system default to that
    /// number (capped at `count`). Pass-through `grain != 0` calls
    /// are unaffected.
    ///
    /// Default: 0 (use the heuristic). Pick a non-zero value when
    /// inner-loop cost makes a known good batch size win.
    virtual std::uint32_t preferredGrain() const noexcept { return 0; }
};

/// Configuration knobs for the built-in hierarchy system. Pass to
/// @ref makeHierarchySystem; defaults match the historical behavior.
struct HierarchyConfig {
    /// When true, the child's world `scale` is the component-wise
    /// product of its parent's world `scale` and `localOffset.scale`,
    /// chaining through arbitrarily deep trees. When false (the
    /// default), the child's world `scale` is just `localOffset.scale`
    /// and the parent's scale is ignored. See `doc/hierarchy.md` for
    /// the rationale behind the default.
    bool propagateScale = false;
};

/// Built-in hierarchy system factory.
///
/// Returns a system that propagates `Parent`-attached entities' world
/// `Transform` from their parent's world `Transform` composed with
/// `Parent::localOffset`. Resolves multi-level chains in one pass via
/// DFS with memoization. Scale does NOT chain by default — see
/// @ref Parent and @ref HierarchyConfig::propagateScale to opt in.
///
/// @note Register this *after* movement systems that write `Transform`
///       so it runs in a later wave and observes their commits.
/// @par Reads / Writes
///      `reads = {Transform, Parent}`, `writes = {Transform}`.
std::unique_ptr<class ISystem> makeHierarchySystem(HierarchyConfig cfg = {});

} // namespace threadmaxx
