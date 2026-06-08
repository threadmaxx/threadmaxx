#pragma once

#include "threadmaxx_physics/backend.hpp"

#include <memory>

/// Factory for the bundled `StubBackend` — a deterministic, no-real-
/// physics implementation of `IPhysicsBackend`. Use it for:
///
/// - tests that need predictable, bit-stable behavior;
/// - "physics-disabled" runtime modes where the engine still owns
///   `PhysicsBodyRef` slots but no solver actually integrates;
/// - the conformance baseline against which real backends are
///   compared at the API surface (signatures, lifecycle, span sizes).
///
/// The stub:
/// - tracks worlds / shapes / bodies in flat tables,
/// - in P1, `stepWorld` is a no-op (positions never change),
/// - in P4 (planned), `stepWorld` adds kinematic integration:
///   `position += linearVelocity * dt` and angular composition,
/// - reports body state via `syncBodiesToGame` from its own table.
namespace threadmaxx::physics {

/// Construct a fresh StubBackend instance. Returns a `unique_ptr`
/// suitable for handing straight into the `PhysicsScene` constructor
/// in later batches; for P1 the backend is exercised directly via the
/// `IPhysicsBackend` surface.
std::unique_ptr<IPhysicsBackend> makeStubBackend();

} // namespace threadmaxx::physics
