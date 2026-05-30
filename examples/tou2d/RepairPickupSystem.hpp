#pragma once

#include "DemoTypes.hpp"

#include <threadmaxx/Handles.hpp>
#include <threadmaxx/System.hpp>

#include <cstdint>
#include <functional>
#include <unordered_set>

namespace threadmaxx { class Engine; }

namespace tou2d {

class ParticleSystem;

/// M5.7 / M7.5 — ship-vs-RepairBase-tile overlap. Runs in its own wave
/// AFTER TerrainCollisionSystem (so the ship's pushed-out position is
/// settled before we test the overlap) and BEFORE WeaponFireSystem (so
/// a freshly-cycled weapon fires the same tick the entry-edge cycle
/// happens).
///
/// M7.5 reworked the on-touch contract from "consume + heal + cycle"
/// to non-consuming "regen + edge-triggered cycle":
///
///   1. Per-tick HP regen while a ship's AABB overlaps a
///      `Attribute::RepairBase` cell. `Ship::currentHp` advances by
///      `kRepairBaseHpPerTick`, clamped to `Ship::maxHp`.
///   2. Edge-triggered special-cycle on ENTRY — the first tick a ship
///      newly stands on a base, `WeaponLoadout::specialKind` advances
///      `(kind + 1) % kSpecialKindCount`, with `specialAmmo` refilled
///      to the new spec's `magazine` and both reload/cooldown counters
///      zeroed. Cycling does not re-trigger while the ship stays put.
///   3. Entry-edge also fires the AudioPlay cue + particle burst
///      (reuses `kSoundTileBreak` for now) — the regen lane is silent.
///   4. The cell stays in place — no `grid->clear` / no destroyCb.
///
/// Per-ship entry-edge state lives in `onBasePrev_` (entity index set).
/// `pickupsThisStep_` / `pickupsTotal_` count entry-edge events only,
/// preserving the M5.7 test interpretation ("how many fresh pickup
/// events fired this step").
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
    UserComponentIds                ids_;
    TerrainGrid*                    grid_   = nullptr;   // borrowed; owned by TouGame
    threadmaxx::Engine*             engine_ = nullptr;   // borrowed; AudioPlay only
    DestroyCallback                 destroyCb_;
    ParticleSystem*                 particles_      = nullptr;
    std::uint32_t                   pickupsThisStep_= 0;
    std::uint64_t                   pickupsTotal_   = 0;
    std::unordered_set<std::uint32_t> onBasePrev_;   ///< entity indices on a base last tick
};

} // namespace tou2d
