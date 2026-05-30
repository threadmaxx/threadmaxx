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

// M5.6 — special-weapon tunables now live in `kSpecialWeaponSpecs` in
// DemoTypes.hpp. Per-kind values (magazine / reload / cooldown / fan
// step / bullet count / damage / speed / ttl) are looked up at fire
// time via `specialSpecAt(loadout.specialKind)`. The
// `kSpreadAngleRad / kSpreadSpeed / kSpreadTtlSeconds / kSpreadDamage`
// constants are gone — Spread is just entry 0 in the spec table now.

// 2026-05-28 — per-shot loader cooldown for the basic weapon. Dumbfire
// fires forever (no reload, no magazine) on a 30-tick cooldown = 0.5 s.
constexpr std::uint16_t kDumbfireCooldownTicks = 30;   // 0.5 s

inline float orientationAngleZ(const threadmaxx::Quat& q) noexcept {
    return std::atan2(2.0f * (q.w * q.z + q.x * q.y),
                      1.0f - 2.0f * (q.y * q.y + q.z * q.z));
}

inline float wrapPi_(float a) noexcept {
    constexpr float twoPi = 6.28318530718f;
    constexpr float pi    = 3.14159265359f;
    a = std::fmod(a + pi, twoPi);
    if (a < 0.0f) a += twoPi;
    return a - pi;
}

/// Spawn one bullet pointed at world-space angle `angle`, inheriting
/// the ship's velocity. Shared between Dumbfire (1 call) and Spread
/// (3 calls per fire).
///
/// M5.8 — when `speed == 0` (Mine drop) the bullet skips the muzzle-
/// offset push AND velocity inheritance: it drops at the ship's
/// position with zero forward velocity. `bouncesLeft` is forwarded
/// for the Bouncer kind; non-bouncer kinds pass 0.
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
                 std::uint8_t bouncesLeft,
                 std::uint16_t ownerSlot) {
    const float sa = std::sin(angle);
    const float ca = std::cos(angle);
    const threadmaxx::Vec3 forward = {-sa, ca, 0.0f};
    const bool isDrop = (speed == 0.0f);

    threadmaxx::Bundle b = {};
    b.transform.position = isDrop
        ? threadmaxx::Vec3{shipT.position.x, shipT.position.y, 0.0f}
        : threadmaxx::Vec3{shipT.position.x + forward.x * kMuzzleOffset,
                           shipT.position.y + forward.y * kMuzzleOffset,
                           0.0f};
    // Orient the bullet around its travel direction so it visually
    // points where it's going (matters once we render textured sprites,
    // harmless for the cube preview).
    {
        const float half = angle * 0.5f;
        b.transform.orientation = {0.0f, 0.0f, std::sin(half), std::cos(half)};
    }
    b.transform.scale = {kBulletScale, kBulletScale, kBulletScale};
    b.velocity.linear = isDrop
        ? threadmaxx::Vec3{0.0f, 0.0f, 0.0f}
        : threadmaxx::Vec3{shipV.linear.x + forward.x * speed,
                           shipV.linear.y + forward.y * speed,
                           0.0f};
    b.renderTag   = threadmaxx::RenderTag{0, 2, 0u};
    b.initialMask = threadmaxx::ComponentSet{
        threadmaxx::Component::Transform,
        threadmaxx::Component::Velocity,
        threadmaxx::Component::RenderTag,
    };

    const auto bulletH = ctx.reserveHandle();
    cb.spawnBundle(bulletH, b);

    Bullet blt{};
    blt.ttlSeconds  = ttlSeconds;
    blt.damage      = damage;
    blt.weaponKind  = weaponKind;
    blt.ownerSlot   = ownerSlot;
    blt.bouncesLeft = bouncesLeft;
    threadmaxx::addUserComponent(cb, idsBl, bulletH, blt);
}

} // namespace

bool botShotHitsAlly(std::span<const AllyPos> allies,
                     float ox, float oy,
                     float fireAngle,
                     std::uint8_t selfFaction,
                     std::uint32_t selfIdx) noexcept {
    for (const AllyPos& a : allies) {
        if (a.selfIdx == selfIdx)             continue;
        if (a.factionId != selfFaction)       continue;
        const float dx = a.x - ox;
        const float dy = a.y - oy;
        const float r2 = dx * dx + dy * dy;
        if (r2 > kFriendlyFireRangeWU * kFriendlyFireRangeWU) continue;
        const float angleToAlly = std::atan2(-dx, dy);
        const float delta       = std::fabs(wrapPi_(angleToAlly - fireAngle));
        if (delta < kFriendlyFireArcRad) return true;
    }
    return false;
}

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

        // M7.4 — pre-collect every live (non-disabled) LocalPlayer
        // ship's position + factionId so the per-fire arc test below
        // is O(N) per shot. Bounded at 16 because the existing demo
        // caps ships at ≤16 alive (matches BotControlSystem's `live`
        // buffer). A larger arena would need a dynamic vector — but
        // tou2d isn't that.
        std::array<AllyPos, 16> allies{};
        std::size_t             allyCount = 0;
        if (idsLp.valid()) {
            for (const auto* chunkPtr : view.chunks()) {
                if (!chunkPtr) continue;
                const auto& chunk = *chunkPtr;
                if (!chunk.mask.has(idsLp.componentBit()))              continue;
                if (!chunk.mask.has(threadmaxx::Component::Transform))  continue;
                if (chunk.mask.has(threadmaxx::Component::DisabledTag)) continue;
                const auto& transforms = chunk.transforms;
                const auto  entities   = chunk.entities;
                const auto  lpSpan     = threadmaxx::user::chunkSpan<LocalPlayer>(chunk, idsLp);
                for (std::size_t row = 0, n = entities.size(); row < n; ++row) {
                    if (allyCount >= allies.size()) break;
                    allies[allyCount].x         = transforms[row].position.x;
                    allies[allyCount].y         = transforms[row].position.y;
                    allies[allyCount].factionId = lpSpan[row].factionId;
                    allies[allyCount].selfIdx   = entities[row].index;
                    ++allyCount;
                }
            }
        }
        const std::span<const AllyPos> allyView{allies.data(), allyCount};

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

                // M7.4 — friendly-fire gate. Only bots are filtered;
                // human shots stay unrestricted per the M7.4 design
                // note ("friendly fire may exist as game rule" — only
                // for manual human aim). Computed once per ship per
                // tick and reused by both fire branches below.
                const bool isBot = hasLp && lpSpan[row].isBot != 0;
                const std::uint8_t selfFaction =
                    hasLp ? lpSpan[row].factionId : kFactionAuto;
                const bool suppressBotShot = isBot &&
                    botShotHitsAlly(allyView,
                                    shipT.position.x, shipT.position.y,
                                    angle, selfFaction,
                                    entities[row].index);

                // ---- Tick reloads + loader cooldowns down first -----
                // 2026-05-28 — dumbfire never reloads (infinite ammo
                // on the basic weapon per M5.2). Only the special
                // reload chain ticks here.
                const SpecialWeaponSpec& spec = specialSpecAt(ld.specialKind);
                if (ld.specialReloadIn > 0) {
                    ld.specialReloadIn = static_cast<std::uint16_t>(ld.specialReloadIn - 1);
                    if (ld.specialReloadIn == 0) {
                        ld.specialAmmo = spec.magazine;
                    }
                }
                // M4.5 — independent per-shot loader cooldown. Ticks
                // every step regardless of input; fire is gated on it
                // reaching 0.
                if (ld.dumbfireCooldown > 0) {
                    ld.dumbfireCooldown = static_cast<std::uint16_t>(ld.dumbfireCooldown - 1);
                }
                if (ld.specialCooldown > 0) {
                    ld.specialCooldown = static_cast<std::uint16_t>(ld.specialCooldown - 1);
                }

                // ---- Dumbfire (fireBasic) ---------------------------
                // 2026-05-28 — infinite ammo, no reload. Gate is just
                // "input held AND per-shot cooldown ready". `ld.dumbfireAmmo`
                // stays pinned at the magazine size for HUD compat.
                if (in.fireBasic && ld.dumbfireCooldown == 0 && !suppressBotShot) {
                    spawnBullet(ctx, cb, idsBl, shipT, shipV,
                                angle,
                                kMuzzleSpeed,
                                kBulletTtlSeconds,
                                kDumbfireDamage,
                                /*weaponKind*/ 0,
                                /*bouncesLeft*/ 0,
                                ownerSlot);
                    ld.dumbfireCooldown = kDumbfireCooldownTicks;
                    if (engine_) {
                        engine_->events<AudioPlay>().emit(
                            AudioPlay{audio::kSoundDumbfire, 0, 0});
                    }
                }

                // ---- Special (fireSpecial) --------------------------
                // M5.6 — dispatches on `specialKind` to the per-kind
                // spec table. Single-bullet kinds (Rapid/Sniper) fall
                // out of the fan loop with bulletsPerShot = 1 and
                // spreadStepRad = 0 → one straight shot. Multi-bullet
                // kinds (Spread/Quintet) center the fan around the
                // ship's forward.
                const bool canSpecial =
                    ld.specialReloadIn == 0 &&
                    ld.specialCooldown == 0 &&
                    ld.specialAmmo > 0;
                if (in.fireSpecial && canSpecial && !suppressBotShot) {
                    const int n      = static_cast<int>(spec.bulletsPerShot);
                    const int half   = (n - 1) / 2;
                    const bool even  = (n % 2) == 0;
                    for (int i = 0; i < n; ++i) {
                        // Center the fan: odd-N puts bullet 0 dead-
                        // ahead and pairs flank it (±step, ±2*step…);
                        // even-N stamps ±step/2, ±3*step/2… so the
                        // pattern stays symmetric about the forward.
                        const int   midOffset = n / 2;
                        const float lane = even
                            ? (static_cast<float>(i - midOffset) + 0.5f)
                            : static_cast<float>(i - half);
                        spawnBullet(ctx, cb, idsBl, shipT, shipV,
                                    angle + lane * spec.spreadStepRad,
                                    spec.muzzleSpeed,
                                    spec.ttlSeconds,
                                    spec.damagePerBullet,
                                    spec.weaponKind,
                                    spec.bouncesLeft,
                                    ownerSlot);
                    }
                    ld.specialAmmo     = static_cast<std::uint16_t>(ld.specialAmmo - 1);
                    ld.specialCooldown = spec.cooldownTicks;
                    if (ld.specialAmmo == 0) {
                        ld.specialReloadIn = spec.reloadTicks;
                    }
                    if (engine_) {
                        // M5.6 — every special routes through the same
                        // audio cue for now (one bank slot). A per-
                        // kind audio table can land later — out of
                        // scope for this batch.
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
