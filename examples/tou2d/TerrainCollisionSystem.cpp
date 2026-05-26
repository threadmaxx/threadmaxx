#include "TerrainCollisionSystem.hpp"

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Query.hpp>
#include <threadmaxx/World.hpp>

#include <cmath>

namespace tou2d {

namespace {

constexpr float kShipCollisionHalfExtent = 11.0f;

/// M3.2 — crash-damage threshold.
constexpr float kCrashImpactSpeed       = 60.0f;
constexpr float kCrashShipDamageCoeff   = 0.06f;
constexpr float kCrashShipDamageMax     = 18.0f;
constexpr std::uint8_t kCrashTileChipDamage = 8;

inline std::int32_t worldToCell(float wx) noexcept {
    return static_cast<std::int32_t>(
        std::floor((wx + kTileWorldUnits * 0.5f) / kTileWorldUnits));
}

inline float cellToWorld(std::int32_t c) noexcept {
    return static_cast<float>(c) * kTileWorldUnits;
}

} // namespace

TerrainCollisionSystem::TerrainCollisionSystem(UserComponentIds ids,
                                               TerrainGrid*     grid) noexcept
    : ids_(ids), grid_(grid) {}

void TerrainCollisionSystem::update(threadmaxx::SystemContext& ctx) {
    const auto idsPi   = ids_.playerInput;
    const auto idsShip = ids_.ship;
    if (!idsPi.valid()) return;
    if (!grid_)         return;

    ctx.single([&](threadmaxx::Range /*r*/, threadmaxx::CommandBuffer& cb) {
        const auto& view = ctx.worldView();
        for (const auto* chunkPtr : view.chunks()) {
            if (!chunkPtr) continue;
            const auto& chunk = *chunkPtr;
            if (!chunk.mask.has(threadmaxx::Component::Transform))   continue;
            if (!chunk.mask.has(threadmaxx::Component::Velocity))    continue;
            if (!chunk.mask.has(idsPi.componentBit()))               continue;
            // Dead/respawning ships are masked with DisabledTag — they
            // sit out of the physics step entirely.
            if (chunk.mask.has(threadmaxx::Component::DisabledTag))  continue;

            const bool hasShipComp = idsShip.valid() &&
                                     chunk.mask.has(idsShip.componentBit());
            const auto shipSpan = hasShipComp
                ? threadmaxx::user::chunkSpan<Ship>(chunk, idsShip)
                : std::span<const Ship>{};

            const auto entities    = chunk.entities;
            const auto& positions  = chunk.transforms;
            const auto& velocities = chunk.velocities;
            const std::size_t n    = entities.size();

            const int range = static_cast<int>(std::ceil(
                (kShipCollisionHalfExtent + kTileWorldUnits * 0.5f) /
                kTileWorldUnits));

            for (std::size_t row = 0; row < n; ++row) {
                threadmaxx::Transform t = positions[row];
                threadmaxx::Velocity  v = velocities[row];

                float shipDamageThisTick = 0.0f;

                const std::int32_t cx = worldToCell(t.position.x);
                const std::int32_t cy = worldToCell(t.position.y);

                bool anyMoved = false;
                for (int pass = 0; pass < 2; ++pass) {
                    bool moved = false;
                    for (int dy = -range; dy <= range; ++dy) {
                        for (int dx = -range; dx <= range; ++dx) {
                            const std::int32_t qcx = cx + dx;
                            const std::int32_t qcy = cy + dy;
                            if (grid_->attrAt(qcx, qcy) != Attribute::Solid) continue;
                            const std::uint8_t cellHp = grid_->hpAt(qcx, qcy);
                            if (cellHp == 0) continue;

                            const float tileCenterX = cellToWorld(qcx);
                            const float tileCenterY = cellToWorld(qcy);
                            const float tileHalf    = kTileWorldUnits * 0.5f;
                            const float shipHalf    = kShipCollisionHalfExtent;

                            const float dxOverlap = (tileHalf + shipHalf) -
                                                    std::fabs(t.position.x - tileCenterX);
                            const float dyOverlap = (tileHalf + shipHalf) -
                                                    std::fabs(t.position.y - tileCenterY);
                            if (dxOverlap <= 0.0f || dyOverlap <= 0.0f) continue;

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

                            if (impactSpeed > kCrashImpactSpeed) {
                                const float over = impactSpeed - kCrashImpactSpeed;
                                const float dmg  = std::min(
                                    over * kCrashShipDamageCoeff,
                                    kCrashShipDamageMax);
                                shipDamageThisTick += dmg;

                                // Bedrock immune. Otherwise saturating
                                // chip; destroy when hp <= chip.
                                if (cellHp != 0xFF) {
                                    if (cellHp <= kCrashTileChipDamage) {
                                        grid_->clear(qcx, qcy);
                                        if (destroyCb_) destroyCb_(qcx, qcy);
                                    } else {
                                        grid_->hp[grid_->indexOf(qcx, qcy)] =
                                            static_cast<std::uint8_t>(
                                                cellHp - kCrashTileChipDamage);
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
