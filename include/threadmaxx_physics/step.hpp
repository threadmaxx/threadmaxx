#pragma once

#include "threadmaxx_physics/backend.hpp"
#include "threadmaxx_physics/types.hpp"

#include <cstdint>
#include <span>

/// Simulation step helpers.
///
/// `IPhysicsBackend::stepWorld` is the low-level virtual every backend
/// implements; the free functions in this header are ergonomic wrappers
/// around it. Game code typically wants one of:
///
/// - **Variable-step single world** — drive the backend with whatever
///   `dt` the engine just produced. Cheapest path; matches what
///   `PhysicsScene::step(world, dt)` will lower to once the wrapper
///   class lands.
/// - **Variable-step multi world** — same `dt`, fan out across every
///   `PhysicsWorldId` in a span. Used by games that maintain isolated
///   simulation worlds (split-screen, parallel server sandboxes, replay
///   scrubbing).
/// - **Fixed-timestep with accumulator** — pin determinism: feed a
///   variable wall-clock `dt`, drain it into N fixed-size sub-steps
///   capped at `maxSubSteps`. Returns the sub-step count actually run
///   so the caller can drive interpolation with the residual.
///
/// All helpers are sim-thread-only by convention — they call into the
/// backend directly and inherit its threading contract (see
/// `IPhysicsBackend` doc).
namespace threadmaxx::physics {

/// Advance a single world by `dt` seconds. Thin pass-through to
/// `IPhysicsBackend::stepWorld`; the wrapper exists so call sites read
/// as scene-level intent rather than backend-virtual plumbing.
inline void stepScene(IPhysicsBackend& backend,
                      PhysicsWorldId world,
                      float dt) {
    backend.stepWorld(world, dt);
}

/// Step every world in `worlds` by the same `dt`. Invalid ids are
/// silently skipped by the backend (`stepWorld` is documented no-op on
/// stale handles), so a hetero-aged span is safe.
inline void stepScenes(IPhysicsBackend& backend,
                       std::span<const PhysicsWorldId> worlds,
                       float dt) {
    for (PhysicsWorldId w : worlds) {
        backend.stepWorld(w, dt);
    }
}

/// Carries the residual time between fixed-step calls. Caller owns the
/// instance — usually a per-scene member zero-initialized at scene
/// construction.
struct FixedStepAccumulator {
    float accumulated{0.0f};
};

/// Fixed-timestep stepping driven by a variable wall-clock `dt`. Adds
/// `dt` to the accumulator, then drains as many `fixedStep`-sized
/// sub-steps as fit (capped at `maxSubSteps`). Returns the number of
/// sub-steps actually run; the residual stays in `accumulator` for the
/// next call.
///
/// @pre `fixedStep > 0`. A non-positive `fixedStep` is treated as a
/// configuration error and the call is a no-op (returns 0); callers
/// should validate at construction time.
inline std::uint32_t stepSceneFixed(IPhysicsBackend& backend,
                                    PhysicsWorldId world,
                                    float dt,
                                    float fixedStep,
                                    std::uint32_t maxSubSteps,
                                    FixedStepAccumulator& accumulator) {
    if (fixedStep <= 0.0f) {
        return 0;
    }
    accumulator.accumulated += dt;
    std::uint32_t substeps = 0;
    while (accumulator.accumulated >= fixedStep && substeps < maxSubSteps) {
        backend.stepWorld(world, fixedStep);
        accumulator.accumulated -= fixedStep;
        ++substeps;
    }
    return substeps;
}

} // namespace threadmaxx::physics
