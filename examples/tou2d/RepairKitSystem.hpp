#pragma once

#include "DemoTypes.hpp"

#include <threadmaxx/Handles.hpp>
#include <threadmaxx/System.hpp>

#include <cstdint>

namespace threadmaxx { class Engine; }

namespace tou2d {

class ParticleSystem;

/// M7.5 — entity-based collectible pickup. Walks entities carrying a
/// `Pickup` user component + `Transform`, splits behaviour by
/// `Pickup::state`:
///
///   state == 0 (active): AABB-test the kit against every non-disabled
///     LocalPlayer ship. On overlap, apply the kind's effect (per
///     `pickupSpecAt(kind)`), flip state→1, attach `DisabledTag`, and
///     set `respawnIn` to the catalogue's `respawnIntervalTicks`. For
///     `PickupKind::RepairKit` the effect mirrors the pre-M7.5
///     single-shot Repair tile: heal `effectMagnitude` HP (clamped at
///     `maxHp`) and advance `WeaponLoadout::specialKind` by one.
///
///   state == 1 (respawning): tick `respawnIn` down each step; when it
///     hits zero, flip state→0, remove `DisabledTag`, and the kit is
///     pickable again next tick.
///
/// Notes:
///   * The system walks BOTH active and respawning chunks (no
///     `DisabledTag` filter on the kit-mask scan) — required so the
///     respawn countdown can advance even while the entity is hidden.
///   * Ships' AABB half-extent matches `RepairPickupSystem`'s
///     `kShipPickupHalfExtent` so a kit triggers the moment the ship
///     visually contacts it.
///
/// reads / writes:
///   * reads  = {Transform, UserData}
///   * writes = {EntityStructural, UserData} — flipping DisabledTag
///              + rewriting `Pickup` + (on apply) `Ship`/`WeaponLoadout`.
class RepairKitSystem : public threadmaxx::ISystem {
public:
    RepairKitSystem(UserComponentIds    ids,
                    threadmaxx::Engine* engine = nullptr) noexcept;

    const char*              name()   const noexcept override { return "tou2d.repairKit"; }
    threadmaxx::ComponentSet reads()  const noexcept override {
        return threadmaxx::ComponentSet{
            threadmaxx::Component::Transform,
            threadmaxx::Component::UserData,
        };
    }
    threadmaxx::ComponentSet writes() const noexcept override {
        return threadmaxx::ComponentSet{
            threadmaxx::Component::EntityStructural,
            threadmaxx::Component::UserData,
        };
    }

    void update(threadmaxx::SystemContext& ctx) override;

    void setParticleSystem(ParticleSystem* p) noexcept { particles_ = p; }

    /// Public per-step counter — pickup-applied events (state-0 → state-1
    /// transitions). Mirrors `RepairPickupSystem::pickupsThisStep` so
    /// host smoke / tests can sum the two for a total pickup count.
    std::uint32_t pickupsThisStep() const noexcept { return pickupsThisStep_; }
    std::uint64_t pickupsTotal()    const noexcept { return pickupsTotal_; }
    /// Respawn-event counter — state-1 → state-0 transitions.
    std::uint32_t respawnsThisStep() const noexcept { return respawnsThisStep_; }

private:
    UserComponentIds      ids_;
    threadmaxx::Engine*   engine_           = nullptr;   // borrowed
    ParticleSystem*       particles_        = nullptr;
    std::uint32_t         pickupsThisStep_  = 0;
    std::uint64_t         pickupsTotal_     = 0;
    std::uint32_t         respawnsThisStep_ = 0;
};

} // namespace tou2d
