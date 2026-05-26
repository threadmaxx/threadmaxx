#include "TerrainCollisionSystem.hpp"

#include "BulletTerrainSystem.hpp"

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

/// M3.2 — crash-damage threshold. Below this impact speed the contact
/// is treated as "gentle nudge / slide along wall" and no damage is
/// applied; above, both ship and tile take a chip. Tuned so air-damp +
/// gravity steady-state slides don't constantly tick down ship HP.
constexpr float kCrashImpactSpeed = 60.0f;        ///< wu/s
constexpr float kCrashShipDamageCoeff = 0.06f;    ///< hp per (wu/s) over threshold
constexpr float kCrashShipDamageMax   = 18.0f;    ///< clamp single hit so brushing isn't fatal
constexpr std::uint8_t kCrashTileChipDamage = 8;  ///< vs Dumbfire's 64 — ≈ 1/8 weapon damage

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

        const auto tbSpan   = threadmaxx::user::chunkSpan<TerrainBlock>(chunk, idsTb);
        const auto entities = chunk.entities;
        for (std::size_t row = 0; row < tbSpan.size(); ++row) {
            const auto& blk = tbSpan[row];
            grid_[packCell(blk.cellX, blk.cellY)] = TileCell{
                /*handle*/ entities[row],
                /*hp*/     blk.hp,
                /*attr*/   blk.attr,
                /*cellX*/  blk.cellX,
                /*cellY*/  blk.cellY,
            };
        }
    }

    built_ = true;
}

void TerrainCollisionSystem::update(threadmaxx::SystemContext& ctx) {
    const auto idsPi   = ids_.playerInput;
    const auto idsShip = ids_.ship;
    const auto idsTb   = ids_.terrainBlock;
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

            const bool hasShipComp = idsShip.valid() &&
                                     chunk.mask.has(idsShip.componentBit());
            const auto shipSpan = hasShipComp
                ? threadmaxx::user::chunkSpan<Ship>(chunk, idsShip)
                : std::span<const Ship>{};

            const auto entities    = chunk.entities;
            const auto& positions  = chunk.transforms;
            const auto& velocities = chunk.velocities;
            const std::size_t n    = entities.size();

            for (std::size_t row = 0; row < n; ++row) {
                threadmaxx::Transform t = positions[row];
                threadmaxx::Velocity  v = velocities[row];

                // Accumulated crash damage to apply to ship HP this
                // tick — sum over every contact this ship had so a
                // wedge-into-corner deals damage from both axes.
                float shipDamageThisTick = 0.0f;

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
                            const auto packed = packCell(cx + dx, cy + dy);
                            const auto it = grid_.find(packed);
                            if (it == grid_.end()) continue;
                            if (it->second.attr != Attribute::Solid) continue;

                            const float tileCenterX = cellToWorld(cx + dx);
                            const float tileCenterY = cellToWorld(cy + dy);
                            const float tileHalf    = kTileWorldUnits * 0.5f;
                            const float shipHalf    = kShipCollisionHalfExtent;

                            const float dxOverlap = (tileHalf + shipHalf) -
                                                    std::fabs(t.position.x - tileCenterX);
                            const float dyOverlap = (tileHalf + shipHalf) -
                                                    std::fabs(t.position.y - tileCenterY);
                            if (dxOverlap <= 0.0f || dyOverlap <= 0.0f) continue;

                            // Push out along the axis of least
                            // penetration; capture the impact speed in
                            // that axis BEFORE we zero the velocity so
                            // crash-damage scales with how hard we hit.
                            float impactSpeed = 0.0f;
                            if (dxOverlap < dyOverlap) {
                                const float sign = (t.position.x > tileCenterX) ? +1.0f : -1.0f;
                                t.position.x += sign * dxOverlap;
                                if ((sign > 0 && v.linear.x < 0) ||
                                    (sign < 0 && v.linear.x > 0)) {
                                    impactSpeed = std::fabs(v.linear.x);
                                    v.linear.x  = 0.0f;
                                }
                            } else {
                                const float sign = (t.position.y > tileCenterY) ? +1.0f : -1.0f;
                                t.position.y += sign * dyOverlap;
                                if ((sign > 0 && v.linear.y < 0) ||
                                    (sign < 0 && v.linear.y > 0)) {
                                    impactSpeed = std::fabs(v.linear.y);
                                    v.linear.y  = 0.0f;
                                }
                            }
                            moved = true;

                            // ---- Crash damage --------------------------------
                            // Only fires above the threshold so a ship
                            // resting on a floor under gravity doesn't
                            // bleed HP forever.
                            if (impactSpeed > kCrashImpactSpeed) {
                                const float over = impactSpeed - kCrashImpactSpeed;
                                const float dmg  = std::min(
                                    over * kCrashShipDamageCoeff,
                                    kCrashShipDamageMax);
                                shipDamageThisTick += dmg;

                                // Tile chip damage — saturating sub on
                                // u8. Bedrock (hp == 0xFF) is immune so
                                // the perimeter / arena walls stay put.
                                auto& cellRef = it->second;
                                if (cellRef.hp != 0xFF) {
                                    if (cellRef.hp <= kCrashTileChipDamage) {
                                        const auto destroyedX = cellRef.cellX;
                                        const auto destroyedY = cellRef.cellY;
                                        cb.destroy(cellRef.handle);
                                        if (destroyCb_) destroyCb_(destroyedX, destroyedY);
                                        grid_.erase(packed);
                                        // Bullet system's index still
                                        // points at the dead handle —
                                        // mark it stale so the next
                                        // preStep rebuilds.
                                        if (bullets_) bullets_->markDirty();
                                    } else {
                                        cellRef.hp = static_cast<std::uint8_t>(
                                            cellRef.hp - kCrashTileChipDamage);
                                        TerrainBlock updated{};
                                        updated.attr  = cellRef.attr;
                                        updated.hp    = cellRef.hp;
                                        updated.cellX = cellRef.cellX;
                                        updated.cellY = cellRef.cellY;
                                        threadmaxx::addUserComponent(
                                            cb, idsTb, cellRef.handle, updated);
                                    }
                                }
                            }
                        }
                    }
                    if (!moved) break;
                    anyMoved = true;
                }

                if (anyMoved) {
                    cb.setTransform(entities[row], t);
                    cb.setVelocity(entities[row], v);
                }

                // ---- Apply ship damage ---------------------------------------
                if (hasShipComp && shipDamageThisTick > 0.0f) {
                    Ship ship = shipSpan[row];
                    ship.currentHp -= shipDamageThisTick;
                    if (ship.currentHp < 0.0f) ship.currentHp = 0.0f;
                    threadmaxx::addUserComponent(cb, idsShip, entities[row], ship);
                }
            }
        }
    });
}

} // namespace tou2d
