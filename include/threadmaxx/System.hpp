#pragma once

#include "CommandBuffer.hpp"

#include <cstdint>
#include <functional>
#include <memory>

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
    using JobFn = std::function<void(Range, CommandBuffer&)>;

    virtual ~SystemContext() = default;

    virtual const World& world() const noexcept = 0;

    /// Fixed-step delta in seconds (`Config::fixedStepSeconds`).
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

    /// Run on the simulation thread, single-threaded. Useful for setup
    /// work or systems that fundamentally cannot be parallelized.
    /// The callable receives a zero-length @ref Range and a single
    /// @ref CommandBuffer.
    virtual void single(JobFn fn) = 0;
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
};

/// Built-in hierarchy system factory.
///
/// Returns a system that propagates `Parent`-attached entities' world
/// `Transform` from their parent's world `Transform` composed with
/// `Parent::localOffset`. Resolves multi-level chains in one pass via
/// DFS with memoization. Scale does NOT chain — see @ref Parent.
///
/// @note Register this *after* movement systems that write `Transform`
///       so it runs in a later wave and observes their commits.
/// @par Reads / Writes
///      `reads = {Transform, Parent}`, `writes = {Transform}`.
std::unique_ptr<class ISystem> makeHierarchySystem();

} // namespace threadmaxx
