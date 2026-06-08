#pragma once

#include "threadmaxx_physics/backend.hpp"
#include "threadmaxx_physics/body.hpp"
#include "threadmaxx_physics/types.hpp"

#include <cmath>
#include <cstdint>

/// Capsule-based character controller.
///
/// A character controller is a higher-level abstraction on top of the
/// `IPhysicsBackend` queries. It owns a kinematic capsule that the game
/// can drive each tick with a horizontal intent vector plus an optional
/// jump; the controller resolves collisions, snaps to ground, and steps
/// over small ledges autonomously. It does NOT register as a backend
/// body — the controller drives queries (`sweep` / `raycast`) against
/// every other body in the world but is invisible to those queries
/// itself. Game code typically creates the matching kinematic body
/// alongside the controller if other queries need to see the character.
///
/// **`position` convention** — `state.position` is the world-space
/// CENTER of the capsule. Bottom hemisphere center sits at
/// `position - (0, height/2 - radius, 0)`; bottom of capsule at
/// `position - (0, height/2, 0)`.
///
/// **Stub-backend behavior.** The stub's queries are AABB-only (no
/// rotation, no true capsule narrowphase). The controller models the
/// character as a SPHERE of `desc.radius` centered at `state.position`
/// for sweep purposes, which is conservative against the stub's own
/// AABB-inflated sweep. Step-up, ground detect, and forward-blocking
/// all use sphere sweeps. A real backend (P9) can re-implement the
/// controller using its native capsule-sweep API; the public surface
/// stays the same.
///
/// **Slope handling.** The controller exposes `isSurfaceWalkable` as a
/// free function — game code can also use it to gate jump / interact
/// logic from a manual `raycast` ground sample. Slopes whose normal is
/// inside `desc.slopeLimit` (measured from world-up) are walkable;
/// steeper surfaces are treated as walls. The stub's AABB-aligned
/// raycast normals are always axis-aligned, so the integration test
/// for slope limit drives the helper directly with synthetic normals;
/// the gameplay path is intentionally minimal until P9.
///
/// **Threading.** Sim-thread-only by convention. The controller calls
/// straight into the backend's `sweep` / `raycast`; it inherits their
/// threading contract (see `IPhysicsBackend`).
namespace threadmaxx::physics {

/// Create-time blueprint for a character controller. Trivially-
/// copyable POD; host code stack-constructs one and hands it to the
/// `CharacterController` constructor.
struct CharacterControllerDesc {
    /// Initial capsule center position in world space.
    Vec3 startPosition{};

    /// Initial orientation. The controller does not currently rotate
    /// the capsule (cylinder is upright), but stores the rotation for
    /// rendering / facing-direction game code.
    Quat startRotation{};

    /// Capsule cross-section radius.
    float radius{0.5f};

    /// Total capsule height (cylinder + two hemispheres). Must be
    /// `>= 2 * radius` for a non-degenerate shape; the controller does
    /// not enforce this (game-side responsibility).
    float height{1.8f};

    /// Maximum ledge height the controller will auto-step up over. A
    /// horizontal sweep that hits something is retried at
    /// `position + (0, stepHeight, 0)`; on clear, the controller
    /// snaps down to the new floor.
    float stepHeight{0.3f};

    /// Maximum walkable slope angle in RADIANS, measured between the
    /// ground normal and world-up `(0, 1, 0)`. Default ~45°. Surfaces
    /// steeper than this are treated as walls — the character slides
    /// off rather than walking up. Used by `isSurfaceWalkable`.
    float slopeLimit{0.7853982f};

    /// Maximum horizontal move speed in m/s applied when
    /// `|input.moveIntent| = 1`. Smaller intents scale linearly.
    float maxMoveSpeed{5.0f};

    /// World-frame gravitational acceleration in m/s². Engine
    /// convention: negative y is down, so a typical value is
    /// `-9.81f`. Applied per tick when `!grounded`.
    float gravity{-9.81f};

    /// Collision layer mask propagated to every backend `sweep` /
    /// `raycast` the controller dispatches. Default
    /// `0xFFFFFFFF` blocks against every layer.
    std::uint32_t layerMask{0xFFFFFFFFu};
};

/// Per-tick movement intent supplied by game code.
struct CharacterInput {
    /// Desired horizontal move direction in world space. The Y
    /// component is ignored (vertical motion is gravity-driven only,
    /// except for the jump pulse). Magnitude is clamped to `[0, 1]`
    /// — pass a unit vector for full speed, a shorter vector to
    /// scale move speed down.
    Vec3 moveIntent{};

    /// Request a jump THIS tick. Honored only when `state.grounded`.
    /// Sets vertical velocity to `jumpSpeed` and immediately unsets
    /// `grounded`.
    bool jump{false};

    /// Initial upward velocity applied on a successful jump (m/s).
    float jumpSpeed{5.0f};
};

/// Per-tick state read back from the controller after `move()`.
struct CharacterState {
    /// Current capsule-center position in world space.
    Vec3 position{};

    /// Current orientation (not currently rotated by the controller;
    /// see `CharacterControllerDesc::startRotation`).
    Quat rotation{};

    /// Current world-space velocity. Horizontal components reflect
    /// the most recent applied move intent; the Y component is the
    /// integrated gravity / jump pulse.
    Vec3 velocity{};

    /// `true` iff the most recent ground probe found a walkable
    /// surface beneath the capsule.
    bool grounded{false};
};

/// Returns `true` iff `groundNormal` represents a surface the
/// character can walk on under `desc.slopeLimit`. `groundNormal` is
/// assumed unit-length and pointing UP from the surface. The check is
/// the angle between `groundNormal` and world-up `(0, 1, 0)` — the
/// surface is walkable iff that angle is `<= slopeLimit`, equivalent
/// to `dot(groundNormal, up) >= cos(slopeLimit)`.
///
/// A negative or zero `slopeLimit` makes every surface unwalkable.
/// A `slopeLimit >= π/2` makes every upward-facing surface walkable.
inline bool isSurfaceWalkable(const CharacterControllerDesc& desc,
                              const Vec3& groundNormal) noexcept {
    if (desc.slopeLimit <= 0.0f) {
        return false;
    }
    // dot(groundNormal, (0,1,0)) == groundNormal.y, assuming
    // groundNormal is unit-length.
    return groundNormal.y >= std::cos(desc.slopeLimit);
}

/// Capsule character controller. Move-only (holds a non-owning
/// backend pointer; copying would alias the backend handle without
/// any duplication semantics).
///
/// Lifecycle:
/// - Construct with backend + world id + desc; the initial state is
///   placed at `desc.startPosition / startRotation` with zero
///   velocity. `grounded` is computed eagerly via a downward sweep so
///   the first `state()` query reports the correct value before any
///   `move()` has run.
/// - Call `move(input, dt)` once per fixed tick to advance.
/// - Destruction is trivial — no backend handle to release. The
///   underlying world / body / shape registry stays untouched.
class CharacterController {
public:
    CharacterController(IPhysicsBackend& backend,
                        PhysicsWorldId world,
                        const CharacterControllerDesc& desc);

    CharacterController(const CharacterController&) = delete;
    CharacterController& operator=(const CharacterController&) = delete;
    CharacterController(CharacterController&&) noexcept = default;
    CharacterController& operator=(CharacterController&&) noexcept = default;

    /// Create-time descriptor (read-only after construction).
    const CharacterControllerDesc& desc() const noexcept { return desc_; }

    /// Per-tick state. Mutated by `move()`.
    const CharacterState& state() const noexcept { return state_; }

    /// Advance the controller by `dt` seconds under `input`. Order of
    /// operations:
    ///   1. Compute horizontal velocity from `input.moveIntent` and
    ///      `desc.maxMoveSpeed`.
    ///   2. Apply jump (if requested + grounded) and integrate gravity
    ///      (if not grounded).
    ///   3. Horizontal sweep — on hit, attempt step-up by retrying the
    ///      sweep at `position + (0, stepHeight, 0)`; commit the step
    ///      if a floor is found beneath the elevated target.
    ///   4. Apply vertical delta from gravity / jump.
    ///   5. Ground probe — downward sphere sweep; if hit within range,
    ///      snap to the contact point and mark `grounded`.
    void move(const CharacterInput& input, float dt);

private:
    IPhysicsBackend* backend_;
    PhysicsWorldId world_;
    CharacterControllerDesc desc_;
    CharacterState state_;
};

} // namespace threadmaxx::physics
