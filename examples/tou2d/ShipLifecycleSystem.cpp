#include "ShipLifecycleSystem.hpp"

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Query.hpp>
#include <threadmaxx/World.hpp>
#include <threadmaxx/render/DebugGeometry.hpp>
#include <threadmaxx/render/RenderFrameBuilder.hpp>

#include <cmath>

namespace tou2d {

namespace {

constexpr std::array<std::uint32_t, 4> kSlotSparkColors = {
    0xFF0000FFu,  // P1 red
    0xFFFF0000u,  // P2 blue
    0xFF00FF00u,  // P3 green
    0xFF00FFFFu,  // P4 yellow
};

constexpr int   kSparkRays     = 8;
constexpr float kSparkMinRWU   = 6.0f;    // inner radius (world units)
constexpr float kSparkMaxRWU   = 28.0f;   // outer radius at frame 1

} // namespace

ShipLifecycleSystem::ShipLifecycleSystem(UserComponentIds ids) noexcept
    : ids_(ids) {}

void ShipLifecycleSystem::update(threadmaxx::SystemContext& ctx) {
    const auto idsShip = ids_.ship;
    const auto idsLp   = ids_.localPlayer;
    const auto idsLd   = ids_.loadout;
    if (!idsShip.valid()) return;

    // M4.3 — latch mode at top of tick.
    const MatchMode mode =
        matchMode_ ? *matchMode_ : MatchMode::Deathmatch;
    const bool lss = mode == MatchMode::LastShipStanding;

    // Tick down existing sparks first (so a fresh spark this tick lands
    // at full ticksLeft and doesn't get immediately decremented).
    for (auto& sp : sparks_) {
        if (sp.ticksLeft > 0) sp.ticksLeft = static_cast<std::uint16_t>(sp.ticksLeft - 1);
    }

    ctx.single([&](threadmaxx::Range /*r*/, threadmaxx::CommandBuffer& cb) {
        const auto& view = ctx.worldView();
        for (const auto* chunkPtr : view.chunks()) {
            if (!chunkPtr) continue;
            const auto& chunk = *chunkPtr;
            if (!chunk.mask.has(idsShip.componentBit()))             continue;
            if (!chunk.mask.has(threadmaxx::Component::Transform))   continue;
            if (!chunk.mask.has(threadmaxx::Component::Velocity))    continue;

            const bool disabled =
                chunk.mask.has(threadmaxx::Component::DisabledTag);
            const bool hasLp = idsLp.valid() && chunk.mask.has(idsLp.componentBit());
            const auto lpSpan = hasLp
                ? threadmaxx::user::chunkSpan<LocalPlayer>(chunk, idsLp)
                : std::span<const LocalPlayer>{};

            const auto shipSpan = threadmaxx::user::chunkSpan<Ship>(chunk, idsShip);
            const auto entities = chunk.entities;
            const auto& positions  = chunk.transforms;
            const auto& velocities = chunk.velocities;
            const std::size_t n    = entities.size();

            for (std::size_t row = 0; row < n; ++row) {
                Ship ship = shipSpan[row];

                if (!disabled) {
                    if (ship.currentHp > 0.0f) continue;

                    // ---- Begin death --------------------------------
                    // M4.3 — in LSS, death is permanent for the round.
                    // Stamp the sentinel so the Disabled-chunk branch
                    // below knows not to count down / respawn.
                    ship.respawnIn = lss ? kPermanentDeathSentinel
                                         : kRespawnTicks;
                    threadmaxx::addUserComponent(cb, idsShip, entities[row], ship);

                    threadmaxx::Velocity v = velocities[row];
                    v.linear  = {0.0f, 0.0f, 0.0f};
                    v.angular = {0.0f, 0.0f, 0.0f};
                    cb.setVelocity(entities[row], v);

                    cb.addTag(entities[row], threadmaxx::Component::DisabledTag);

                    // ---- Death spark --------------------------------
                    // Round-robin into the spark ring; on overflow the
                    // oldest spark is overwritten — fine, we want the
                    // freshest deaths to be the most visible.
                    const std::uint8_t slot =
                        hasLp ? lpSpan[row].slot : std::uint8_t{0};
                    const std::uint32_t col =
                        slot < kSlotSparkColors.size()
                            ? kSlotSparkColors[slot]
                            : 0xFFFFFFFFu;
                    auto& sp = sparks_[nextSpark_];
                    sp.x         = positions[row].position.x;
                    sp.y         = positions[row].position.y;
                    sp.color     = col;
                    sp.ticksLeft = kSparkTicks;
                    nextSpark_   = (nextSpark_ + 1u) %
                                   static_cast<std::uint32_t>(sparks_.size());
                    continue;
                }

                // ---- Disabled chunk: tick down respawn ---------------
                // M4.3 — LSS permanent-death sentinel pins the ship in
                // Disabled-limbo forever (until RoundRestartSystem
                // resets it on the next round).
                if (ship.respawnIn == kPermanentDeathSentinel) {
                    continue;
                }
                if (ship.respawnIn > 1) {
                    ship.respawnIn = static_cast<std::uint16_t>(ship.respawnIn - 1);
                    threadmaxx::addUserComponent(cb, idsShip, entities[row], ship);
                    continue;
                }

                // respawnIn == 0 inside a Disabled chunk shouldn't
                // happen in normal flow, but treat it the same as the
                // last-tick case so we self-heal.
                //
                // M4.4 — pick a random Air cell from the terrain as
                // the respawn target. Failing that, fall back to the
                // original (spawnX, spawnY) so the ship still comes
                // back somewhere known.
                float rx = ship.spawnX;
                float ry = ship.spawnY;
                if (grid_) {
                    sampleRandomRespawn(*grid_, rng_, rx, ry);
                }
                threadmaxx::Transform t = positions[row];
                t.position.x = rx;
                t.position.y = ry;
                t.position.z = 0.0f;
                cb.setTransform(entities[row], t);

                threadmaxx::Velocity v{};
                cb.setVelocity(entities[row], v);

                ship.currentHp = ship.maxHp;
                ship.respawnIn = 0;
                threadmaxx::addUserComponent(cb, idsShip, entities[row], ship);

                // M4.2 — refresh weapon state on respawn so the player
                // comes back with a full magazine and zero reload
                // pending. Without this, a ship that died mid-reload
                // would respawn with whatever ammo/reload counters it
                // had at death tick — uncomfortable inconsistency.
                if (idsLd.valid()) {
                    WeaponLoadout fresh{};
                    fresh.dumbfireAmmo     = kDumbfireMagazine;
                    fresh.dumbfireReloadIn = 0;
                    fresh.spreadAmmo       = kSpreadMagazine;
                    fresh.spreadReloadIn   = 0;
                    threadmaxx::addUserComponent(cb, idsLd, entities[row], fresh);
                }

                cb.removeTag(entities[row], threadmaxx::Component::DisabledTag);
            }
        }
    });
}

void ShipLifecycleSystem::buildRenderFrame(threadmaxx::RenderFrameBuilder& b) {
    constexpr float kTwoPi = 6.28318530718f;
    for (const auto& sp : sparks_) {
        if (sp.ticksLeft == 0) continue;

        // Linear envelope: ticksLeft = kSparkTicks ⇒ frac = 1 (full
        // outer radius, brightest); ticksLeft = 0 ⇒ frac = 0 (vanished).
        const float frac = static_cast<float>(sp.ticksLeft) /
                           static_cast<float>(kSparkTicks);
        const float outerR = kSparkMinRWU + (kSparkMaxRWU - kSparkMinRWU) * frac;

        // Fade alpha proportionally — pack into 0xAABBGGRR while leaving
        // RGB intact. Start from full opacity at frac=1.
        const std::uint32_t a8 = static_cast<std::uint32_t>(
            std::min(255.0f, std::max(0.0f, frac * 255.0f)));
        const std::uint32_t col = (sp.color & 0x00FFFFFFu) | (a8 << 24);

        for (int i = 0; i < kSparkRays; ++i) {
            const float theta = (kTwoPi * static_cast<float>(i)) /
                                static_cast<float>(kSparkRays);
            const float cx = std::cos(theta);
            const float sy = std::sin(theta);
            threadmaxx::DebugLine ray{};
            ray.a         = {sp.x + cx * kSparkMinRWU, sp.y + sy * kSparkMinRWU, 0.0f};
            ray.b         = {sp.x + cx * outerR,       sp.y + sy * outerR,       0.0f};
            ray.colorRGBA = col;
            b.addDebugLine(ray);
        }
    }
}

} // namespace tou2d
