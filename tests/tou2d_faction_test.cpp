// tou2d_faction_test — pins the M7.4 faction-system contract.
//
// Contract pinned here:
//
//   (1) `LocalPlayer.factionId` exists, defaults to the `kFactionAuto`
//       sentinel, and the struct still fits in 8 bytes. The sentinel
//       semantics ("use slot as faction") are documented in
//       DemoTypes.hpp; TouGame::spawnShip rewrites it to `slot` at
//       spawn time so default-init reproduces the pre-M7.4 free-for-
//       all (every slot in its own unique faction).
//
//   (2) `PlayerSlotSetup.factionId` exists with the same sentinel.
//       The MatchSetup determinism contract (default = bitwise equal
//       to a freshly default-init MatchSetup) is checked elsewhere
//       in tou2d_player_setup_test.
//
//   (3) `botShotHitsAlly` returns true exactly when a same-faction
//       ally lies inside the forward cone (half-angle
//       `kFriendlyFireArcRad`, range `kFriendlyFireRangeWU`).
//       Returns false for: no allies, only enemies, only the firing
//       ship's own row, out-of-arc same-faction ally, and out-of-
//       range same-faction ally.
//
//   (4) The arc tunables sit in sensible bands relative to
//       BotControlSystem's engagement constants — the arc is wider
//       than the bot's fire-arc decision (so we suppress
//       conservatively) and the range matches the bot's effective
//       fire range.

#include "Check.hpp"

#include "../examples/tou2d/DemoTypes.hpp"
#include "../examples/tou2d/MatchSetup.hpp"
#include "../examples/tou2d/WeaponFireSystem.hpp"

#include <array>
#include <cstdint>

namespace {

constexpr float kFireAngleZeroIsNorth = 0.0f;   // ship facing +Y

} // namespace

int main() {
    using tou2d::AllyPos;
    using tou2d::LocalPlayer;
    using tou2d::PlayerSlotSetup;
    using tou2d::botShotHitsAlly;
    using tou2d::kFactionAuto;
    using tou2d::kFriendlyFireArcRad;
    using tou2d::kFriendlyFireRangeWU;

    // ---- (1) LocalPlayer default + sentinel + size -------------------
    {
        LocalPlayer lp{};
        CHECK_EQ(lp.slot,      std::uint8_t{0});
        CHECK_EQ(lp.isBot,     std::uint8_t{0});
        CHECK_EQ(lp.factionId, kFactionAuto);
        CHECK_EQ(sizeof(LocalPlayer), std::size_t{8});
        CHECK_EQ(kFactionAuto, std::uint8_t{0xFFu});
    }

    // ---- (2) PlayerSlotSetup default + sentinel + size --------------
    {
        PlayerSlotSetup s{};
        CHECK_EQ(s.factionId, kFactionAuto);
        CHECK_EQ(s.role, std::uint8_t{0});
        CHECK_EQ(s.shipKindIdx, kFactionAuto);  // both sentinels are 0xFF
        CHECK_EQ(s.paletteIdx,  kFactionAuto);
        CHECK_EQ(sizeof(PlayerSlotSetup), std::size_t{8});
    }

    // ---- (3) botShotHitsAlly — straight-ahead same-faction ally → true
    {
        // Firer at origin facing +Y (angle 0). An ally dead ahead at
        // (0, 50) is well inside fire range and inside the arc.
        std::array<AllyPos, 1> allies = {{
            { 0.0f, 50.0f, /*factionId*/ 2u, /*selfIdx*/ 99u },
        }};
        CHECK(botShotHitsAlly(allies, 0.0f, 0.0f, kFireAngleZeroIsNorth,
                              /*selfFaction*/ 2u, /*selfIdx*/ 7u));
    }

    // ---- (3b) Same position, different faction → false ----------------
    {
        std::array<AllyPos, 1> allies = {{
            { 0.0f, 50.0f, /*factionId*/ 3u, /*selfIdx*/ 99u },
        }};
        CHECK(!botShotHitsAlly(allies, 0.0f, 0.0f, kFireAngleZeroIsNorth,
                               /*selfFaction*/ 2u, /*selfIdx*/ 7u));
    }

    // ---- (3c) Same faction but out of range → false -----------------
    {
        // Twice the friendly-fire range — way past the engagement bubble.
        const float farY = kFriendlyFireRangeWU * 2.0f;
        std::array<AllyPos, 1> allies = {{
            { 0.0f, farY, /*factionId*/ 1u, /*selfIdx*/ 99u },
        }};
        CHECK(!botShotHitsAlly(allies, 0.0f, 0.0f, kFireAngleZeroIsNorth,
                               /*selfFaction*/ 1u, /*selfIdx*/ 7u));
    }

    // ---- (3d) Same faction, in range, but outside the arc → false ----
    {
        // Ally 90° to the side of the firing direction (firer faces +Y;
        // ally is at +X). Well outside the half-arc.
        std::array<AllyPos, 1> allies = {{
            { 60.0f, 0.0f, /*factionId*/ 1u, /*selfIdx*/ 99u },
        }};
        CHECK(!botShotHitsAlly(allies, 0.0f, 0.0f, kFireAngleZeroIsNorth,
                               /*selfFaction*/ 1u, /*selfIdx*/ 7u));
    }

    // ---- (3e) Self-row is always skipped -----------------------------
    {
        // The only "ally" is the firing ship itself — match-by-selfIdx.
        std::array<AllyPos, 1> allies = {{
            { 0.0f, 30.0f, /*factionId*/ 5u, /*selfIdx*/ 42u },
        }};
        CHECK(!botShotHitsAlly(allies, 0.0f, 0.0f, kFireAngleZeroIsNorth,
                               /*selfFaction*/ 5u, /*selfIdx*/ 42u));
    }

    // ---- (3f) Multi-ally — true if ANY matches ----------------------
    {
        std::array<AllyPos, 3> allies = {{
            { 200.0f, 0.0f,  /*factionId*/ 0u, /*selfIdx*/ 1u },   // wrong arc
            { 0.0f,   400.0f, /*factionId*/ 0u, /*selfIdx*/ 2u },  // wrong range
            { 0.0f,    80.0f, /*factionId*/ 0u, /*selfIdx*/ 3u },  // hit
        }};
        CHECK(botShotHitsAlly(allies, 0.0f, 0.0f, kFireAngleZeroIsNorth,
                              /*selfFaction*/ 0u, /*selfIdx*/ 7u));
    }

    // ---- (3g) Empty ally list → false --------------------------------
    {
        std::array<AllyPos, 0> allies{};
        CHECK(!botShotHitsAlly(allies, 0.0f, 0.0f, kFireAngleZeroIsNorth,
                               /*selfFaction*/ 0u, /*selfIdx*/ 7u));
    }

    // ---- (3h) Symmetry: ally directly behind the firer → false -------
    {
        // Firer at origin facing +Y; ally 50 wu BEHIND on -Y. Outside
        // the forward arc by a full π — must not suppress.
        std::array<AllyPos, 1> allies = {{
            { 0.0f, -50.0f, /*factionId*/ 0u, /*selfIdx*/ 99u },
        }};
        CHECK(!botShotHitsAlly(allies, 0.0f, 0.0f, kFireAngleZeroIsNorth,
                               /*selfFaction*/ 0u, /*selfIdx*/ 7u));
    }

    // ---- (4) Tunable bands -------------------------------------------
    {
        // Arc is wider than the bot's kFacingFire (~0.17 rad / ~10°)
        // so the suppression is conservative; cap below π/4 so it
        // doesn't gate a whole hemisphere worth of bullets.
        CHECK(kFriendlyFireArcRad > 0.17f);
        CHECK(kFriendlyFireArcRad < 0.785f);
        // Range covers the bot's engagement bubble. BotControlSystem's
        // kFireRange is 220 wu; the gate range must be >= that so a
        // bot can never fire on an ally inside its own fire envelope.
        CHECK(kFriendlyFireRangeWU >= 220.0f);
        CHECK(kFriendlyFireRangeWU <= 400.0f);
    }

    EXIT_WITH_RESULT();
}
