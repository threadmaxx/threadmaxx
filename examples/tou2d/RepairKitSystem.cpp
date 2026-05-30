#include "RepairKitSystem.hpp"

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

/// Mirrors `RepairPickupSystem`'s ship-half-extent so kits and bases
/// feel identical on first contact.
constexpr float kShipPickupHalfExtent = 11.0f;

/// Kits use a slightly larger pickup radius than ship visuals so the
/// "you touched it" feel is generous (kits are sparse vs. tiles, so a
/// near-miss should still grab).
constexpr float kKitHalfExtent = 8.0f;

} // namespace

RepairKitSystem::RepairKitSystem(UserComponentIds    ids,
                                 threadmaxx::Engine* engine) noexcept
    : ids_(ids), engine_(engine) {}

void RepairKitSystem::update(threadmaxx::SystemContext& ctx) {
    pickupsThisStep_  = 0;
    respawnsThisStep_ = 0;

    const auto idsPickup = ids_.pickup;
    const auto idsShip   = ids_.ship;
    const auto idsLd     = ids_.loadout;
    if (!idsPickup.valid()) return;
    if (!idsShip.valid())   return;

    ctx.single([&](threadmaxx::Range /*r*/, threadmaxx::CommandBuffer& cb) {
        const auto& view = ctx.worldView();

        // ---- Pass 1: snapshot every live ship's position ---------------
        struct ShipPos {
            float                 x = 0, y = 0;
            threadmaxx::EntityHandle handle{};
            std::uint16_t         currentHp = 0;
            std::uint16_t         maxHp     = 0;
            std::uint16_t         row       = 0;
            const Ship*           shipPtr   = nullptr;
            const WeaponLoadout*  ldPtr     = nullptr;
        };
        std::array<ShipPos, 16> ships{};
        std::size_t shipCount = 0;

        for (const auto* chunkPtr : view.chunks()) {
            if (!chunkPtr) continue;
            const auto& chunk = *chunkPtr;
            if (!chunk.mask.has(idsShip.componentBit()))             continue;
            if (!chunk.mask.has(threadmaxx::Component::Transform))   continue;
            if (chunk.mask.has(threadmaxx::Component::DisabledTag))  continue;

            const auto shipSpan = threadmaxx::user::chunkSpan<Ship>(chunk, idsShip);
            const bool hasLd    = idsLd.valid() && chunk.mask.has(idsLd.componentBit());
            const auto ldSpan   = hasLd
                ? threadmaxx::user::chunkSpan<WeaponLoadout>(chunk, idsLd)
                : std::span<const WeaponLoadout>{};
            const auto entities   = chunk.entities;
            const auto& positions = chunk.transforms;
            const std::size_t n   = entities.size();

            for (std::size_t row = 0; row < n && shipCount < ships.size(); ++row) {
                ShipPos sp;
                sp.x         = positions[row].position.x;
                sp.y         = positions[row].position.y;
                sp.handle    = entities[row];
                sp.shipPtr   = &shipSpan[row];
                sp.ldPtr     = hasLd ? &ldSpan[row] : nullptr;
                ships[shipCount++] = sp;
            }
        }

        // ---- Pass 2: walk every pickup chunk ---------------------------
        for (const auto* chunkPtr : view.chunks()) {
            if (!chunkPtr) continue;
            const auto& chunk = *chunkPtr;
            if (!chunk.mask.has(idsPickup.componentBit()))           continue;
            if (!chunk.mask.has(threadmaxx::Component::Transform))   continue;

            const auto pickupSpan =
                threadmaxx::user::chunkSpan<Pickup>(chunk, idsPickup);
            const auto entities   = chunk.entities;
            const auto& positions = chunk.transforms;
            const std::size_t n   = entities.size();

            for (std::size_t row = 0; row < n; ++row) {
                Pickup pk = pickupSpan[row];

                if (pk.state == 1) {
                    // Respawning. Tick down; on zero, re-enable.
                    if (pk.respawnIn > 0) {
                        --pk.respawnIn;
                        if (pk.respawnIn == 0) {
                            pk.state = 0;
                            cb.removeTag(entities[row],
                                         threadmaxx::Component::DisabledTag);
                            ++respawnsThisStep_;
                        }
                    }
                    threadmaxx::addUserComponent(cb, idsPickup, entities[row], pk);
                    continue;
                }

                // Active. AABB-test against every snapshotted ship.
                const float px = positions[row].position.x;
                const float py = positions[row].position.y;
                std::size_t hit = ships.size();
                for (std::size_t s = 0; s < shipCount; ++s) {
                    const float dx = std::fabs(ships[s].x - px);
                    const float dy = std::fabs(ships[s].y - py);
                    if (dx > kShipPickupHalfExtent + kKitHalfExtent) continue;
                    if (dy > kShipPickupHalfExtent + kKitHalfExtent) continue;
                    hit = s;
                    break;
                }
                if (hit == ships.size()) continue;

                // ---- Apply the effect -----------------------------------
                const PickupKind kind   = static_cast<PickupKind>(pk.kind);
                const PickupSpec& spec  = pickupSpecAt(kind);
                const ShipPos& sp       = ships[hit];

                if (kind == PickupKind::RepairKit) {
                    Ship ship = *sp.shipPtr;
                    ship.currentHp = std::min(
                        ship.maxHp,
                        ship.currentHp + static_cast<float>(spec.effectMagnitude));
                    threadmaxx::addUserComponent(cb, idsShip, sp.handle, ship);

                    if (sp.ldPtr) {
                        WeaponLoadout ld = *sp.ldPtr;
                        ld.specialKind = static_cast<std::uint8_t>(
                            (ld.specialKind + 1u) % kSpecialKindCount);
                        const SpecialWeaponSpec& spec2 =
                            specialSpecAt(ld.specialKind);
                        ld.specialAmmo     = spec2.magazine;
                        ld.specialReloadIn = 0;
                        ld.specialCooldown = 0;
                        threadmaxx::addUserComponent(cb, idsLd, sp.handle, ld);
                    }
                }
                // Future PickupKinds plug in here — switch on `kind` and
                // apply per-spec effect without a virtual interface.

                // ---- Despawn the kit ------------------------------------
                pk.state     = 1;
                pk.respawnIn = spec.respawnIntervalTicks;
                threadmaxx::addUserComponent(cb, idsPickup, entities[row], pk);
                cb.addTag(entities[row], threadmaxx::Component::DisabledTag);

                ++pickupsThisStep_;
                ++pickupsTotal_;

                if (engine_) {
                    engine_->events<AudioPlay>().emit(
                        AudioPlay{audio::kSoundTileBreak, 0, 0});
                }
                if (particles_) {
                    particles_->emitTileBreakDust(px, py);
                }
            }
        }
    });
}

} // namespace tou2d
