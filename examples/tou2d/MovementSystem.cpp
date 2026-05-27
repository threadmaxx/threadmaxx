#include "MovementSystem.hpp"

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Query.hpp>
#include <threadmaxx/World.hpp>

#include <cmath>

namespace tou2d {

namespace {

// M1 / M4.5 tunables. The thrust / turn / cap constants below are the
// REFERENCE values that match the "Basic ship" stat row (Strength 3 /
// Thrusters 3 / Turning 3). Every other ship is computed as a ratio
// against the Basic baseline via `ShipKind::thrustForce / turnRate`,
// so editing the constants here rescales the whole fleet uniformly.
constexpr float kThrustAccelBase  = 240.0f;   // wu / s² @ thrustForce 3.0
// M4.6 — reverse must beat gravity. Previously 100 vs gravity 120 →
// pressing Back while pointed straight down could NOT make a Basic-ship
// rise, which felt broken (the original TOU lets you yank yourself out
// of a falling-into-pit situation with reverse). 180 gives the Basic
// ship 60 wu/s² of net climb when pointed down (vs 120 for forward).
// Still weaker than forward so reverse remains a tactical "abort"
// rather than a primary travel mode. Per-kind scaling stays in place:
// Bee × 3.3 → 594 (still tank-of-thrust), Destroyer × 0.5 → 90 (will
// NOT beat gravity when pointed down, on purpose — the brick falls).
constexpr float kReverseAccelBase = 180.0f;
constexpr float kTurnRateBase     =   4.5f;   // rad / s   @ turnRate 3.0
constexpr float kGravityAccel     = 120.0f;
constexpr float kAirDamping       =   0.45f;
constexpr float kMaxAngularSpeedBase = 6.0f;  // cap @ turnRate 3.0

inline float orientationAngleZ(const threadmaxx::Quat& q) noexcept {
    // For a pure-Z rotation we stored (0, 0, sin(θ/2), cos(θ/2));
    // general atan2 form recovers θ even after tiny drift.
    return std::atan2(2.0f * (q.w * q.z + q.x * q.y),
                      1.0f - 2.0f * (q.y * q.y + q.z * q.z));
}

inline threadmaxx::Quat quatFromAngleZ(float theta) noexcept {
    const float half = theta * 0.5f;
    return {0.0f, 0.0f, std::sin(half), std::cos(half)};
}

} // namespace

MovementSystem::MovementSystem(UserComponentIds ids) noexcept : ids_(ids) {}

void MovementSystem::update(threadmaxx::SystemContext& ctx) {
    const auto idsPi   = ids_.playerInput;
    const auto idsShip = ids_.ship;
    if (!idsPi.valid()) return;

    const float dt = static_cast<float>(ctx.dt());
    const float damping = std::exp(-kAirDamping * dt);

    // Sequential single() — M1 has one ship. The system layout
    // (parallelFor over chunks) lands in M3 when 2-4 players + bots
    // create chunk fan-out worth fanning across workers.
    ctx.single([&](threadmaxx::Range /*r*/, threadmaxx::CommandBuffer& cb) {
        const auto& view = ctx.worldView();
        for (const auto* chunkPtr : view.chunks()) {
            if (!chunkPtr) continue;
            const auto& chunk = *chunkPtr;
            if (!chunk.mask.has(threadmaxx::Component::Transform))   continue;
            if (!chunk.mask.has(threadmaxx::Component::Velocity))    continue;
            if (!chunk.mask.has(idsPi.componentBit()))               continue;
            // Dead/respawning ships sit out — DisabledTag splits the
            // archetype chunk, so this check is a single mask test.
            if (chunk.mask.has(threadmaxx::Component::DisabledTag))  continue;

            const auto piSpan = threadmaxx::user::chunkSpan<PlayerInput>(chunk, idsPi);
            // M4.5 — per-ship stats come from the ShipKind table indexed
            // by Ship.shipKindIdx. Bot-driven and human ships both end
            // up in chunks that carry Ship, so the mask-gate here is
            // also defence-in-depth; absent Ship → Basic-ship stats.
            const bool hasShip = idsShip.valid() && chunk.mask.has(idsShip.componentBit());
            const auto shipSpan = hasShip
                ? threadmaxx::user::chunkSpan<Ship>(chunk, idsShip)
                : std::span<const Ship>{};
            const auto entities  = chunk.entities;
            const auto& positions = chunk.transforms;
            const auto& velocities = chunk.velocities;
            const std::size_t n = entities.size();

            for (std::size_t row = 0; row < n; ++row) {
                const auto& in = piSpan[row];
                threadmaxx::Transform t = positions[row];
                threadmaxx::Velocity  v = velocities[row];

                // Per-kind scalars vs the Basic-ship baseline (3/3/3).
                const ShipKind& kind = hasShip
                    ? shipKindAt(shipSpan[row].shipKindIdx)
                    : kShipKinds[0];
                const float thrustScale = kind.thrustForce / kShipKindStatReference;
                const float turnScale   = kind.turnRate    / kShipKindStatReference;
                const float thrustAccel  = kThrustAccelBase     * thrustScale;
                const float reverseAccel = kReverseAccelBase    * thrustScale;
                const float turnRate     = kTurnRateBase        * turnScale;
                const float maxAngular   = kMaxAngularSpeedBase * turnScale;

                // ---- Turn ---------------------------------------------------
                const float turnDir = static_cast<float>(in.turnLeft) -
                                      static_cast<float>(in.turnRight);
                float angle = orientationAngleZ(t.orientation);
                angle += turnDir * turnRate * dt;
                t.orientation = quatFromAngleZ(angle);
                v.angular = {0.0f, 0.0f, turnDir * turnRate};
                if (std::fabs(v.angular.z) > maxAngular) {
                    v.angular.z = (v.angular.z > 0 ? 1.0f : -1.0f) * maxAngular;
                }

                // ---- Thrust (forward = local +Y rotated by `angle`) --------
                // For pure-Z rotation by θ, local (0,1,0) maps to (-sin θ, cos θ, 0).
                const float sa = std::sin(angle);
                const float ca = std::cos(angle);
                const threadmaxx::Vec3 forward = {-sa, ca, 0.0f};

                const float thrustMag =
                    static_cast<float>(in.thrust)  * thrustAccel -
                    static_cast<float>(in.back)    * reverseAccel;

                v.linear.x += forward.x * thrustMag * dt;
                v.linear.y += forward.y * thrustMag * dt;

                // ---- Gravity -----------------------------------------------
                v.linear.y -= kGravityAccel * dt;

                // ---- Air damping -------------------------------------------
                v.linear.x *= damping;
                v.linear.y *= damping;

                // ---- Integrate position ------------------------------------
                t.position.x += v.linear.x * dt;
                t.position.y += v.linear.y * dt;

                // ---- Level bounds clamp ------------------------------------
                // Hard wall at the level extent. Ship-half = 14 (visual
                // scale 28 / 2). Zero the velocity in the clamped axis
                // so the ship sticks at the edge rather than skating.
                if (levelActive_) {
                    constexpr float shipHalf = 14.0f;
                    const float minX = levelMinX_ + shipHalf;
                    const float maxX = levelMaxX_ - shipHalf;
                    if (t.position.x < minX) { t.position.x = minX; v.linear.x = 0.0f; }
                    if (t.position.x > maxX) { t.position.x = maxX; v.linear.x = 0.0f; }
                    const float minY = levelMinY_ + shipHalf;
                    const float maxY = levelMaxY_ - shipHalf;
                    if (t.position.y < minY) { t.position.y = minY; v.linear.y = 0.0f; }
                    if (t.position.y > maxY) { t.position.y = maxY; v.linear.y = 0.0f; }
                }

                cb.setVelocity(entities[row], v);
                cb.setTransform(entities[row], t);
            }
        }
    });
}

} // namespace tou2d
