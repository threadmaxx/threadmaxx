#include "WeaponFireSystem.hpp"

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Engine.hpp>
#include <threadmaxx/Query.hpp>
#include <threadmaxx/World.hpp>

#include <cmath>

namespace tou2d {

namespace {

// M3.1 Dumbfire tunables. The cooldown is in ticks (60 Hz fixed step),
// muzzle speed / ttl in world-units / seconds. Numbers picked to match
// TOU's reload feel — fast enough to be playable, slow enough that
// each shot reads visually.
constexpr std::uint64_t kFireCooldownTicks = 8;        // 8 ticks @ 60 Hz ≈ 7.5 shots / s
constexpr float         kMuzzleSpeed       = 600.0f;   // world units / s
constexpr float         kBulletTtlSeconds  = 1.2f;     // covers ~720 world units of travel
constexpr float         kMuzzleOffset      = 18.0f;    // spawn forward of the ship body (~ ship half-extent + buffer)
constexpr float         kBulletScale       = 4.0f;     // visual cube edge
constexpr std::uint8_t  kDumbfireDamage    = 64;       // ¼ of a 0xFF tile per hit

/// Recover the Z-axis rotation angle from a quaternion that should be
/// (0, 0, sin(θ/2), cos(θ/2)).
inline float orientationAngleZ(const threadmaxx::Quat& q) noexcept {
    return std::atan2(2.0f * (q.w * q.z + q.x * q.y),
                      1.0f - 2.0f * (q.y * q.y + q.z * q.z));
}

} // namespace

WeaponFireSystem::WeaponFireSystem(UserComponentIds ids) noexcept : ids_(ids) {}

void WeaponFireSystem::update(threadmaxx::SystemContext& ctx) {
    const auto idsPi = ids_.playerInput;
    const auto idsBl = ids_.bullet;
    if (!idsPi.valid() || !idsBl.valid()) return;

    const std::uint64_t now = ctx.tick();

    ctx.single([&](threadmaxx::Range /*r*/, threadmaxx::CommandBuffer& cb) {
        const auto& view = ctx.worldView();
        for (const auto* chunkPtr : view.chunks()) {
            if (!chunkPtr) continue;
            const auto& chunk = *chunkPtr;
            if (!chunk.mask.has(threadmaxx::Component::Transform)) continue;
            if (!chunk.mask.has(threadmaxx::Component::Velocity))  continue;
            if (!chunk.mask.has(idsPi.componentBit()))             continue;

            const auto piSpan = threadmaxx::user::chunkSpan<PlayerInput>(chunk, idsPi);
            const auto entities    = chunk.entities;
            const auto& positions  = chunk.transforms;
            const auto& velocities = chunk.velocities;
            const std::size_t n    = entities.size();

            for (std::size_t row = 0; row < n; ++row) {
                if (!piSpan[row].fireBasic) continue;

                const auto& last = lastFireTick_.find(entities[row].index);
                if (last != lastFireTick_.end() &&
                    now - last->second < kFireCooldownTicks) {
                    continue;
                }
                lastFireTick_[entities[row].index] = now;

                const float angle = orientationAngleZ(positions[row].orientation);
                const float sa    = std::sin(angle);
                const float ca    = std::cos(angle);
                const threadmaxx::Vec3 forward = {-sa, ca, 0.0f};

                const auto& shipT = positions[row];
                const auto& shipV = velocities[row];

                threadmaxx::Bundle b = {};
                b.transform.position = {
                    shipT.position.x + forward.x * kMuzzleOffset,
                    shipT.position.y + forward.y * kMuzzleOffset,
                    0.0f,
                };
                b.transform.orientation = shipT.orientation;
                b.transform.scale       = {kBulletScale, kBulletScale, kBulletScale};
                b.velocity.linear = {
                    shipV.linear.x + forward.x * kMuzzleSpeed,
                    shipV.linear.y + forward.y * kMuzzleSpeed,
                    0.0f,
                };
                b.renderTag = threadmaxx::RenderTag{0, 2, 0u};  // mat 2 reserved for bullets
                b.initialMask = threadmaxx::ComponentSet{
                    threadmaxx::Component::Transform,
                    threadmaxx::Component::Velocity,
                    threadmaxx::Component::RenderTag,
                };

                const auto bulletH = ctx.reserveHandle();
                cb.spawnBundle(bulletH, b);

                Bullet blt{};
                blt.ttlSeconds = kBulletTtlSeconds;
                blt.damage     = kDumbfireDamage;
                blt.weaponKind = 0;       // Dumbfire
                blt.ownerSlot  = 0;       // M3.1 single player; bot/multiplayer wires this in M3.3
                threadmaxx::addUserComponent(cb, idsBl, bulletH, blt);
            }
        }
    });
}

} // namespace tou2d
