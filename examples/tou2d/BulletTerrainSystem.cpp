#include "BulletTerrainSystem.hpp"

#include "ParticleSystem.hpp"

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Engine.hpp>
#include <threadmaxx/EventChannel.hpp>
#include <threadmaxx/Query.hpp>
#include <threadmaxx/World.hpp>

#include <cmath>

namespace tou2d {

namespace {

inline std::int32_t worldToCell(float wx) noexcept {
    return static_cast<std::int32_t>(
        std::floor((wx + kTileWorldUnits * 0.5f) / kTileWorldUnits));
}

inline float cellCenterWorld(std::int32_t cell) noexcept {
    return static_cast<float>(cell) * kTileWorldUnits;
}

} // namespace

BulletTerrainSystem::BulletTerrainSystem(UserComponentIds    ids,
                                         TerrainGrid*        grid,
                                         threadmaxx::Engine* engine) noexcept
    : ids_(ids), grid_(grid), engine_(engine) {}

void BulletTerrainSystem::update(threadmaxx::SystemContext& ctx) {
    const auto idsBl   = ids_.bullet;
    const auto idsShip = ids_.ship;
    if (!idsBl.valid()) return;
    if (!grid_)         return;

    ctx.single([&](threadmaxx::Range /*r*/, threadmaxx::CommandBuffer& cb) {
        const auto& view = ctx.worldView();

        // First pass: tally up per-owner tile-destroy counts so we can
        // credit them back to the shooter even though ships live in
        // different chunks. Kills (deathmatch score) are handled by
        // BulletShipCollisionSystem; this only tracks the secondary
        // `tilesDestroyed` wreckage counter.
        std::array<std::uint32_t, kMaxPlayerSlots> tilesDelta{};

        for (const auto* chunkPtr : view.chunks()) {
            if (!chunkPtr) continue;
            const auto& chunk = *chunkPtr;
            if (!chunk.mask.has(idsBl.componentBit()))             continue;
            if (!chunk.mask.has(threadmaxx::Component::Transform)) continue;

            const auto blSpan = threadmaxx::user::chunkSpan<Bullet>(chunk, idsBl);
            const auto entities    = chunk.entities;
            const auto& positions  = chunk.transforms;
            const std::size_t n    = entities.size();

            const bool hasVel = chunk.mask.has(threadmaxx::Component::Velocity);
            const auto* velPtr = hasVel ? &chunk.velocities : nullptr;

            for (std::size_t row = 0; row < n; ++row) {
                const auto& bp = positions[row].position;
                const std::int32_t cx = worldToCell(bp.x);
                const std::int32_t cy = worldToCell(bp.y);

                if (!grid_->inBounds(cx, cy)) continue;
                const std::uint8_t cellHp = grid_->hpAt(cx, cy);
                const Attribute    cellAttr = grid_->attrAt(cx, cy);

                // N3 (2026-06-18) — water entry. Water cells carry
                // hp == 0 (so they're not destructible) but a distinct
                // `Attribute::Water`. Pre-N3 the `hp == 0` early-out
                // matched both Air AND Water — bullets flew through
                // water silently. N3: bullet hits water → splash
                // particles + audio + bullet consumed (matches the
                // visual intuition that water absorbs the projectile).
                if (cellAttr == Attribute::Water) {
                    if (particles_) {
                        particles_->emitWaterSplash(
                            cellCenterWorld(cx),
                            cellCenterWorld(cy),
                            /*intensity=*/1.0f);
                    }
                    if (engine_) {
                        engine_->events<AudioPlay>().emit(
                            AudioPlay{audio::kSoundWaterSplash, 0, 0});
                    }
                    cb.destroy(entities[row]);
                    continue;
                }

                if (cellHp == 0) continue;  // Air — bullet flies on

                if (cellAttr != Attribute::Solid) continue;

                const Bullet& blt = blSpan[row];

                // M5.8 — Bouncer: reflect off the surface instead of
                // destroying. Bedrock and non-bouncer kinds still take
                // the regular destroy path below. Reflection picks the
                // surface-normal axis by probing the two cells the
                // bullet would have crossed into to land here: whichever
                // neighbor is air, the bullet came from that direction
                // → reflect the matching velocity component. If BOTH
                // neighbors are solid (corner hit) we reflect both axes,
                // which kicks the bullet straight back.
                if (blt.weaponKind == 9 && blt.bouncesLeft > 0 &&
                    cellHp != 0xFF && hasVel) {
                    const auto& v = (*velPtr)[row].linear;
                    const std::int32_t backX = (v.x > 0.0f) ? cx - 1
                                             : (v.x < 0.0f) ? cx + 1 : cx;
                    const std::int32_t backY = (v.y > 0.0f) ? cy - 1
                                             : (v.y < 0.0f) ? cy + 1 : cy;
                    const bool xOpen = grid_->inBounds(backX, cy) &&
                                       grid_->hpAt(backX, cy) == 0;
                    const bool yOpen = grid_->inBounds(cx, backY) &&
                                       grid_->hpAt(cx, backY) == 0;
                    threadmaxx::Velocity newV = (*velPtr)[row];
                    if (xOpen)      { newV.linear.x = -v.x * kBouncerDamping; }
                    else if (yOpen) { newV.linear.y = -v.y * kBouncerDamping; }
                    else {
                        newV.linear.x = -v.x * kBouncerDamping;
                        newV.linear.y = -v.y * kBouncerDamping;
                    }
                    // Nudge the bullet back into the previous air cell
                    // so it doesn't re-collide next tick before
                    // MovementSystem advances it.
                    threadmaxx::Transform newT = positions[row];
                    if (xOpen)      { newT.position.x = cellCenterWorld(backX); }
                    else if (yOpen) { newT.position.y = cellCenterWorld(backY); }
                    else {
                        newT.position.x = cellCenterWorld(backX);
                        newT.position.y = cellCenterWorld(backY);
                    }
                    cb.setTransform(entities[row], newT);
                    cb.setVelocity(entities[row], newV);
                    Bullet nb = blt;
                    nb.bouncesLeft = static_cast<std::uint8_t>(nb.bouncesLeft - 1);
                    threadmaxx::addUserComponent(cb, idsBl, entities[row], nb);
                    continue;
                }

                // Bedrock — bullet stops, tile survives.
                if (cellHp == 0xFF) {
                    cb.destroy(entities[row]);
                    continue;
                }

                const std::uint8_t dmg = blt.damage;
                if (cellHp <= dmg) {
                    grid_->clear(cx, cy);
                    if (destroyCb_) destroyCb_(cx, cy);
                    if (blt.ownerSlot < tilesDelta.size()) {
                        tilesDelta[blt.ownerSlot] += 1;
                    }
                    if (engine_) {
                        engine_->events<AudioPlay>().emit(
                            AudioPlay{audio::kSoundTileBreak, 0, 0});
                    }
                    // M5.3 — dust burst at the broken cell's world
                    // center. Visually anchors the destruction to the
                    // affected tile rather than wherever the bullet
                    // happened to be when it triggered the break.
                    if (particles_) {
                        particles_->emitTileBreakDust(
                            cellCenterWorld(cx),
                            cellCenterWorld(cy));
                    }
                } else {
                    grid_->hp[grid_->indexOf(cx, cy)] =
                        static_cast<std::uint8_t>(cellHp - dmg);
                }

                cb.destroy(entities[row]);
            }
        }

        // Second pass: walk ship chunks; credit any tilesDestroyed
        // deltas accumulated above to the matching LocalPlayer slot.
        // Cheap (≤4 ships), and keeps the bullet loop clear of ship
        // lookups. Saturates at uint16 max.
        if (idsShip.valid()) {
            for (const auto* chunkPtr : view.chunks()) {
                if (!chunkPtr) continue;
                const auto& chunk = *chunkPtr;
                if (!chunk.mask.has(idsShip.componentBit()))                   continue;
                if (!chunk.mask.has(ids_.localPlayer.componentBit()))          continue;

                const auto shipSpan = threadmaxx::user::chunkSpan<Ship>(chunk, idsShip);
                const auto lpSpan   = threadmaxx::user::chunkSpan<LocalPlayer>(
                    chunk, ids_.localPlayer);
                const auto entities = chunk.entities;
                for (std::size_t row = 0, m = entities.size(); row < m; ++row) {
                    const std::uint8_t slot = lpSpan[row].slot;
                    if (slot >= tilesDelta.size())   continue;
                    if (tilesDelta[slot] == 0)       continue;
                    Ship ship = shipSpan[row];
                    const std::uint32_t sum =
                        static_cast<std::uint32_t>(ship.tilesDestroyed) +
                        tilesDelta[slot];
                    ship.tilesDestroyed =
                        sum > 0xFFFFu ? std::uint16_t{0xFFFFu}
                                      : static_cast<std::uint16_t>(sum);
                    threadmaxx::addUserComponent(cb, idsShip, entities[row], ship);
                }
            }
        }
    });
}

} // namespace tou2d
