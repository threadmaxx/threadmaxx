#include "TerrainCollisionSystem.hpp"

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Query.hpp>
#include <threadmaxx/World.hpp>

#include <cmath>

namespace tou2d {

namespace {

/// Ship half-extent used for collision. Slightly smaller than the
/// visual scale (28 / 2 = 14) so the ship can squeeze through a
/// tile-wide corridor with a little visual overlap rather than wedging
/// at the doorway. Tuned with the synthetic arena; M2.7+ uses the
/// imported config's `ship_size` field.
constexpr float kShipCollisionHalfExtent = 11.0f;

/// Convert a world position to an integer grid cell. Floor-style:
/// (-tile/2, +tile/2) → 0; (+tile/2, +3tile/2) → 1; etc. The terrain
/// block at cell (cx, cy) has center (cx*tile, cy*tile) and AABB
/// [cx*tile - tile/2, cx*tile + tile/2] × same in y. With tile=28,
/// floor((wx + tile/2) / tile) round-trips correctly.
inline std::int32_t worldToCell(float wx) noexcept {
    return static_cast<std::int32_t>(
        std::floor((wx + kTileWorldUnits * 0.5f) / kTileWorldUnits));
}

inline float cellToWorld(std::int32_t c) noexcept {
    return static_cast<float>(c) * kTileWorldUnits;
}

} // namespace

TerrainCollisionSystem::TerrainCollisionSystem(UserComponentIds ids) noexcept
    : ids_(ids) {}

void TerrainCollisionSystem::preStep(threadmaxx::SystemContext& ctx) {
    if (built_) return;

    const auto idsTb = ids_.terrainBlock;
    if (!idsTb.valid()) return;

    grid_.clear();

    const auto& view = ctx.worldView();
    for (const auto* chunkPtr : view.chunks()) {
        if (!chunkPtr) continue;
        const auto& chunk = *chunkPtr;
        if (!chunk.mask.has(idsTb.componentBit())) continue;

        const auto tbSpan = threadmaxx::user::chunkSpan<TerrainBlock>(chunk, idsTb);
        for (std::size_t row = 0; row < tbSpan.size(); ++row) {
            const auto& blk = tbSpan[row];
            grid_[packCell(blk.cellX, blk.cellY)] = blk.attr;
        }
    }

    built_ = true;
}

void TerrainCollisionSystem::update(threadmaxx::SystemContext& ctx) {
    const auto idsPi = ids_.playerInput;
    if (!idsPi.valid()) return;
    if (grid_.empty()) return;

    ctx.single([&](threadmaxx::Range /*r*/, threadmaxx::CommandBuffer& cb) {
        const auto& view = ctx.worldView();
        for (const auto* chunkPtr : view.chunks()) {
            if (!chunkPtr) continue;
            const auto& chunk = *chunkPtr;
            if (!chunk.mask.has(threadmaxx::Component::Transform))   continue;
            if (!chunk.mask.has(threadmaxx::Component::Velocity))    continue;
            if (!chunk.mask.has(idsPi.componentBit()))               continue;

            const auto entities    = chunk.entities;
            const auto& positions  = chunk.transforms;
            const auto& velocities = chunk.velocities;
            const std::size_t n    = entities.size();

            for (std::size_t row = 0; row < n; ++row) {
                threadmaxx::Transform t = positions[row];
                threadmaxx::Velocity  v = velocities[row];

                const std::int32_t cx = worldToCell(t.position.x);
                const std::int32_t cy = worldToCell(t.position.y);

                // Sample a (2*range+1)² neighborhood sized to cover
                // any tile whose AABB can overlap the ship at its
                // current cell. range = ceil((shipHalf + tileHalf) /
                // tileSize) — at the M3.1 default (px=32 → tile=28)
                // this collapses to 3×3; at finer tile granularity it
                // grows automatically so collision keeps working.
                // Iterate multiple times so a wedge between two solid
                // tiles is resolved cleanly (axis-of-least-penetration
                // push-out can swap the dominant axis after the first
                // resolve).
                const int range = static_cast<int>(std::ceil(
                    (kShipCollisionHalfExtent + kTileWorldUnits * 0.5f) /
                    kTileWorldUnits));
                bool anyMoved = false;
                for (int pass = 0; pass < 2; ++pass) {
                    bool moved = false;
                    for (int dy = -range; dy <= range; ++dy) {
                        for (int dx = -range; dx <= range; ++dx) {
                            const auto it = grid_.find(packCell(cx + dx, cy + dy));
                            if (it == grid_.end()) continue;
                            if (it->second != Attribute::Solid) continue;

                            const float tileCenterX = cellToWorld(cx + dx);
                            const float tileCenterY = cellToWorld(cy + dy);
                            const float tileHalf    = kTileWorldUnits * 0.5f;
                            const float shipHalf    = kShipCollisionHalfExtent;

                            const float dxOverlap = (tileHalf + shipHalf) -
                                                    std::fabs(t.position.x - tileCenterX);
                            const float dyOverlap = (tileHalf + shipHalf) -
                                                    std::fabs(t.position.y - tileCenterY);
                            if (dxOverlap <= 0.0f || dyOverlap <= 0.0f) continue;

                            // Push out along the axis of least penetration.
                            if (dxOverlap < dyOverlap) {
                                const float sign = (t.position.x > tileCenterX) ? +1.0f : -1.0f;
                                t.position.x += sign * dxOverlap;
                                if ((sign > 0 && v.linear.x < 0) ||
                                    (sign < 0 && v.linear.x > 0)) {
                                    v.linear.x = 0.0f;
                                }
                            } else {
                                const float sign = (t.position.y > tileCenterY) ? +1.0f : -1.0f;
                                t.position.y += sign * dyOverlap;
                                if ((sign > 0 && v.linear.y < 0) ||
                                    (sign < 0 && v.linear.y > 0)) {
                                    v.linear.y = 0.0f;
                                }
                            }
                            moved = true;
                        }
                    }
                    if (!moved) break;
                    anyMoved = true;
                }

                if (anyMoved) {
                    cb.setTransform(entities[row], t);
                    cb.setVelocity(entities[row], v);
                }
            }
        }
    });
}

} // namespace tou2d
