#pragma once

#include "threadmaxx_physics/body.hpp"
#include "threadmaxx_physics/config.hpp"
#include "threadmaxx_physics/shape.hpp"
#include "threadmaxx_physics/types.hpp"

#include <span>

/// Backend solver interface.
///
/// `IPhysicsBackend` is the abstraction every solver adapter
/// implements. The library ships two backends in v1.0:
///
/// - **StubBackend** — deterministic, no real physics. Bodies remain
///   where they were created; `stepWorld` is a no-op in P1, advances
///   linearly in P4. The stub is the documented "physics disabled"
///   mode and the reference behavior for backend conformance tests.
/// - **JoltBackend** — gated by `find_package(Jolt)` at configure time
///   (lands in P9). Real broadphase + narrowphase + constraint solver.
///
/// A new backend implements every pure virtual below; it does not
/// need to derive from anything beyond `IPhysicsBackend`. Lifetime is
/// managed by `PhysicsScene` via `std::unique_ptr<IPhysicsBackend>`.
///
/// @thread_safety A single backend instance must not have concurrent
/// calls into different methods unless the implementation explicitly
/// documents otherwise. Game code calls into this from the sim thread.
namespace threadmaxx::physics {

class IPhysicsBackend {
public:
    virtual ~IPhysicsBackend() = default;

    /// Construct a new physics world. Returns a non-zero
    /// `PhysicsWorldId` on success.
    virtual PhysicsWorldId createWorld(const PhysicsConfig& config) = 0;

    /// Tear down a world and every body / constraint it owns. Safe
    /// to call with a zero-valued id (no-op).
    virtual void destroyWorld(PhysicsWorldId world) = 0;

    /// Register a collider shape. Backends are free to cook /
    /// validate the source `ShapeDesc` and discard the input vectors
    /// afterwards.
    virtual ShapeId createShape(const ShapeDesc& desc) = 0;

    /// Drop a registered shape. Bodies still referencing it must
    /// continue to operate until they're destroyed themselves
    /// (real backends defer the free until the last referent is gone;
    /// the StubBackend mirrors that behavior).
    virtual void destroyShape(ShapeId shape) = 0;

    /// Create a rigid body in `world` from `desc`, attaching the
    /// supplied collider shapes. Returns a non-zero `BodyId` on
    /// success.
    virtual BodyId createBody(PhysicsWorldId world,
                              const BodyDesc& desc,
                              std::span<const ShapeId> shapes) = 0;

    /// Destroy a body. Body ids are scoped per-world, so
    /// `destroyBody` must be paired with the same world that created
    /// the body.
    virtual void destroyBody(PhysicsWorldId world, BodyId body) = 0;

    /// Advance `world` by `dt` seconds. Backends may substep
    /// internally based on `PhysicsConfig::fixedTimestep` /
    /// `maxSubSteps`. P1 ships with the StubBackend as a no-op;
    /// kinematic integration arrives in P4.
    virtual void stepWorld(PhysicsWorldId world, float dt) = 0;

    /// Read the current state of every body in `bodies` into
    /// `outStates`. The two spans MUST be the same size; backends
    /// MAY no-op on size mismatch (defensive — release builds skip
    /// the work, debug builds may assert).
    virtual void syncBodiesToGame(PhysicsWorldId world,
                                  std::span<const BodyId> bodies,
                                  std::span<BodyState> outStates) = 0;

protected:
    IPhysicsBackend() = default;
    IPhysicsBackend(const IPhysicsBackend&) = delete;
    IPhysicsBackend& operator=(const IPhysicsBackend&) = delete;
};

} // namespace threadmaxx::physics
