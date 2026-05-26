#include "WeaponFireSystem.hpp"

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Engine.hpp>
#include <threadmaxx/Query.hpp>
#include <threadmaxx/World.hpp>

#include <cmath>

namespace tou2d {

namespace {

// M3.1 Dumbfire tunables.
constexpr std::uint64_t kFireCooldownTicks = 8;        // 60 Hz → ~7.5 shots / s
constexpr float         kMuzzleSpeed       = 600.0f;   // world units / s
constexpr float         kBulletTtlSeconds  = 1.2f;
constexpr float         kMuzzleOffset      = 18.0f;
constexpr float         kBulletScale       = 4.0f;
constexpr std::uint8_t  kDumbfireDamage    = 64;       // ¼ of a 192-HP tile

// M3.3 Spread tunables — 3 bullets at ±kSpreadAngle around forward.
constexpr std::uint64_t kSpreadCooldownTicks = 18;     // ~3.3 shots / s
constexpr float         kSpreadAngleRad      = 0.30f;  // ~17°
constexpr float         kSpreadSpeed         = 520.0f; // slightly slower than Dumbfire
constexpr float         kSpreadTtlSeconds    = 0.9f;
constexpr std::uint8_t  kSpreadDamage        = 40;     // 3 × 40 = 120 burst vs 64

inline float orientationAngleZ(const threadmaxx::Quat& q) noexcept {
    return std::atan2(2.0f * (q.w * q.z + q.x * q.y),
                      1.0f - 2.0f * (q.y * q.y + q.z * q.z));
}

/// Spawn one bullet pointed at world-space angle `angle`, inheriting
/// the ship's velocity. Shared between Dumbfire (1 call) and Spread
/// (3 calls per fire).
void spawnBullet(threadmaxx::SystemContext& ctx,
                 threadmaxx::CommandBuffer& cb,
                 threadmaxx::UserComponentId idsBl,
                 const threadmaxx::Transform& shipT,
                 const threadmaxx::Velocity&  shipV,
                 float angle,
                 float speed,
                 float ttlSeconds,
                 std::uint8_t damage,
                 std::uint8_t weaponKind,
                 std::uint16_t ownerSlot) {
    const float sa = std::sin(angle);
    const float ca = std::cos(angle);
    const threadmaxx::Vec3 forward = {-sa, ca, 0.0f};

    threadmaxx::Bundle b = {};
    b.transform.position = {
        shipT.position.x + forward.x * kMuzzleOffset,
        shipT.position.y + forward.y * kMuzzleOffset,
        0.0f,
    };
    // Orient the bullet around its travel direction so it visually
    // points where it's going (matters once we render textured sprites,
    // harmless for the cube preview).
    {
        const float half = angle * 0.5f;
        b.transform.orientation = {0.0f, 0.0f, std::sin(half), std::cos(half)};
    }
    b.transform.scale = {kBulletScale, kBulletScale, kBulletScale};
    b.velocity.linear = {
        shipV.linear.x + forward.x * speed,
        shipV.linear.y + forward.y * speed,
        0.0f,
    };
    b.renderTag   = threadmaxx::RenderTag{0, 2, 0u};
    b.initialMask = threadmaxx::ComponentSet{
        threadmaxx::Component::Transform,
        threadmaxx::Component::Velocity,
        threadmaxx::Component::RenderTag,
    };

    const auto bulletH = ctx.reserveHandle();
    cb.spawnBundle(bulletH, b);

    Bullet blt{};
    blt.ttlSeconds = ttlSeconds;
    blt.damage     = damage;
    blt.weaponKind = weaponKind;
    blt.ownerSlot  = ownerSlot;
    threadmaxx::addUserComponent(cb, idsBl, bulletH, blt);
}

} // namespace

WeaponFireSystem::WeaponFireSystem(UserComponentIds ids) noexcept : ids_(ids) {}

void WeaponFireSystem::update(threadmaxx::SystemContext& ctx) {
    const auto idsPi = ids_.playerInput;
    const auto idsBl = ids_.bullet;
    const auto idsLp = ids_.localPlayer;
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
            // Dead ships can't shoot.
            if (chunk.mask.has(threadmaxx::Component::DisabledTag)) continue;

            const auto piSpan = threadmaxx::user::chunkSpan<PlayerInput>(chunk, idsPi);
            const bool hasLp = idsLp.valid() && chunk.mask.has(idsLp.componentBit());
            const auto lpSpan = hasLp
                ? threadmaxx::user::chunkSpan<LocalPlayer>(chunk, idsLp)
                : std::span<const LocalPlayer>{};

            const auto entities    = chunk.entities;
            const auto& positions  = chunk.transforms;
            const auto& velocities = chunk.velocities;
            const std::size_t n    = entities.size();

            for (std::size_t row = 0; row < n; ++row) {
                const auto& in = piSpan[row];
                const std::uint16_t ownerSlot =
                    hasLp ? static_cast<std::uint16_t>(lpSpan[row].slot) : 0u;
                const float angle = orientationAngleZ(positions[row].orientation);
                const auto& shipT = positions[row];
                const auto& shipV = velocities[row];

                // ---- Dumbfire (fireBasic) ---------------------------
                if (in.fireBasic) {
                    auto& slot = lastFireTick_[entities[row].index];
                    if (now - slot >= kFireCooldownTicks) {
                        spawnBullet(ctx, cb, idsBl, shipT, shipV,
                                    angle,
                                    kMuzzleSpeed,
                                    kBulletTtlSeconds,
                                    kDumbfireDamage,
                                    /*weaponKind*/ 0,
                                    ownerSlot);
                        slot = now;
                    }
                }

                // ---- Spread (fireSpecial) ---------------------------
                if (in.fireSpecial) {
                    auto& slot = lastSpreadTick_[entities[row].index];
                    if (now - slot >= kSpreadCooldownTicks) {
                        for (int i = -1; i <= 1; ++i) {
                            spawnBullet(ctx, cb, idsBl, shipT, shipV,
                                        angle + static_cast<float>(i) * kSpreadAngleRad,
                                        kSpreadSpeed,
                                        kSpreadTtlSeconds,
                                        kSpreadDamage,
                                        /*weaponKind*/ 1,
                                        ownerSlot);
                        }
                        slot = now;
                    }
                }
            }
        }
    });
}

} // namespace tou2d
