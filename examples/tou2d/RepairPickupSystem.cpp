#include "RepairPickupSystem.hpp"

#include "ParticleSystem.hpp"

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Engine.hpp>
#include <threadmaxx/EventChannel.hpp>
#include <threadmaxx/Query.hpp>
#include <threadmaxx/World.hpp>

#include <algorithm>
#include <cmath>

namespace tou2d {

namespace {

/// Ship's half-extent for the AABB overlap test. Matches
/// `TerrainCollisionSystem::kShipCollisionHalfExtent` so a pickup
/// triggers the moment the ship visually contacts the tile, not earlier.
constexpr float kShipPickupHalfExtent = 11.0f;

inline std::int32_t worldToCell(float wx) noexcept {
    return static_cast<std::int32_t>(
        std::floor((wx + kTileWorldUnits * 0.5f) / kTileWorldUnits));
}

inline float cellToWorld(std::int32_t c) noexcept {
    return static_cast<float>(c) * kTileWorldUnits;
}

} // namespace

RepairPickupSystem::RepairPickupSystem(UserComponentIds    ids,
                                       TerrainGrid*        grid,
                                       threadmaxx::Engine* engine) noexcept
    : ids_(ids), grid_(grid), engine_(engine) {}

void RepairPickupSystem::update(threadmaxx::SystemContext& ctx) {
    pickupsThisStep_ = 0;
    const auto idsShip = ids_.ship;
    const auto idsLd   = ids_.loadout;
    if (!idsShip.valid()) return;
    if (!grid_)            return;

    // M7.5 — collected at the head of this step; swapped into
    // `onBasePrev_` at the end so the next step sees the correct
    // entry-edge state.
    std::unordered_set<std::uint32_t> onBaseNow;

    ctx.single([&](threadmaxx::Range /*r*/, threadmaxx::CommandBuffer& cb) {
        const auto& view = ctx.worldView();
        for (const auto* chunkPtr : view.chunks()) {
            if (!chunkPtr) continue;
            const auto& chunk = *chunkPtr;
            if (!chunk.mask.has(idsShip.componentBit()))             continue;
            if (!chunk.mask.has(threadmaxx::Component::Transform))   continue;
            // Disabled (dead) ships don't pick up.
            if (chunk.mask.has(threadmaxx::Component::DisabledTag))  continue;

            const auto shipSpan = threadmaxx::user::chunkSpan<Ship>(chunk, idsShip);
            const bool hasLd = idsLd.valid() && chunk.mask.has(idsLd.componentBit());
            const auto ldSpan = hasLd
                ? threadmaxx::user::chunkSpan<WeaponLoadout>(chunk, idsLd)
                : std::span<const WeaponLoadout>{};

            const auto entities    = chunk.entities;
            const auto& positions  = chunk.transforms;
            const std::size_t n    = entities.size();

            const int range = static_cast<int>(std::ceil(
                (kShipPickupHalfExtent + kTileWorldUnits * 0.5f) /
                kTileWorldUnits));

            for (std::size_t row = 0; row < n; ++row) {
                const auto& pos = positions[row].position;
                const std::int32_t cx = worldToCell(pos.x);
                const std::int32_t cy = worldToCell(pos.y);

                bool             overlapping = false;
                std::int32_t     hitCx       = 0;
                std::int32_t     hitCy       = 0;

                for (int dy = -range; dy <= range && !overlapping; ++dy) {
                    for (int dx = -range; dx <= range && !overlapping; ++dx) {
                        const std::int32_t qcx = cx + dx;
                        const std::int32_t qcy = cy + dy;
                        if (grid_->attrAt(qcx, qcy) != Attribute::RepairBase) continue;
                        if (grid_->hpAt(qcx, qcy) == 0)                       continue;

                        const float tileCenterX = cellToWorld(qcx);
                        const float tileCenterY = cellToWorld(qcy);
                        const float tileHalf    = kTileWorldUnits * 0.5f;
                        const float shipHalf    = kShipPickupHalfExtent;
                        if (std::fabs(pos.x - tileCenterX) > tileHalf + shipHalf) continue;
                        if (std::fabs(pos.y - tileCenterY) > tileHalf + shipHalf) continue;

                        overlapping = true;
                        hitCx       = qcx;
                        hitCy       = qcy;
                    }
                }
                if (!overlapping) continue;

                const std::uint32_t idx = entities[row].index;
                onBaseNow.insert(idx);
                const bool entryEdge = (onBasePrev_.find(idx) == onBasePrev_.end());

                // ---- Per-tick regen (always while overlapping) -------
                {
                    Ship ship = shipSpan[row];
                    if (ship.currentHp < ship.maxHp) {
                        ship.currentHp = std::min(
                            ship.maxHp,
                            ship.currentHp + kRepairBaseHpPerTick);
                        threadmaxx::addUserComponent(cb, idsShip, entities[row], ship);
                    }
                }

                // ---- Entry-edge: cycle special + cue ------------------
                if (entryEdge) {
                    ++pickupsThisStep_;
                    ++pickupsTotal_;

                    if (hasLd) {
                        WeaponLoadout ld = ldSpan[row];
                        ld.specialKind = static_cast<std::uint8_t>(
                            (ld.specialKind + 1u) % kSpecialKindCount);
                        const SpecialWeaponSpec& spec =
                            specialSpecAt(ld.specialKind);
                        ld.specialAmmo     = spec.magazine;
                        ld.specialReloadIn = 0;
                        ld.specialCooldown = 0;
                        threadmaxx::addUserComponent(cb, idsLd, entities[row], ld);
                    }

                    if (engine_) {
                        engine_->events<AudioPlay>().emit(
                            AudioPlay{audio::kSoundTileBreak, 0, 0});
                    }
                    if (particles_) {
                        particles_->emitTileBreakDust(
                            cellToWorld(hitCx), cellToWorld(hitCy));
                    }
                }
            }
        }
    });

    onBasePrev_.swap(onBaseNow);
}

} // namespace tou2d
