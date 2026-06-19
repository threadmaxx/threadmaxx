// tou2d_results_screen_test — M6.6 Results screen contract.
//
// Pins:
//   * `showResults(MatchResults)` captures the snapshot AND transitions
//     the active screen to UIScreen::Results (idempotent — successive
//     calls overwrite the snapshot and stay on Results).
//   * Results row table layout: row 0 = winner banner (Display),
//     row 1 = column header (Display), rows 2..5 = per-slot scoreboard
//     (Display, slotIdx 0..3), rows 6..8 = Rematch / ReturnToSetup /
//     ReturnToMainMenu (Action). 9 rows total.
//   * Display rows have `enabled == false` so moveFocus skips them —
//     initial focus on UIScreen::Results lands on row 6 (Rematch).
//   * formatRow renders Display rows from `matchResults()`: winner
//     banner contains "WINNER" + slot index + tag + kills; per-slot
//     lines contain the tag, kills, ship name; empty slots render
//     "(empty)".
//   * acceptFocused on Rematch sets pendingRematch_ AND dismisses the
//     menu (setCurrent(None)).
//   * acceptFocused on ReturnToSetup transitions to UIScreen::MatchSetup
//     (does NOT set any sticky flag).
//   * acceptFocused on ReturnToMainMenu sets pendingReturnToMainMenu_
//     AND transitions to UIScreen::MainMenu (existing M6.4 behaviour).
//   * matchResults() snapshot persists across screen transitions —
//     navigating away from Results and back does NOT overwrite it;
//     only a fresh showResults call does.
//   * MatchResults POD: sizeof == 8 + kMatchSetupSlotCount * 8 (40 B
//     today). The Results-row formatter relies on this layout.

#include "Check.hpp"

#include "../examples/tou2d/DemoTypes.hpp"
#include "../examples/tou2d/MatchSetup.hpp"
#include "../examples/tou2d/UISystem.hpp"

#include <threadmaxx/Engine.hpp>

#include <cstring>
#include <string>

int main() {
    using tou2d::MatchResults;
    using tou2d::MatchResultsSlot;
    using tou2d::MenuAction;
    using tou2d::MenuRowKind;
    using tou2d::UIScreen;
    using tou2d::UISystem;
    using tou2d::kMatchSetupSlotCount;

    // ---- 1. MatchResults POD layout pin ---------------------------------
    // N6 (2026-06-18) — MatchResultsSlot bumped from 8 to 16 bytes to
    // carry the new deaths / damageDealt / damageTaken fields.
    CHECK_EQ(sizeof(MatchResultsSlot), std::size_t{16});
    CHECK_EQ(sizeof(MatchResults),
             std::size_t{8 + kMatchSetupSlotCount * 16});

    threadmaxx::Engine engine;

    // ---- 2. showResults transitions to Results --------------------------
    {
        UISystem ui(&engine, UIScreen::MainMenu);
        CHECK_EQ(ui.current(), UIScreen::MainMenu);

        MatchResults r{};
        r.winnerSlot  = 2;
        r.winnerKills = 7;
        r.slots[0] = MatchResultsSlot{
            {'P','0',' '}, /*active*/1, /*kills*/3,
            /*isBot*/0, /*shipKindIdx*/0,
        };
        r.slots[1] = MatchResultsSlot{
            {'B','O','T'}, /*active*/1, /*kills*/5,
            /*isBot*/1, /*shipKindIdx*/0,
        };
        r.slots[2] = MatchResultsSlot{
            {'A','C','E'}, /*active*/1, /*kills*/7,
            /*isBot*/0, /*shipKindIdx*/0,
        };
        r.slots[3] = MatchResultsSlot{
            {' ',' ',' '}, /*active*/0, /*kills*/0,
            /*isBot*/1, /*shipKindIdx*/0,
        };
        ui.showResults(r);
        CHECK_EQ(ui.current(), UIScreen::Results);
        CHECK_EQ(ui.matchResults().winnerSlot,  std::uint8_t{2});
        CHECK_EQ(ui.matchResults().winnerKills, std::uint16_t{7});
        CHECK_EQ(ui.matchResults().slots[1].kills, std::uint16_t{5});
        CHECK_EQ(ui.matchResults().slots[3].active, std::uint8_t{0});

        // ---- 3. Row layout (9 rows: 2 display headers + 4 slots + 3 acts)
        const auto rs = ui.currentRows();
        CHECK_EQ(rs.size(), std::size_t{2 + kMatchSetupSlotCount + 3});

        // Display rows 0..5 — slotIdx encodes meaning (0xFF banner, 0xFE
        // header, 0..3 slot index). enabled == false so moveFocus skips.
        CHECK_EQ(static_cast<int>(rs[0].kind), int(MenuRowKind::Display));
        CHECK_EQ(rs[0].slotIdx, std::uint8_t{0xFFu});
        CHECK(!rs[0].enabled);
        CHECK_EQ(static_cast<int>(rs[1].kind), int(MenuRowKind::Display));
        CHECK_EQ(rs[1].slotIdx, std::uint8_t{0xFEu});
        CHECK(!rs[1].enabled);
        for (std::size_t i = 0; i < kMatchSetupSlotCount; ++i) {
            const auto& row = rs[2 + i];
            CHECK_EQ(static_cast<int>(row.kind), int(MenuRowKind::Display));
            CHECK_EQ(row.slotIdx, static_cast<std::uint8_t>(i));
            CHECK(!row.enabled);
        }
        // Action rows — Rematch / ReturnToSetup / ReturnToMainMenu.
        const std::size_t actBase = 2 + kMatchSetupSlotCount;
        CHECK_EQ(static_cast<int>(rs[actBase + 0].action),
                 int(MenuAction::Rematch));
        CHECK(rs[actBase + 0].enabled);
        CHECK_EQ(static_cast<int>(rs[actBase + 1].action),
                 int(MenuAction::ReturnToSetup));
        CHECK(rs[actBase + 1].enabled);
        CHECK_EQ(static_cast<int>(rs[actBase + 2].action),
                 int(MenuAction::ReturnToMainMenu));
        CHECK(rs[actBase + 2].enabled);

        // ---- 4. Initial focus lands on the first enabled row (Rematch). -
        CHECK_EQ(ui.focusIndex(), static_cast<std::int32_t>(actBase));

        // moveFocus(-1) wraps to the last enabled row (ReturnToMainMenu).
        ui.moveFocus(-1);
        CHECK_EQ(ui.focusIndex(),
                 static_cast<std::int32_t>(actBase + 2));
        // moveFocus(+1) wraps back to Rematch (first enabled).
        ui.moveFocus(+1);
        CHECK_EQ(ui.focusIndex(), static_cast<std::int32_t>(actBase));

        // ---- 5. formatRow renders Display rows from matchResults_. ------
        char buf[64];
        ui.formatRow(0, buf, sizeof buf);
        const std::string banner{buf};
        CHECK(banner.find("WINNER") != std::string::npos);
        CHECK(banner.find("P2")     != std::string::npos);
        CHECK(banner.find("ACE")    != std::string::npos);
        CHECK(banner.find("7")      != std::string::npos);

        ui.formatRow(1, buf, sizeof buf);
        // Column header — just a static labels line.
        CHECK(std::strstr(buf, "Slot") != nullptr);
        CHECK(std::strstr(buf, "Kills") != nullptr);

        // Per-slot Display rows include tag + kills.
        ui.formatRow(2, buf, sizeof buf);  // slot 0
        CHECK(std::strstr(buf, "P0") != nullptr);
        CHECK(std::strstr(buf, "3")  != nullptr);

        ui.formatRow(3, buf, sizeof buf);  // slot 1 (BOT)
        const std::string slot1{buf};
        CHECK(slot1.find("BOT") != std::string::npos);
        CHECK(slot1.find("[bot]") != std::string::npos);

        ui.formatRow(5, buf, sizeof buf);  // slot 3 — inactive
        CHECK(std::strstr(buf, "empty") != nullptr);

        // Action rows render the static label verbatim.
        ui.formatRow(static_cast<std::int32_t>(actBase), buf, sizeof buf);
        CHECK_EQ(std::string{buf}, std::string{"Rematch"});
    }

    // ---- 6. acceptFocused — Rematch sets pendingRematch_ + dismisses ---
    {
        UISystem ui(&engine, UIScreen::None);
        ui.showResults(MatchResults{});
        CHECK_EQ(ui.current(), UIScreen::Results);
        const std::int32_t rematchIdx = static_cast<std::int32_t>(
            2 + kMatchSetupSlotCount + 0);
        CHECK_EQ(ui.focusIndex(), rematchIdx);
        CHECK(!ui.pendingRematch());
        const auto act = ui.acceptFocused();
        CHECK_EQ(static_cast<int>(act), int(MenuAction::Rematch));
        CHECK(ui.pendingRematch());
        CHECK_EQ(ui.current(), UIScreen::None);
        ui.clearPendingRematch();
        CHECK(!ui.pendingRematch());
    }

    // ---- 7. acceptFocused — ReturnToSetup goes to MatchSetup (no flag) -
    {
        UISystem ui(&engine, UIScreen::None);
        ui.showResults(MatchResults{});
        // Move focus to ReturnToSetup row (actBase + 1).
        ui.moveFocus(+1);
        const std::int32_t expect = static_cast<std::int32_t>(
            2 + kMatchSetupSlotCount + 1);
        CHECK_EQ(ui.focusIndex(), expect);
        const auto act = ui.acceptFocused();
        CHECK_EQ(static_cast<int>(act), int(MenuAction::ReturnToSetup));
        CHECK_EQ(ui.current(), UIScreen::MatchSetup);
        CHECK(!ui.pendingRematch());
        CHECK(!ui.pendingStartMatch());
    }

    // ---- 8. acceptFocused — ReturnToMainMenu existing M6.4 path --------
    {
        UISystem ui(&engine, UIScreen::None);
        ui.showResults(MatchResults{});
        ui.moveFocus(+2);  // Rematch -> ReturnToSetup -> ReturnToMainMenu
        const std::int32_t expect = static_cast<std::int32_t>(
            2 + kMatchSetupSlotCount + 2);
        CHECK_EQ(ui.focusIndex(), expect);
        const auto act = ui.acceptFocused();
        CHECK_EQ(static_cast<int>(act), int(MenuAction::ReturnToMainMenu));
        CHECK_EQ(ui.current(), UIScreen::MainMenu);
        CHECK(ui.pendingReturnToMainMenu());
        ui.clearPendingReturnToMainMenu();
    }

    // ---- 9. matchResults() persists across screen transitions ----------
    {
        UISystem ui(&engine, UIScreen::None);
        MatchResults r{};
        r.winnerSlot  = 1;
        r.winnerKills = 4;
        r.slots[0].active = 1;
        r.slots[0].kills  = 4;
        ui.showResults(r);
        CHECK_EQ(ui.matchResults().winnerSlot, std::uint8_t{1});

        ui.setCurrent(UIScreen::MainMenu);
        CHECK_EQ(ui.matchResults().winnerSlot, std::uint8_t{1});
        ui.setCurrent(UIScreen::Results);
        CHECK_EQ(ui.matchResults().winnerSlot, std::uint8_t{1});

        // A fresh showResults overwrites.
        MatchResults r2{};
        r2.winnerSlot = 3;
        ui.showResults(r2);
        CHECK_EQ(ui.matchResults().winnerSlot, std::uint8_t{3});
    }

    EXIT_WITH_RESULT();
}
