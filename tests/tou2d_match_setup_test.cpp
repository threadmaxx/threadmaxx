// tou2d_match_setup_test — M6.2 MatchSetup screen contract.
//
// Pins:
//   * MatchSetup screen row table — 10 scroller rows in MatchSetupKnob
//     order, then Start + Back.
//   * Constructing UISystem with UIScreen::MatchSetup lands focus on
//     row 0 (Humans scroller).
//   * cycleFocused(+1) on each scroller advances the bound knob; the
//     domain wrap holds.
//   * cycleFocused on Action rows is a no-op.
//   * acceptFocused on the Start row sets pendingStartMatch + dismisses
//     the menu (setCurrent(None)). The flag is sticky until cleared.
//   * acceptFocused on the Back row jumps to MainMenu and resets focus.
//   * MainMenu's "Level Setup" row routes to UIScreen::MatchSetup.
//   * formatRow paints "<label>: <value>" for scrollers and just the
//     label for action rows.
//   * MatchSetup-from-menu byte-equals MatchSetup-from-direct-assignment
//     when the menu cycles land on the same values — the determinism
//     contract underwriting M6.2's milestone-level acceptance.

#include "Check.hpp"

#include "../examples/tou2d/DemoTypes.hpp"
#include "../examples/tou2d/MatchSetup.hpp"
#include "../examples/tou2d/UISystem.hpp"

#include <threadmaxx/Engine.hpp>

#include <cstring>

namespace {

bool bytewiseEqual(const tou2d::MatchSetup& a,
                   const tou2d::MatchSetup& b) noexcept {
    return std::memcmp(&a, &b, sizeof(tou2d::MatchSetup)) == 0;
}

} // namespace

int main() {
    using tou2d::MatchSetup;
    using tou2d::MatchSetupKnob;
    using tou2d::MatchMode;
    using tou2d::MenuAction;
    using tou2d::MenuRowKind;
    using tou2d::SpecialKind;
    using tou2d::UIScreen;
    using tou2d::UISystem;

    // ---- MatchSetup row table pinned -------------------------------
    {
        UISystem ui(nullptr, UIScreen::MatchSetup);
        const auto rs = ui.currentRows();
        CHECK_EQ(rs.size(), std::size_t{12});

        // First 10 rows are scrollers in MatchSetupKnob order.
        const MatchSetupKnob expected[] = {
            MatchSetupKnob::Humans,
            MatchSetupKnob::Bots,
            MatchSetupKnob::Mode,
            MatchSetupKnob::Special,
            MatchSetupKnob::UseGen,
            MatchSetupKnob::GenSeed,
            MatchSetupKnob::GenLevel,
            MatchSetupKnob::GenDensity,
            MatchSetupKnob::GenPerim,
            MatchSetupKnob::RepairTiles,
        };
        for (std::size_t i = 0; i < 10; ++i) {
            CHECK(rs[i].kind == MenuRowKind::Scroller);
            CHECK(rs[i].scrollerKnob == expected[i]);
            CHECK(rs[i].enabled);
        }

        // Trailing action rows.
        CHECK(rs[10].kind == MenuRowKind::Action);
        CHECK(rs[10].action == MenuAction::StartMatch);
        CHECK(std::strcmp(rs[10].label, "Start match") == 0);

        CHECK(rs[11].kind == MenuRowKind::Action);
        CHECK(rs[11].action == MenuAction::BackToMain);
        CHECK(std::strcmp(rs[11].label, "Back") == 0);

        // Focus starts on the Humans scroller (first enabled row).
        CHECK_EQ(ui.focusIndex(), std::int32_t{0});
    }

    // ---- Cycle each scroller's knob domain --------------------------
    {
        UISystem ui(nullptr, UIScreen::MatchSetup);
        auto& s = ui.matchSetup();
        const std::uint8_t startHumans = s.numHumans;
        ui.cycleFocused(+1);
        // Humans is bounded [1, kMaxHumans]; +1 from 1 lands on 2.
        CHECK_EQ(s.numHumans, std::uint8_t{2});
        CHECK_EQ(startHumans, std::uint8_t{1});

        // Walk forward N == kMaxHumans steps; lands back on 1 (wrap).
        for (std::uint8_t i = 0; i < tou2d::kMaxHumans; ++i) {
            ui.cycleFocused(+1);
        }
        CHECK_EQ(s.numHumans, std::uint8_t{2});

        // Move to Bots scroller (row 1).
        ui.moveFocus(+1);
        const std::uint8_t botsBefore = s.numBots;
        ui.cycleFocused(+5);
        CHECK_EQ(s.numBots,
                 static_cast<std::uint8_t>(botsBefore + 5));
        ui.cycleFocused(-5);
        CHECK_EQ(s.numBots, botsBefore);

        // Move to Mode scroller (row 2).
        ui.moveFocus(+1);
        CHECK(s.matchMode == MatchMode::Deathmatch);
        ui.cycleFocused(+1);
        CHECK(s.matchMode == MatchMode::LastShipStanding);
        ui.cycleFocused(+1);  // wrap back
        CHECK(s.matchMode == MatchMode::Deathmatch);

        // Move to Special (row 3) — 10 kinds, +1 advances through them.
        ui.moveFocus(+1);
        CHECK(s.specialKind == SpecialKind::Spread);
        ui.cycleFocused(+1);
        CHECK(s.specialKind == SpecialKind::Rapid);
        // Full cycle of 10 lands back on the same kind (wrap).
        ui.cycleFocused(+10);
        CHECK(s.specialKind == SpecialKind::Rapid);
        // Negative wrap: -2 from Rapid (1) lands on Homer (9).
        ui.cycleFocused(-2);
        CHECK(s.specialKind == SpecialKind::Homer);

        // Move to UseGen (row 4).
        ui.moveFocus(+1);
        CHECK(!s.useGen);
        ui.cycleFocused(+1);
        CHECK(s.useGen);

        // GenSeed (row 5) — cycles preset list.
        ui.moveFocus(+1);
        const std::uint32_t seedBefore = s.genCfg.seed;
        ui.cycleFocused(+1);
        CHECK(s.genCfg.seed != seedBefore);
    }

    // ---- cycleFocused on Action rows is no-op ----------------------
    {
        UISystem ui(nullptr, UIScreen::MatchSetup);
        // Jump to Start row (index 10).
        for (int i = 0; i < 10; ++i) ui.moveFocus(+1);
        CHECK_EQ(ui.focusIndex(), std::int32_t{10});
        const MatchSetup snap = ui.matchSetup();
        ui.cycleFocused(+1);
        CHECK(bytewiseEqual(snap, ui.matchSetup()));
        ui.cycleFocused(-1);
        CHECK(bytewiseEqual(snap, ui.matchSetup()));
    }

    // ---- Start accept: pendingStartMatch + dismiss menu ------------
    {
        UISystem ui(nullptr, UIScreen::MatchSetup);
        for (int i = 0; i < 10; ++i) ui.moveFocus(+1);  // Start row
        CHECK(!ui.pendingStartMatch());
        const MenuAction got = ui.acceptFocused();
        CHECK(got == MenuAction::StartMatch);
        CHECK(ui.pendingStartMatch());
        CHECK(ui.current() == UIScreen::None);
        // Flag is sticky until cleared.
        CHECK(ui.pendingStartMatch());
        ui.clearPendingStartMatch();
        CHECK(!ui.pendingStartMatch());
    }

    // ---- Back accept jumps to MainMenu + resets focus --------------
    {
        UISystem ui(nullptr, UIScreen::MatchSetup);
        // Navigate to Back (index 11).
        for (int i = 0; i < 11; ++i) ui.moveFocus(+1);
        CHECK_EQ(ui.focusIndex(), std::int32_t{11});
        const MenuAction got = ui.acceptFocused();
        CHECK(got == MenuAction::BackToMain);
        CHECK(ui.current() == UIScreen::MainMenu);
        // MainMenu lands focus on first enabled (Single Match, index 1).
        CHECK_EQ(ui.focusIndex(), std::int32_t{1});
    }

    // ---- MainMenu "Level Setup" routes to MatchSetup ---------------
    {
        UISystem ui(nullptr, UIScreen::MainMenu);
        // MainMenu rows: 0=Continue(disabled), 1=SingleMatch,
        // 2=LevelSetup, 3=Options, 4=Benchmark, 5=Credits, 6=Quit.
        // Move to LevelSetup (index 2): focus starts at 1, +1 advance.
        ui.moveFocus(+1);
        CHECK_EQ(ui.focusIndex(), std::int32_t{2});
        const MenuAction got = ui.acceptFocused();
        CHECK(got == MenuAction::LevelSetup);
        CHECK(ui.current() == UIScreen::MatchSetup);
        CHECK_EQ(ui.focusIndex(), std::int32_t{0});
    }

    // ---- formatRow ------------------------------------------------
    {
        UISystem ui(nullptr, UIScreen::MatchSetup);
        char buf[160];
        // Row 0 = Humans scroller, default 1.
        ui.formatRow(0, buf, sizeof(buf));
        CHECK(std::strcmp(buf, "Humans: 1") == 0);
        // Row 2 = Mode scroller, default Deathmatch.
        ui.formatRow(2, buf, sizeof(buf));
        CHECK(std::strcmp(buf, "Mode: Deathmatch") == 0);
        // Row 10 = Start (Action).
        ui.formatRow(10, buf, sizeof(buf));
        CHECK(std::strcmp(buf, "Start match") == 0);
        // Row 11 = Back (Action).
        ui.formatRow(11, buf, sizeof(buf));
        CHECK(std::strcmp(buf, "Back") == 0);
        // Out-of-range row index yields empty string.
        ui.formatRow(99, buf, sizeof(buf));
        CHECK_EQ(std::strlen(buf), std::size_t{0});
    }

    // ---- Determinism contract: menu path == direct assignment ------
    // Build a `target` MatchSetup by direct field writes, then drive
    // the menu via cycleFocused to land on the SAME values. The two
    // PODs must be byte-equal — that's the contract main.cpp's CLI
    // → MatchSetup → setMatchSetup pipeline relies on.
    {
        // Target: 3 humans, 8 bots, LSS, Sniper, useGen=true, seed=42,
        // ggLevel=2 (default), density=70, perim=on (default), repair=12.
        MatchSetup target{};
        target.numHumans   = 3;
        target.numBots     = 8;
        target.matchMode   = MatchMode::LastShipStanding;
        target.specialKind = SpecialKind::Sniper;
        target.useGen      = true;
        target.genCfg.seed             = 42u;
        target.genCfg.ggLevel          = 2;
        target.genCfg.stuffDensity     = 70;
        target.genCfg.perimeterBedrock = 1;
        target.genCfg.repairTileCount  = 12;

        // Drive the menu from defaults.
        UISystem ui(nullptr, UIScreen::MatchSetup);
        // Humans 1 → 3: cycle +2.
        ui.cycleFocused(+2);
        // Bots: row 1, default 3 → 8: cycle +5.
        ui.moveFocus(+1);
        ui.cycleFocused(+5);
        // Mode: row 2, Deathmatch → LSS: cycle +1.
        ui.moveFocus(+1);
        ui.cycleFocused(+1);
        // Special: row 3, Spread (0) → Sniper (2): cycle +2.
        ui.moveFocus(+1);
        ui.cycleFocused(+2);
        // UseGen: row 4, Off → On: cycle +1.
        ui.moveFocus(+1);
        ui.cycleFocused(+1);
        // GenSeed: row 5; default is 0 (index 0 in preset list); 42 is
        // index 3. Cycle +3.
        ui.moveFocus(+1);
        ui.cycleFocused(+3);
        // GenLevel: row 6, default 2 (matches target) — no-op.
        ui.moveFocus(+1);
        // GenDensity: row 7, default 50 → 70: +2 buckets of 10.
        ui.moveFocus(+1);
        ui.cycleFocused(+2);
        // GenPerim: row 8, default On (matches target) — no-op.
        ui.moveFocus(+1);
        // RepairTiles: row 9, default 0 → 12 = +3 buckets of 4.
        ui.moveFocus(+1);
        ui.cycleFocused(+3);

        const MatchSetup& got = ui.matchSetup();
        CHECK_EQ(got.numHumans,                 target.numHumans);
        CHECK_EQ(got.numBots,                   target.numBots);
        CHECK(got.matchMode                  == target.matchMode);
        CHECK(got.specialKind                == target.specialKind);
        CHECK(got.useGen                     == target.useGen);
        CHECK_EQ(got.genCfg.seed,               target.genCfg.seed);
        CHECK_EQ(got.genCfg.ggLevel,            target.genCfg.ggLevel);
        CHECK_EQ(got.genCfg.stuffDensity,       target.genCfg.stuffDensity);
        CHECK_EQ(got.genCfg.perimeterBedrock,   target.genCfg.perimeterBedrock);
        CHECK_EQ(got.genCfg.repairTileCount,    target.genCfg.repairTileCount);
        CHECK(bytewiseEqual(target, got));
    }

    EXIT_WITH_RESULT();
}
