#pragma once

#include "threadmaxx_physics/backend.hpp"
#include "threadmaxx_physics/body.hpp"
#include "threadmaxx_physics/types.hpp"

#include <cstdint>
#include <optional>

/// Constraint (joint) descriptors and lifecycle helpers.
///
/// A constraint couples two bodies in `world` so the solver enforces a
/// geometric relationship between them. `ConstraintType` covers the
/// five common forms:
///
/// - **Fixed**       — rigidly weld body A's anchor to body B's anchor.
///   No DOF.
/// - **Hinge**       — anchor + shared axis; one rotational DOF
///   around that axis. Door / lever / wheel rotation.
/// - **Slider**      — anchor + shared axis; one translational DOF
///   along that axis. Piston / drawer / elevator rail.
/// - **BallSocket**  — anchor only; three rotational DOFs around the
///   shared anchor. Shoulder / hip joint.
/// - **SixDOF**      — every translational and rotational DOF available
///   independently, gated by per-axis limits (see `linearLimits` /
///   `angularLimits`). The "swiss army knife" constraint.
///
/// **Stub backend semantics.** The `StubBackend` records every
/// constraint descriptor verbatim through `createConstraint` and
/// answers `getConstraint`; it does NOT enforce the constraint at
/// `stepWorld` (no solver). Destroying either coupled body invalidates
/// the constraint — a subsequent `getConstraint` returns `nullopt`
/// without the host needing to call `destroyConstraint` first. This
/// matches the real-backend contract: solver-owned constraints are
/// torn down when one of their bodies is removed, even when the host
/// code forgot to release the handle.
///
/// **Layer filtering for self-collision.** When
/// `disableCollisionBetweenLinkedBodies = true` the backend tells its
/// broadphase / collision filter to skip the pair. The stub records the
/// flag but doesn't simulate collision; real backends (P9) wire this
/// into the actual filter. Use this for "the wheel shouldn't collide
/// with its own axle" cases.
///
/// All helpers are sim-thread-only by convention — they call into the
/// backend directly and inherit its threading contract (see
/// `IPhysicsBackend` doc).
namespace threadmaxx::physics {

/// Constraint geometry. See the file-level doc for per-type semantics.
enum class ConstraintType : std::uint8_t {
    Fixed      = 0,
    Hinge      = 1,
    Slider     = 2,
    BallSocket = 3,
    SixDOF     = 4,
};

/// Per-axis DOF limit pair. `min == max` locks the DOF entirely;
/// `min > max` disables the limit (the DOF is free over its full
/// range). Units are radians for angular limits, meters for linear.
/// Only consumed by `ConstraintType::SixDOF` (and angular by `Hinge`,
/// linear by `Slider`); other types ignore the limit fields.
struct ConstraintLimit {
    float min{ 1.0f}; // default: min > max → free
    float max{-1.0f};
};

/// Create-time blueprint for a constraint. Trivially-copyable POD —
/// host code constructs one on the stack and hands it to
/// `IPhysicsBackend::createConstraint`. Layout matches what real
/// backends (Jolt, PhysX) expose: two bodies, local anchors, a shared
/// axis, and per-DOF limits.
///
/// Local anchor / axis vectors are in each body's local frame; the
/// backend transforms them to world space using the body's current
/// `BodyState::rotation` + `position`. Axes are assumed unit-length.
struct ConstraintDesc {
    ConstraintType type{ConstraintType::Fixed};

    BodyId bodyA{};
    BodyId bodyB{};

    /// Anchor point in each body's local frame.
    Vec3 localAnchorA{};
    Vec3 localAnchorB{};

    /// Shared axis in each body's local frame. `Hinge` uses this as the
    /// rotation axis, `Slider` as the translation axis; `Fixed` /
    /// `BallSocket` ignore it. `SixDOF` interprets it as the constraint
    /// frame's local X axis (Y / Z are derived).
    Vec3 localAxisA{1.0f, 0.0f, 0.0f};
    Vec3 localAxisB{1.0f, 0.0f, 0.0f};

    /// Per-axis linear / angular limits. `Hinge` reads `angularLimits[0]`
    /// (rotation around the shared axis); `Slider` reads
    /// `linearLimits[0]` (translation along the shared axis); `SixDOF`
    /// reads all three of each. Other types ignore them.
    ConstraintLimit linearLimits[3]{};
    ConstraintLimit angularLimits[3]{};

    /// If `true`, the backend's broadphase / collision filter skips
    /// collision tests between `bodyA` and `bodyB` while the constraint
    /// is alive. Useful when the geometry naturally interpenetrates
    /// (axle inside a wheel, ragdoll bones meeting at a joint).
    bool disableCollisionBetweenLinkedBodies{false};
};

/// Create a constraint between `desc.bodyA` and `desc.bodyB` in
/// `world`. Returns a non-zero `JointId` on success; returns a zero id
/// if `world` is invalid, either body id is stale, or both ids name the
/// same body (a body cannot be constrained to itself). The descriptor
/// is copied into backend-owned storage — the caller may drop their
/// copy immediately after the call returns.
inline JointId createConstraint(IPhysicsBackend& backend,
                                PhysicsWorldId world,
                                const ConstraintDesc& desc) {
    return backend.createConstraint(world, desc);
}

/// Tear down a constraint. Safe to call with a zero-valued id (no-op)
/// or with an id whose underlying constraint has already been
/// invalidated by one of its bodies being destroyed.
inline void destroyConstraint(IPhysicsBackend& backend,
                              PhysicsWorldId world,
                              JointId joint) {
    backend.destroyConstraint(world, joint);
}

/// Look up a constraint's descriptor. Returns `nullopt` if the id is
/// invalid, refers to a constraint that has been destroyed, OR refers
/// to a constraint whose coupled body was destroyed (the stale-body
/// case — see file-level doc). The returned value is a copy and is
/// safe to keep after subsequent mutating backend calls.
inline std::optional<ConstraintDesc> getConstraint(IPhysicsBackend& backend,
                                                   PhysicsWorldId world,
                                                   JointId joint) {
    return backend.getConstraint(world, joint);
}

} // namespace threadmaxx::physics
