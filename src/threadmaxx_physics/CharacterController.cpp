#include "threadmaxx_physics/character.hpp"

#include "threadmaxx_physics/query.hpp"

#include <cmath>

// CharacterController — capsule-based kinematic character driven by
// horizontal intent + gravity. Built entirely on top of the
// `IPhysicsBackend` query surface (`sweep` / `raycast`); the controller
// owns no backend handle of its own.
//
// **Capsule-as-sphere approximation.** The stub backend's queries are
// AABB-only and ignore body rotation, so the controller models the
// character as a SPHERE of `desc.radius` centered at well-chosen
// sample points for every internal sweep:
//
// - **Horizontal sweep** starts at the BOTTOM HEMISPHERE CENTER of the
//   capsule (i.e. `position - (0, height/2 - radius, 0)`). This is the
//   lowest geometrically-meaningful sphere of the capsule — knee-height
//   obstacles that should block locomotion are caught here. Centering
//   the sphere at the capsule center instead would let low obstacles
//   slip under the probe.
// - **Step-up elevated sweep** starts at the bottom hemisphere center
//   raised by `stepHeight + kStepUpEps`. The epsilon is essential: an
//   obstacle whose top is exactly `stepHeight` above the floor produces
//   an inflated-AABB upper edge of `(obs_top + radius) == (stepHeight +
//   radius)`. Without the epsilon, the slab check is a degenerate "in
//   slab" border and the elevated sweep hits — making the "obstacle
//   top exactly at stepHeight" case fail to step. With the epsilon, the
//   sphere clears by 1e-3 and step-up succeeds.
// - **Ground probe** sweeps from the CAPSULE CENTER itself; the
//   resting distance to "bottom hemisphere touches floor" is
//   `height/2 - radius`. `kGroundProbeEps` extends this so a capsule
//   resting exactly at that distance registers as grounded.
//
// A real backend (Jolt, P9) will reimplement the controller using its
// native capsule sweep (no sphere approximation needed); `character.hpp`
// is identical for both.

namespace threadmaxx::physics {

namespace {

// Tiny tolerance added to ground-probe and step-up downward distances
// so a capsule resting EXACTLY on a floor (sweep distance ==
// height/2 - radius) still registers as grounded.
constexpr float kGroundProbeEps = 1.0e-3f;

// Vertical elevation added on top of `stepHeight` for the step-up
// retry sweep. Obstacle tops that sit exactly at stepHeight inflate to
// the slab upper boundary; the eps lifts the probe sphere above that
// boundary so the parallel-axis miss path triggers and step-up
// succeeds. Numerically tiny — the character is never visibly lifted
// by this amount.
constexpr float kStepUpEps = 1.0e-3f;

// Tiny back-off applied to a slide-stop advance distance so the next
// tick's sweep doesn't start inside the inflated AABB of the obstacle
// we just touched (the stub's `rayVsAabb` treats origin-inside as a
// miss, which would let the character ghost-walk through the
// obstacle).
constexpr float kCollisionBackoff = 1.0e-4f;

inline float vec3Length(const Vec3& v) noexcept {
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

// Compute the bottom hemisphere center of a capsule whose center is at
// `capsuleCenter`. Lives at `radius` above the capsule's lowest point.
inline Vec3 bottomHemCenter(const Vec3& capsuleCenter,
                            const CharacterControllerDesc& desc) noexcept {
    return Vec3{capsuleCenter.x,
                capsuleCenter.y - (desc.height * 0.5f - desc.radius),
                capsuleCenter.z};
}

// Issue a horizontal sphere sweep starting at `from` along unit-length
// `dir` for `dist` units.
inline std::optional<SweepHit> sweepHoriz(IPhysicsBackend& backend,
                                          PhysicsWorldId world,
                                          const CharacterControllerDesc& desc,
                                          const Vec3& from,
                                          const Vec3& dir,
                                          float dist) noexcept {
    SweepRequest req;
    req.start = from;
    req.direction = dir;
    req.maxDistance = dist;
    req.radius = desc.radius;
    req.layerMask = desc.layerMask;
    return backend.sweep(world, req);
}

// Issue a downward sphere sweep from `from` for `maxDistance` units.
inline std::optional<SweepHit> sweepDown(IPhysicsBackend& backend,
                                         PhysicsWorldId world,
                                         const CharacterControllerDesc& desc,
                                         const Vec3& from,
                                         float maxDistance) noexcept {
    SweepRequest req;
    req.start = from;
    req.direction = Vec3{0.0f, -1.0f, 0.0f};
    req.maxDistance = maxDistance;
    req.radius = desc.radius;
    req.layerMask = desc.layerMask;
    return backend.sweep(world, req);
}

// Probe ground beneath a capsule whose center is at `capsuleCenter`.
// Returns the capsule-center y that would place the capsule resting on
// the contact floor; nullopt on no contact within
// `restingDistance + kGroundProbeEps`. Resting distance is
// `height/2 - radius` — the travel for the sweep sphere (started at
// capsule center) before its bottom surface touches the floor.
inline std::optional<float> probeRestY(IPhysicsBackend& backend,
                                       PhysicsWorldId world,
                                       const CharacterControllerDesc& desc,
                                       const Vec3& capsuleCenter) noexcept {
    const float restingDistance = desc.height * 0.5f - desc.radius;
    const float maxDist = restingDistance + kGroundProbeEps;
    auto hit = sweepDown(backend, world, desc, capsuleCenter, maxDist);
    if (!hit.has_value()) {
        return std::nullopt;
    }
    // Sphere center y after travel = capsuleCenter.y - hit->distance.
    // Floor contact y = sphere_center_after - radius. Resting capsule
    // center y = contact_y + height/2.
    const float sphereCenterAfter = capsuleCenter.y - hit->distance;
    const float contactY = sphereCenterAfter - desc.radius;
    return contactY + desc.height * 0.5f;
}

} // namespace

CharacterController::CharacterController(IPhysicsBackend& backend,
                                         PhysicsWorldId world,
                                         const CharacterControllerDesc& desc)
    : backend_(&backend), world_(world), desc_(desc) {
    state_.position = desc.startPosition;
    state_.rotation = desc.startRotation;
    state_.velocity = Vec3{};
    // Eagerly compute grounded so the very first state() read reflects
    // contact with the floor (if any) before any move() call.
    auto restY = probeRestY(backend, world, desc, state_.position);
    if (restY.has_value()) {
        state_.grounded = true;
        state_.position.y = *restY;
    } else {
        state_.grounded = false;
    }
}

void CharacterController::move(const CharacterInput& input, float dt) {
    // Snapshot grounded state at tick start. Walking down a slope at
    // non-trivial horizontal speed temporarily separates capsule center
    // from the new floor by `horizDist * tan(slope)`, which is well
    // outside the standard ground probe's `restingDistance + eps` reach
    // — `probeRestY` returns nullopt, grounded flips to false, and the
    // CharacterRender system fires the Jump_Loop clip on flat-ish
    // terrain. Symmetric to step-up: if we were grounded last tick AND
    // we didn't jump, allow the post-move probe to snap down by up to
    // `stepHeight` so descending slopes (and tiny single-tick drops at
    // ledges) stay grounded.
    const bool wasGrounded = state_.grounded;

    // === 1. Horizontal intent → world-frame velocity ===
    Vec3 horizIntent{input.moveIntent.x, 0.0f, input.moveIntent.z};
    const float intentMag = vec3Length(horizIntent);
    Vec3 horizVel{};
    if (intentMag > 0.0f) {
        const float clamped = (intentMag > 1.0f) ? 1.0f : intentMag;
        const float scale = (clamped / intentMag) * desc_.maxMoveSpeed;
        horizVel.x = horizIntent.x * scale;
        horizVel.z = horizIntent.z * scale;
    }

    // === 2. Jump pulse + gravity integration ===
    if (input.jump && state_.grounded) {
        state_.velocity.y = input.jumpSpeed;
        state_.grounded = false;
    } else if (state_.grounded) {
        state_.velocity.y = 0.0f;
    } else {
        state_.velocity.y += desc_.gravity * dt;
    }

    // === 3. Horizontal sweep + step-up ===
    const Vec3 horizDelta{horizVel.x * dt, 0.0f, horizVel.z * dt};
    const float horizDist = vec3Length(horizDelta);
    Vec3 newPos = state_.position;
    if (horizDist > 0.0f) {
        const Vec3 horizDir{horizDelta.x / horizDist,
                            0.0f,
                            horizDelta.z / horizDist};
        const Vec3 horizStart = bottomHemCenter(state_.position, desc_);

        auto hit = sweepHoriz(*backend_, world_, desc_,
                              horizStart, horizDir, horizDist);
        if (!hit.has_value()) {
            newPos = state_.position + horizDelta;
        } else {
            // Try step-up: retry the horizontal sweep at
            // `bottomHemCenter + stepHeight + kStepUpEps`.
            bool stepped = false;
            if (desc_.stepHeight > 0.0f) {
                const Vec3 elevatedStart{
                    horizStart.x,
                    horizStart.y + desc_.stepHeight + kStepUpEps,
                    horizStart.z};
                auto elevHit = sweepHoriz(*backend_, world_, desc_,
                                          elevatedStart, horizDir,
                                          horizDist);
                if (!elevHit.has_value()) {
                    // Forward clear at elevation. Find a floor beneath
                    // the elevated forward target. Max probe distance =
                    // `stepHeight + 2*kStepUpEps` so a step-over onto
                    // the original floor still resolves.
                    const Vec3 elevatedTarget{
                        elevatedStart.x + horizDelta.x,
                        elevatedStart.y,
                        elevatedStart.z + horizDelta.z};
                    // `stepHeight + 1cm` of slack absorbs the "step
                    // over a low obstacle onto original floor" worst
                    // case (`d = stepHeight + kStepUpEps`) with room
                    // for float roundoff.
                    const float maxStepDown =
                        desc_.stepHeight + 0.01f;
                    auto floorHit = sweepDown(*backend_, world_, desc_,
                                              elevatedTarget,
                                              maxStepDown);
                    if (floorHit.has_value()) {
                        const float sphereCenterAfter =
                            elevatedTarget.y - floorHit->distance;
                        const float contactY =
                            sphereCenterAfter - desc_.radius;
                        newPos.x = elevatedTarget.x;
                        newPos.z = elevatedTarget.z;
                        newPos.y = contactY + desc_.height * 0.5f;
                        stepped = true;
                    }
                }
            }
            if (!stepped) {
                // Slide-then-stop: advance up to (distance -
                // back-off). Zero horizontal velocity so subsequent
                // ticks don't keep pressing into the wall.
                float advance = hit->distance - kCollisionBackoff;
                if (advance < 0.0f) {
                    advance = 0.0f;
                }
                newPos = state_.position +
                         Vec3{horizDir.x * advance,
                              0.0f,
                              horizDir.z * advance};
                horizVel = Vec3{};
            }
        }
    }

    // === 4. Apply vertical delta with descent CCD ===
    // Descending fast (|velocity.y| * dt > restingDistance) without a
    // sweep would tunnel through the floor — the post-move probe only
    // sees floors INSIDE `restingDistance + eps` of the capsule
    // center, and the stub's `rayVsAabb` treats origin-inside-AABB as
    // a miss. The CCD sweep handles both fast and slow descent in one
    // path; ascending / stationary motion skips it.
    const float vertDelta = state_.velocity.y * dt;
    bool landed = false;
    if (vertDelta < 0.0f) {
        const float restingDist = desc_.height * 0.5f - desc_.radius;
        const float sweepDist = -vertDelta + restingDist + kGroundProbeEps;
        auto hit = sweepDown(*backend_, world_, desc_, newPos, sweepDist);
        if (hit.has_value()) {
            const float sphereCenterAfter = newPos.y - hit->distance;
            const float contactY = sphereCenterAfter - desc_.radius;
            newPos.y = contactY + desc_.height * 0.5f;
            state_.velocity.y = 0.0f;
            landed = true;
        } else {
            newPos.y += vertDelta;
        }
    } else {
        newPos.y += vertDelta;
    }

    // === 5. Post-move ground probe ===
    if (landed) {
        state_.grounded = true;
    } else {
        auto restY = probeRestY(*backend_, world_, desc_, newPos);
        if (restY.has_value()) {
            state_.grounded = true;
            newPos.y = *restY;
        } else if (wasGrounded && !input.jump && desc_.stepHeight > 0.0f) {
            // Stick-to-floor extended probe: capsule center is currently
            // up to `stepHeight` above where the new floor wants it.
            // Probe an extra `stepHeight` below resting and snap down
            // on hit. Mirrors the step-up reach used in horizontal
            // motion.
            const float restingDistance = desc_.height * 0.5f - desc_.radius;
            const float extendedMax = restingDistance
                                    + desc_.stepHeight
                                    + kGroundProbeEps;
            auto hit = sweepDown(*backend_, world_, desc_, newPos, extendedMax);
            if (hit.has_value()) {
                const float sphereCenterAfter = newPos.y - hit->distance;
                const float contactY = sphereCenterAfter - desc_.radius;
                newPos.y = contactY + desc_.height * 0.5f;
                state_.velocity.y = 0.0f;
                state_.grounded = true;
            } else {
                state_.grounded = false;
            }
        } else {
            state_.grounded = false;
        }
    }

    state_.position = newPos;
    state_.velocity.x = horizVel.x;
    state_.velocity.z = horizVel.z;
}

} // namespace threadmaxx::physics
