#include "BulletTerrainSystem.hpp"

#include "TerrainCollisionSystem.hpp"

#include <threadmaxx/CommandBuffer.hpp>
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

BulletTerrainSystem::BulletTerrainSystem(UserComponentIds        ids,
                                         TerrainCollisionSystem* collision) noexcept
    : ids_(ids), collision_(collision) {}

void BulletTerrainSystem::preStep(threadmaxx::SystemContext& ctx) {
    if (!dirty_) return;

    const auto idsTb = ids_.terrainBlock;
    if (!idsTb.valid()) return;

    tileIndex_.clear();

    const auto& view = ctx.worldView();
    const auto chunkPtrs = view.chunks();
    for (std::size_t ci = 0, n = chunkPtrs.size(); ci < n; ++ci) {
        const auto* chunk = chunkPtrs[ci];
        if (!chunk) continue;
        if (!chunk->mask.has(idsTb.componentBit())) continue;

        const auto tbSpan   = threadmaxx::user::chunkSpan<TerrainBlock>(*chunk, idsTb);
        const auto entities = chunk->entities;
        for (std::size_t row = 0, m = entities.size(); row < m; ++row) {
            const auto& blk = tbSpan[row];
            tileIndex_[packCell(blk.cellX, blk.cellY)] =
                TileEntry{entities[row], ci, row};
        }
    }

    dirty_ = false;
}

void BulletTerrainSystem::update(threadmaxx::SystemContext& ctx) {
    const auto idsBl = ids_.bullet;
    const auto idsTb = ids_.terrainBlock;
    if (!idsBl.valid() || !idsTb.valid()) return;
    if (tileIndex_.empty())                return;

    ctx.single([&](threadmaxx::Range /*r*/, threadmaxx::CommandBuffer& cb) {
        const auto& view = ctx.worldView();
        for (const auto* chunkPtr : view.chunks()) {
            if (!chunkPtr) continue;
            const auto& chunk = *chunkPtr;
            if (!chunk.mask.has(idsBl.componentBit()))             continue;
            if (!chunk.mask.has(threadmaxx::Component::Transform)) continue;

            const auto blSpan      = threadmaxx::user::chunkSpan<Bullet>(chunk, idsBl);
            const auto entities    = chunk.entities;
            const auto& positions  = chunk.transforms;
            const std::size_t n    = entities.size();

            for (std::size_t row = 0; row < n; ++row) {
                const auto& bp = positions[row].position;
                const std::int32_t cx = worldToCell(bp.x);
                const std::int32_t cy = worldToCell(bp.y);

                const auto it = tileIndex_.find(packCell(cx, cy));
                if (it == tileIndex_.end()) continue;

                const auto& entry = it->second;
                const auto* tileChunk = view.chunks()[entry.chunkIndex];
                if (!tileChunk) continue;

                const auto tbSpan =
                    threadmaxx::user::chunkSpan<TerrainBlock>(*tileChunk, idsTb);
                if (entry.rowInChunk >= tbSpan.size()) continue;
                TerrainBlock blk = tbSpan[entry.rowInChunk];
                if (blk.attr != Attribute::Solid) continue;

                // Bedrock — bullet stops, tile survives. Bullet still
                // gets consumed so the player gets visual feedback.
                if (blk.hp == 0xFF) {
                    cb.destroy(entities[row]);
                    continue;
                }

                const std::uint8_t dmg = blSpan[row].damage;
                if (blk.hp <= dmg) {
                    // Tile destroyed. Paint that cell black in the
                    // backing JPG bitmap (host wires this), drop the
                    // collision entity, and mark our cache + the
                    // ship-collision grid stale so both rebuild on
                    // next tick.
                    cb.destroy(entry.handle);
                    if (destroyCb_) destroyCb_(blk.cellX, blk.cellY);
                    dirty_ = true;
                    if (collision_) collision_->invalidate();
                } else {
                    blk.hp = static_cast<std::uint8_t>(blk.hp - dmg);
                    threadmaxx::addUserComponent(cb, idsTb, entry.handle, blk);
                }

                cb.destroy(entities[row]);
            }
        }
    });
}

} // namespace tou2d
