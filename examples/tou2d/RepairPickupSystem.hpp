#pragma once

#include "DemoTypes.hpp"

#include <threadmaxx/Handles.hpp>
#include <threadmaxx/System.hpp>

#include <cstdint>
#include <functional>

namespace threadmaxx { class Engine; }

namespace tou2d {

class ParticleSystem;

/// M5.7 — ship-vs-repair-tile overlap. Runs in its own wave AFTER
/// TerrainCollisionSystem (so the ship's pushed-out position is settled
/// before we test the overlap) and BEFORE WeaponFireSystem (so a
/// freshly-cycled weapon fires the same tick the pickup happens).
///
/// For each LocalPlayer ship: sample a 1-cell neighborhood around the
/// ship's grid cell, AABB-test each `Attribute::Repair` tile against
/// the ship's collision rect; on overlap consume the tile by:
///   1. healing `Ship::currentHp` by `kRepairHealAmount` (clamped to
///      `maxHp`),
///   2. advancing `WeaponLoadout::specialKind` to
///      `(kind + 1) % kSpecialKindCount`, with `specialAmmo` refilled
///      to the new spec's `magazine` and both reload/cooldown counters
///      zeroed,
///   3. clearing the cell (`grid->clear` → Air),
///   4. firing the destroy callback (so the JPG painter clears the
///      repair-tile marker),
///   5. emitting an `AudioPlay` event (reuses `kSoundTileBreak` until
///      a dedicated repair sound lands).
///
/// reads / writes:
///   * reads  = {Transform, UserData}
///   * writes = {EntityStructural} — needed for the user-component
///              writes that mutate `Ship` + `WeaponLoadout`.
class RepairPickupSystem : public threadmaxx::ISystem {
public:
    RepairPickupSystem(UserComponentIds    ids,
                       TerrainGrid*        grid,
                       threadmaxx::Engine* engine = nullptr) noexcept;

    const char*              name()   const noexcept override { return "tou2d.repairPickup"; }
    threadmaxx::ComponentSet reads()  const noexcept override {
        return threadmaxx::ComponentSet{
            threadmaxx::Component::Transform,
            threadmaxx::Component::UserData,
        };
    }
    threadmaxx::ComponentSet writes() const noexcept override {
        return threadmaxx::ComponentSet{
            threadmaxx::Component::EntityStructural,
        };
    }

    void update(threadmaxx::SystemContext& ctx) override;

    /// Mirrors the bullet-break callback signature so the host painter
    /// can route both into the same dirty-rect.
    using DestroyCallback = std::function<void(std::int32_t cellX, std::int32_t cellY)>;
    void setDestroyCallback(DestroyCallback cb) noexcept { destroyCb_ = std::move(cb); }

    void setParticleSystem(ParticleSystem* p) noexcept { particles_ = p; }

    /// Public per-step counter — `tests/tou2d_repair_pickup_test.cpp`
    /// observes it to confirm the system fired.
    std::uint32_t pickupsThisStep() const noexcept { return pickupsThisStep_; }
    /// Cumulative tally across the run. Host smoke can read this for
    /// a one-line "ships picked up N repairs" report.
    std::uint64_t pickupsTotal() const noexcept { return pickupsTotal_; }

private:
    UserComponentIds      ids_;
    TerrainGrid*          grid_   = nullptr;   // borrowed; owned by TouGame
    threadmaxx::Engine*   engine_ = nullptr;   // borrowed; AudioPlay only
    DestroyCallback       destroyCb_;
    ParticleSystem*       particles_ = nullptr;
    std::uint32_t         pickupsThisStep_ = 0;
    std::uint64_t         pickupsTotal_    = 0;
};

} // namespace tou2d
