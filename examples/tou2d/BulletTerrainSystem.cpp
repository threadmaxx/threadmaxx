#include "BulletTerrainSystem.hpp"

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
        std::array<std::uint32_t, 16> tilesDelta{};

        for (const auto* chunkPtr : view.chunks()) {
            if (!chunkPtr) continue;
            const auto& chunk = *chunkPtr;
            if (!chunk.mask.has(idsBl.componentBit()))             continue;
            if (!chunk.mask.has(threadmaxx::Component::Transform)) continue;

            const auto blSpan = threadmaxx::user::chunkSpan<Bullet>(chunk, idsBl);
            const auto entities    = chunk.entities;
            const auto& positions  = chunk.transforms;
            const std::size_t n    = entities.size();

            for (std::size_t row = 0; row < n; ++row) {
                const auto& bp = positions[row].position;
                const std::int32_t cx = worldToCell(bp.x);
                const std::int32_t cy = worldToCell(bp.y);

                if (!grid_->inBounds(cx, cy)) continue;
                const std::uint8_t cellHp = grid_->hpAt(cx, cy);
                if (cellHp == 0) continue;  // Air — bullet flies on

                if (grid_->attrAt(cx, cy) != Attribute::Solid) continue;

                // Bedrock — bullet stops, tile survives.
                if (cellHp == 0xFF) {
                    cb.destroy(entities[row]);
                    continue;
                }

                const Bullet& blt = blSpan[row];
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
