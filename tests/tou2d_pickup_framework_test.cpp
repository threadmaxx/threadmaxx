// tou2d_pickup_framework_test — pins the M7.5 pickup-framework split.
//
// Contract:
//
//   (1) `Attribute::RepairBase` sits at enum value 3 (the same byte the
//       pre-M7.5 `Attribute::Repair` used) so existing snapshot bytes
//       and procedurally-generated levels load identically.
//
//   (2) `PickupKind::RepairKit = 0` is the framework's first kind;
//       `PickupKind::Count` is one past, exposed as `kPickupKindCount`.
//
//   (3) `Pickup` is an 8-byte POD with `kind`, `state` (0=active,
//       1=respawning), `respawnIn` (ticks), padded to 8 bytes for clean
//       memcpy into chunk storage. Default-init is "active, no respawn
//       countdown" so a freshly-spawned kit is immediately pickable.
//
//   (4) `PickupSpec` is an 8-byte POD; `pickupSpecAt(PickupKind)` is a
//       constexpr accessor into the catalogue. RepairKit's spec yields
//       a positive respawn interval and a positive effect magnitude so
//       the framework is wired with sensible defaults out of the box.
//
//   (5) Tunable bands: `kRepairBaseHpPerTick` sits in a sensible band
//       relative to `kRepairHealAmount` — the per-tick regen must be
//       strictly smaller than the kit's single-shot heal so a kit still
//       feels like a meaningful pickup vs. just standing on a base.
//
//   (6) Respawn-countdown shape: a simulated state-1 → state-0 cycle
//       reaches state 0 exactly when `respawnIn` hits 0 (off-by-one
//       guard).

#include "Check.hpp"

#include "../examples/tou2d/DemoTypes.hpp"

#include <cstdint>

int main() {
    using tou2d::Attribute;
    using tou2d::Pickup;
    using tou2d::PickupKind;
    using tou2d::PickupSpec;
    using tou2d::kPickupKindCount;
    using tou2d::kRepairBaseHpPerTick;
    using tou2d::kRepairHealAmount;
    using tou2d::pickupSpecAt;

    // ---- (1) Attribute::RepairBase keeps enum byte 3 ---------------------
    static_assert(static_cast<std::uint8_t>(Attribute::RepairBase) == 3,
                  "Attribute::RepairBase must stay at enum value 3 — preserves "
                  "the pre-M7.5 `Repair` byte for snapshot / level-file compat.");
    CHECK(Attribute::RepairBase != Attribute::Air);
    CHECK(Attribute::RepairBase != Attribute::Solid);
    CHECK(Attribute::RepairBase != Attribute::Damage);

    // ---- (2) PickupKind cardinality + first kind -------------------------
    static_assert(static_cast<std::uint8_t>(PickupKind::RepairKit) == 0,
                  "PickupKind::RepairKit must stay at 0 — first kind, "
                  "stored byte on `Pickup::kind`.");
    static_assert(kPickupKindCount >= 1,
                  "PickupKind::Count must be >= 1 (at least RepairKit).");
    CHECK_EQ(kPickupKindCount, std::uint8_t{1});  // grows when new kinds land

    // ---- (3) Pickup POD: defaults + size --------------------------------
    {
        Pickup pk{};
        CHECK_EQ(pk.kind,      std::uint8_t{0});       // RepairKit
        CHECK_EQ(pk.state,     std::uint8_t{0});       // active
        CHECK_EQ(pk.respawnIn, std::uint16_t{0});
        CHECK_EQ(sizeof(Pickup), std::size_t{8});
    }

    // ---- (4) PickupSpec: size + catalogue ---------------------------------
    {
        CHECK_EQ(sizeof(PickupSpec), std::size_t{8});
        const auto& spec = pickupSpecAt(PickupKind::RepairKit);
        CHECK(spec.respawnIntervalTicks > 0);
        CHECK(spec.effectMagnitude      > 0);
        // 360 ticks @ 60Hz = 6s. Loose band — the constant can move
        // without re-pinning every test, but stays in a playable range.
        CHECK(spec.respawnIntervalTicks >= 60);   // >= 1s
        CHECK(spec.respawnIntervalTicks <= 1800); // <= 30s
    }

    // ---- (5) Tunable band: per-tick regen < single-shot heal -------------
    {
        // A kit should always feel like a bigger event than one tick of
        // standing on a base.
        CHECK(kRepairBaseHpPerTick > 0.0f);
        CHECK(kRepairBaseHpPerTick < kRepairHealAmount);
        // It's also small enough that holding still on a base isn't a
        // get-out-of-jail-free against sustained fire.
        CHECK(kRepairBaseHpPerTick <= 4.0f);
    }

    // ---- (6) Respawn countdown — state-1 → state-0 at the right tick ----
    {
        // Mirrors RepairKitSystem's tick-down:
        //   if (state == 1 && respawnIn > 0) {
        //       --respawnIn;
        //       if (respawnIn == 0) state = 0;
        //   }
        Pickup pk{};
        pk.state     = 1;
        pk.respawnIn = 3;
        for (int tick = 0; tick < 3; ++tick) {
            CHECK_EQ(pk.state, std::uint8_t{1});
            if (pk.respawnIn > 0) {
                --pk.respawnIn;
                if (pk.respawnIn == 0) pk.state = 0;
            }
        }
        CHECK_EQ(pk.state,     std::uint8_t{0});  // reactivated
        CHECK_EQ(pk.respawnIn, std::uint16_t{0});
    }

    EXIT_WITH_RESULT();
}
