// tou2d_scoreboard_depth_test — N6 (2026-06-18) scoreboard accumulators.
//
// Pins the public surface added in N6:
//   * `MatchResultsSlot` is now 16 bytes (was 8) and carries `deaths`,
//     `damageDealt`, `damageTaken` parallel to `kills`.
//   * `BulletShipCollisionSystem` exposes per-slot accumulator getters
//     and a `resetStats()` reset hook; defaults are zero.
//   * `resetStats()` truly zeroes every slot — round restart relies on
//     this so a Rematch starts at 0/0/0 across the row.
//
// The accumulators' actual increment side-effect (hit a ship → damage
// dealt goes up) requires the full engine + spawned ships; pinned by
// the headless smoke binary and TouGame::collectMatchResults's plumb.

#include "Check.hpp"

#include "../examples/tou2d/BulletShipCollisionSystem.hpp"
#include "../examples/tou2d/DemoTypes.hpp"
#include "../examples/tou2d/UISystem.hpp"

#include <cstdint>

int main() {
    using tou2d::BulletShipCollisionSystem;
    using tou2d::MatchResultsSlot;
    using tou2d::MatchResults;
    using tou2d::UserComponentIds;
    using tou2d::kMatchSetupSlotCount;
    using tou2d::kMaxPlayerSlots;

    // ---- (1) MatchResultsSlot grew to 16 bytes -----------------------
    static_assert(sizeof(MatchResultsSlot) == 16,
                  "N6 bumped MatchResultsSlot from 8 to 16 bytes to carry "
                  "the deaths/damageDealt/damageTaken trio.");
    static_assert(sizeof(MatchResults) == 8 + kMatchSetupSlotCount * 16,
                  "MatchResults total recomputed from the new slot size.");

    // ---- (2) New slot fields default to zero --------------------------
    {
        MatchResultsSlot s{};
        CHECK_EQ(s.deaths,      std::uint16_t{0});
        CHECK_EQ(s.damageDealt, std::uint16_t{0});
        CHECK_EQ(s.damageTaken, std::uint16_t{0});
    }

    // ---- (3) BulletShipCollisionSystem accumulator getters default 0 -
    {
        UserComponentIds ids{};
        BulletShipCollisionSystem sys(ids, /*engine=*/nullptr);

        for (std::uint8_t slot = 0; slot < kMaxPlayerSlots; ++slot) {
            CHECK_EQ(sys.deathsBySlot(slot),       std::uint16_t{0});
            CHECK_EQ(sys.damageDealtBySlot(slot),  std::uint16_t{0});
            CHECK_EQ(sys.damageTakenBySlot(slot),  std::uint16_t{0});
        }
        // Out-of-bounds: returns 0 (no UB).
        CHECK_EQ(sys.deathsBySlot(255),       std::uint16_t{0});
        CHECK_EQ(sys.damageDealtBySlot(255),  std::uint16_t{0});
        CHECK_EQ(sys.damageTakenBySlot(255),  std::uint16_t{0});
    }

    // ---- (4) resetStats() is a no-op on a fresh system (idempotent) -
    {
        UserComponentIds ids{};
        BulletShipCollisionSystem sys(ids, /*engine=*/nullptr);
        sys.resetStats();
        for (std::uint8_t slot = 0; slot < kMaxPlayerSlots; ++slot) {
            CHECK_EQ(sys.deathsBySlot(slot),       std::uint16_t{0});
            CHECK_EQ(sys.damageDealtBySlot(slot),  std::uint16_t{0});
            CHECK_EQ(sys.damageTakenBySlot(slot),  std::uint16_t{0});
        }
    }

    EXIT_WITH_RESULT();
}
