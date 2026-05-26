#include "MovementSystem.hpp"

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Query.hpp>
#include <threadmaxx/World.hpp>

#include <cmath>

namespace tou2d {

namespace {

// M1 tunables — no per-level config yet. M2 will read these from a
// parsed Normal.txt-style level config (gravity / resistance / etc.).
constexpr float kThrustAccel     = 240.0f;   // world units / s² along forward
constexpr float kReverseAccel    = 100.0f;   // weaker than forward, mirrors TOU
constexpr float kTurnRate        = 4.5f;     // radians / s
constexpr float kGravityAccel    = 120.0f;   // world units / s² along -Y
constexpr float kAirDamping      = 0.45f;    // 1/s — velocity *= exp(-damp * dt)
constexpr float kMaxAngularSpeed = 6.0f;

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
    const auto idsPi = ids_.playerInput;
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

            const auto piSpan = threadmaxx::user::chunkSpan<PlayerInput>(chunk, idsPi);
            const auto entities  = chunk.entities;
            const auto& positions = chunk.transforms;
            const auto& velocities = chunk.velocities;
            const std::size_t n = entities.size();

            for (std::size_t row = 0; row < n; ++row) {
                const auto& in = piSpan[row];
                threadmaxx::Transform t = positions[row];
                threadmaxx::Velocity  v = velocities[row];

                // ---- Turn ---------------------------------------------------
                const float turnDir = static_cast<float>(in.turnLeft) -
                                      static_cast<float>(in.turnRight);
                float angle = orientationAngleZ(t.orientation);
                angle += turnDir * kTurnRate * dt;
                t.orientation = quatFromAngleZ(angle);
                v.angular = {0.0f, 0.0f, turnDir * kTurnRate};
                if (std::fabs(v.angular.z) > kMaxAngularSpeed) {
                    v.angular.z = (v.angular.z > 0 ? 1.0f : -1.0f) * kMaxAngularSpeed;
                }

                // ---- Thrust (forward = local +Y rotated by `angle`) --------
                // For pure-Z rotation by θ, local (0,1,0) maps to (-sin θ, cos θ, 0).
                const float sa = std::sin(angle);
                const float ca = std::cos(angle);
                const threadmaxx::Vec3 forward = {-sa, ca, 0.0f};

                const float thrustMag =
                    static_cast<float>(in.thrust)  * kThrustAccel -
                    static_cast<float>(in.back)    * kReverseAccel;

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

                cb.setVelocity(entities[row], v);
                cb.setTransform(entities[row], t);
            }
        }
    });
}

} // namespace tou2d
