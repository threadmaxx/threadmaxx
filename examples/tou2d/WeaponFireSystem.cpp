#include "WeaponFireSystem.hpp"

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Engine.hpp>
#include <threadmaxx/EventChannel.hpp>
#include <threadmaxx/Query.hpp>
#include <threadmaxx/World.hpp>

#include <cmath>

namespace tou2d {

namespace {

// M3.1 Dumbfire tunables.
//
// 2026-05-28 — bullet visuals tightened per M5.2:
//   * `kMuzzleOffset` dropped from 18 → 10 wu (ship sprite is 28 wu
//     wide, half = 14 — so a 10 wu offset lands the bullet right at
//     the nose tip instead of floating 4 wu ahead of it).
//   * `kBulletScale` dropped from 4 → 2.4 wu — smaller bullet sprite
//     so a sustained burst doesn't visually obscure the target.
constexpr float         kMuzzleSpeed       = 600.0f;   // world units / s
constexpr float         kBulletTtlSeconds  = 1.2f;
constexpr float         kMuzzleOffset      = 10.0f;
constexpr float         kBulletScale       = 2.4f;
// M4.5 — second nerf pass. 8 dmg vs Basic-ship 150 HP → TTK ≈ 19
// hits at the new 6-tick cadence ≈ 1.9 s of held fire; vs Bee 50 HP
// → ≈ 700 ms, vs Destroyer 300 HP → ≈ 3.8 s. Engagements now hinge on
// sustained tracking, not single-trigger taps.
constexpr std::uint8_t  kDumbfireDamage    = 8;

// M3.3 Spread tunables — 3 bullets at ±kSpreadAngle around forward.
constexpr float         kSpreadAngleRad      = 0.30f;  // ~17°
constexpr float         kSpreadSpeed         = 520.0f; // slightly slower than Dumbfire
constexpr float         kSpreadTtlSeconds    = 0.9f;
// M4.5 — 3 × 5 = 15 per full-pellet burst vs Basic 150 HP → ≈ 10
// bursts to kill at the new ~370 ms burst cadence ≈ 3.7 s.
constexpr std::uint8_t  kSpreadDamage        = 5;

// 2026-05-28 — per-shot loader cooldown. Reworked per M5.2 design:
//   * Dumbfire fires forever (no reload, no magazine) on a 30-tick
//     cooldown = 0.5 s — the user's "interval of maybe half second by
//     default" for the always-on basic weapon.
//   * Spread has a single-shot magazine and a generous post-burst
//     reload (kSpreadReloadTicks ≈ 1.25 s in DemoTypes.hpp). The
//     per-burst cooldown also stays in place but is short — the reload
//     dominates the gap.
constexpr std::uint16_t kDumbfireCooldownTicks = 30;   // 0.5 s
constexpr std::uint16_t kSpreadCooldownTicks   = 22;

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

WeaponFireSystem::WeaponFireSystem(UserComponentIds ids,
                                   threadmaxx::Engine* engine) noexcept
    : ids_(ids), engine_(engine) {}

void WeaponFireSystem::update(threadmaxx::SystemContext& ctx) {
    const auto idsPi = ids_.playerInput;
    const auto idsBl = ids_.bullet;
    const auto idsLp = ids_.localPlayer;
    const auto idsLd = ids_.loadout;
    if (!idsPi.valid() || !idsBl.valid() || !idsLd.valid()) return;

    // M4.2 — round over, no new bullets and no reload ticking. Stops
    // post-victory phantom shots cleanly; resuming a round (out of
    // scope this batch — would need a "reset round" RPC) would start
    // every ship with whatever loadout state it had at freeze time,
    // which is fine because TouGame::spawnShip / ShipLifecycleSystem's
    // respawn both rewrite the loadout to a fresh default.
    if (roundEnded_ && roundEnded_->load(std::memory_order_acquire)) {
        return;
    }

    ctx.single([&](threadmaxx::Range /*r*/, threadmaxx::CommandBuffer& cb) {
        const auto& view = ctx.worldView();
        for (const auto* chunkPtr : view.chunks()) {
            if (!chunkPtr) continue;
            const auto& chunk = *chunkPtr;
            if (!chunk.mask.has(threadmaxx::Component::Transform)) continue;
            if (!chunk.mask.has(threadmaxx::Component::Velocity))  continue;
            if (!chunk.mask.has(idsPi.componentBit()))             continue;
            if (!chunk.mask.has(idsLd.componentBit()))             continue;
            // Dead ships can't shoot. Their reload counter also pauses
            // (no decrement here) — a respawn will refresh the loadout
            // anyway via ShipLifecycleSystem.
            if (chunk.mask.has(threadmaxx::Component::DisabledTag)) continue;

            const auto piSpan = threadmaxx::user::chunkSpan<PlayerInput>(chunk, idsPi);
            const auto ldSpan = threadmaxx::user::chunkSpan<WeaponLoadout>(chunk, idsLd);
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
                WeaponLoadout ld = ldSpan[row];
                const std::uint16_t ownerSlot =
                    hasLp ? static_cast<std::uint16_t>(lpSpan[row].slot) : 0u;
                const float angle = orientationAngleZ(positions[row].orientation);
                const auto& shipT = positions[row];
                const auto& shipV = velocities[row];

                // ---- Tick reloads + loader cooldowns down first -----
                // 2026-05-28 — dumbfire never reloads (infinite ammo
                // on the basic weapon per M5.2). Only the spread
                // reload chain ticks here.
                if (ld.spreadReloadIn > 0) {
                    ld.spreadReloadIn = static_cast<std::uint16_t>(ld.spreadReloadIn - 1);
                    if (ld.spreadReloadIn == 0) {
                        ld.spreadAmmo = kSpreadMagazine;
                    }
                }
                // M4.5 — independent per-shot loader cooldown. Ticks
                // every step regardless of input; fire is gated on it
                // reaching 0.
                if (ld.dumbfireCooldown > 0) {
                    ld.dumbfireCooldown = static_cast<std::uint16_t>(ld.dumbfireCooldown - 1);
                }
                if (ld.spreadCooldown > 0) {
                    ld.spreadCooldown = static_cast<std::uint16_t>(ld.spreadCooldown - 1);
                }

                // ---- Dumbfire (fireBasic) ---------------------------
                // 2026-05-28 — infinite ammo, no reload. Gate is just
                // "input held AND per-shot cooldown ready". `ld.dumbfireAmmo`
                // stays pinned at the magazine size for HUD compat.
                if (in.fireBasic && ld.dumbfireCooldown == 0) {
                    spawnBullet(ctx, cb, idsBl, shipT, shipV,
                                angle,
                                kMuzzleSpeed,
                                kBulletTtlSeconds,
                                kDumbfireDamage,
                                /*weaponKind*/ 0,
                                ownerSlot);
                    ld.dumbfireCooldown = kDumbfireCooldownTicks;
                    if (engine_) {
                        engine_->events<AudioPlay>().emit(
                            AudioPlay{audio::kSoundDumbfire, 0, 0});
                    }
                }

                // ---- Spread (fireSpecial) ---------------------------
                const bool canSpread =
                    ld.spreadReloadIn == 0 &&
                    ld.spreadCooldown == 0 &&
                    ld.spreadAmmo > 0;
                if (in.fireSpecial && canSpread) {
                    for (int i = -1; i <= 1; ++i) {
                        spawnBullet(ctx, cb, idsBl, shipT, shipV,
                                    angle + static_cast<float>(i) * kSpreadAngleRad,
                                    kSpreadSpeed,
                                    kSpreadTtlSeconds,
                                    kSpreadDamage,
                                    /*weaponKind*/ 1,
                                    ownerSlot);
                    }
                    ld.spreadAmmo     = static_cast<std::uint16_t>(ld.spreadAmmo - 1);
                    ld.spreadCooldown = kSpreadCooldownTicks;
                    if (ld.spreadAmmo == 0) {
                        ld.spreadReloadIn = kSpreadReloadTicks;
                    }
                    if (engine_) {
                        engine_->events<AudioPlay>().emit(
                            AudioPlay{audio::kSoundSpread, 0, 0});
                    }
                }

                // Write the loadout back every tick — cheap (8 B per
                // ship × ≤4 ships) and lets reload ticking persist
                // even when the ship doesn't fire.
                threadmaxx::addUserComponent(cb, idsLd, entities[row], ld);
            }
        }
    });
}

} // namespace tou2d
