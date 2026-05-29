// tou2d_player_setup_test — M6.3 PlayerSetup screen contract.
//
// Pins:
//   * MatchSetup row 10 = "Players..." Action row whose accept routes
//     to UIScreen::PlayerSetup.
//   * PlayerSetup row table — 4 slots × (TagChar0/TagChar1/TagChar2 +
//     Role + Ship + Palette), then Back. 25 rows total in slot-major
//     order; per-slot rows carry slotIdx in [0, 4).
//   * Constructing UISystem with UIScreen::PlayerSetup lands focus on
//     row 0 (slot 0 / TagChar0).
//   * cycleFocused(+1) on a tag-char row advances the alphabet (space →
//     A → B → ... → Z → space). 27-position wrap.
//   * cycleFocused(+1) on a role row walks Auto → Human → Bot → Auto.
//   * cycleFocused(+1) on a ship row walks Auto → kShipKinds[0] → ... →
//     kShipKinds[N-1] → Auto. (N+1)-position wrap.
//   * cycleFocused(+1) on a palette row walks Auto → Yellow → Blue →
//     Red → Green → Auto. 5-position wrap.
//   * Per-slot scroller writes target the row's slotIdx — cycling slot
//     1's ship leaves slot 0's ship untouched.
//   * formatRow renders the slot prefix + value (e.g. "Slot 1 ship:
//     Auto"); the tag-char blank renders as '_' so the focus is
//     unambiguous.
//   * BackToMain accept on PlayerSetup routes to UIScreen::MatchSetup
//     (not MainMenu) so the user returns to where they came from. Focus
//     lands on MatchSetup row 0.
//   * Default MatchSetup has all-sentinel playerSlots — bytewise equal
//     between a freshly constructed UISystem's MatchSetup and a
//     direct-init MatchSetup. This is the M6.3 determinism contract:
//     unedited menu run = CLI run.

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
    using tou2d::MenuAction;
    using tou2d::MenuRowKind;
    using tou2d::PlayerSlotSetup;
    using tou2d::UIScreen;
    using tou2d::UISystem;
    using tou2d::kMatchSetupSlotCount;
    using tou2d::kShipKindCount;

    // ---- MatchSetup row 10 ("Players...") routes to PlayerSetup -----
    {
        UISystem ui(nullptr, UIScreen::MatchSetup);
        for (int i = 0; i < 10; ++i) ui.moveFocus(+1);
        CHECK_EQ(ui.focusIndex(), std::int32_t{10});
        const MenuAction got = ui.acceptFocused();
        CHECK(got == MenuAction::PlayerSetup);
        CHECK(ui.current() == UIScreen::PlayerSetup);
        // Lands on the first row of the PlayerSetup screen.
        CHECK_EQ(ui.focusIndex(), std::int32_t{0});
    }

    // ---- PlayerSetup row table pinned -------------------------------
    {
        UISystem ui(nullptr, UIScreen::PlayerSetup);
        const auto rs = ui.currentRows();
        CHECK_EQ(rs.size(), kMatchSetupSlotCount * 6 + 1);
        CHECK_EQ(rs.size(), std::size_t{25});

        // Slot-major order: for each slot, rows in
        // TagChar0/TagChar1/TagChar2/Role/Ship/Palette order.
        const MatchSetupKnob kPerSlotOrder[] = {
            MatchSetupKnob::SlotTagChar0,
            MatchSetupKnob::SlotTagChar1,
            MatchSetupKnob::SlotTagChar2,
            MatchSetupKnob::SlotRole,
            MatchSetupKnob::SlotShip,
            MatchSetupKnob::SlotPalette,
        };
        for (std::size_t slot = 0; slot < kMatchSetupSlotCount; ++slot) {
            for (std::size_t f = 0; f < 6; ++f) {
                const std::size_t idx = slot * 6 + f;
                CHECK(rs[idx].kind == MenuRowKind::Scroller);
                CHECK(rs[idx].scrollerKnob == kPerSlotOrder[f]);
                CHECK_EQ(rs[idx].slotIdx, static_cast<std::uint8_t>(slot));
                CHECK(rs[idx].enabled);
            }
        }

        // Trailing Back action row.
        const std::size_t backIdx = kMatchSetupSlotCount * 6;
        CHECK(rs[backIdx].kind == MenuRowKind::Action);
        CHECK(rs[backIdx].action == MenuAction::BackToMain);
        CHECK(std::strcmp(rs[backIdx].label, "Back") == 0);

        CHECK_EQ(ui.focusIndex(), std::int32_t{0});
    }

    // ---- Tag-char cycle: 27-position alphabet ----------------------
    {
        UISystem ui(nullptr, UIScreen::PlayerSetup);
        // Slot 0 TagChar0 is row 0.
        CHECK_EQ(ui.matchSetup().playerSlots[0].tag[0], ' ');
        ui.cycleFocused(+1);
        CHECK_EQ(ui.matchSetup().playerSlots[0].tag[0], 'A');
        ui.cycleFocused(+1);
        CHECK_EQ(ui.matchSetup().playerSlots[0].tag[0], 'B');
        // Full wrap: 27 advances lands back on B.
        ui.cycleFocused(+27);
        CHECK_EQ(ui.matchSetup().playerSlots[0].tag[0], 'B');
        // -2 from B lands on space (Z, A is +2 from space; B is +2;
        // -2 from B = space).
        ui.cycleFocused(-2);
        CHECK_EQ(ui.matchSetup().playerSlots[0].tag[0], ' ');
        // -1 from space wraps to Z.
        ui.cycleFocused(-1);
        CHECK_EQ(ui.matchSetup().playerSlots[0].tag[0], 'Z');
    }

    // ---- Role tristate: Auto → Human → Bot → Auto ------------------
    {
        UISystem ui(nullptr, UIScreen::PlayerSetup);
        // Slot 0 Role is row 3.
        for (int i = 0; i < 3; ++i) ui.moveFocus(+1);
        CHECK_EQ(ui.focusIndex(), std::int32_t{3});
        CHECK_EQ(ui.matchSetup().playerSlots[0].role, std::uint8_t{0});
        ui.cycleFocused(+1);
        CHECK_EQ(ui.matchSetup().playerSlots[0].role, std::uint8_t{1});
        ui.cycleFocused(+1);
        CHECK_EQ(ui.matchSetup().playerSlots[0].role, std::uint8_t{2});
        ui.cycleFocused(+1);  // wrap
        CHECK_EQ(ui.matchSetup().playerSlots[0].role, std::uint8_t{0});
        ui.cycleFocused(-1);  // wrap back
        CHECK_EQ(ui.matchSetup().playerSlots[0].role, std::uint8_t{2});
    }

    // ---- Ship-kind cycle: Auto + N ship kinds ----------------------
    {
        UISystem ui(nullptr, UIScreen::PlayerSetup);
        // Slot 0 Ship is row 4.
        for (int i = 0; i < 4; ++i) ui.moveFocus(+1);
        CHECK_EQ(ui.focusIndex(), std::int32_t{4});
        // Default is 0xFF (Auto).
        CHECK_EQ(ui.matchSetup().playerSlots[0].shipKindIdx, std::uint8_t{0xFFu});
        ui.cycleFocused(+1);
        CHECK_EQ(ui.matchSetup().playerSlots[0].shipKindIdx, std::uint8_t{0});
        ui.cycleFocused(+1);
        CHECK_EQ(ui.matchSetup().playerSlots[0].shipKindIdx, std::uint8_t{1});
        // Cycle to the last kind via +(N-2).
        ui.cycleFocused(static_cast<std::int32_t>(kShipKindCount) - 2);
        CHECK_EQ(ui.matchSetup().playerSlots[0].shipKindIdx,
                 static_cast<std::uint8_t>(kShipKindCount - 1));
        // +1 wraps to Auto.
        ui.cycleFocused(+1);
        CHECK_EQ(ui.matchSetup().playerSlots[0].shipKindIdx, std::uint8_t{0xFFu});
    }

    // ---- Palette cycle: Auto + 4 colors ----------------------------
    {
        UISystem ui(nullptr, UIScreen::PlayerSetup);
        // Slot 0 Palette is row 5.
        for (int i = 0; i < 5; ++i) ui.moveFocus(+1);
        CHECK_EQ(ui.focusIndex(), std::int32_t{5});
        CHECK_EQ(ui.matchSetup().playerSlots[0].paletteIdx, std::uint8_t{0xFFu});
        ui.cycleFocused(+1);  // Auto → Yellow
        CHECK_EQ(ui.matchSetup().playerSlots[0].paletteIdx, std::uint8_t{0});
        ui.cycleFocused(+5);  // full wrap (5 positions) — back to Yellow
        CHECK_EQ(ui.matchSetup().playerSlots[0].paletteIdx, std::uint8_t{0});
        ui.cycleFocused(-1);  // Yellow → Auto
        CHECK_EQ(ui.matchSetup().playerSlots[0].paletteIdx, std::uint8_t{0xFFu});
        ui.cycleFocused(-1);  // Auto → Green (wrap)
        CHECK_EQ(ui.matchSetup().playerSlots[0].paletteIdx, std::uint8_t{3});
    }

    // ---- Per-slot writes target the row's slotIdx ------------------
    {
        UISystem ui(nullptr, UIScreen::PlayerSetup);
        // Move to slot 1's Ship row (slot 1 starts at row 6; Ship is +4).
        for (int i = 0; i < 6 + 4; ++i) ui.moveFocus(+1);
        CHECK_EQ(ui.focusIndex(), std::int32_t{10});
        const auto& rs = ui.currentRows();
        CHECK_EQ(rs[10].slotIdx, std::uint8_t{1});
        CHECK(rs[10].scrollerKnob == MatchSetupKnob::SlotShip);
        ui.cycleFocused(+1);
        // Slot 1's ship changed; slot 0's didn't.
        CHECK_EQ(ui.matchSetup().playerSlots[1].shipKindIdx, std::uint8_t{0});
        CHECK_EQ(ui.matchSetup().playerSlots[0].shipKindIdx, std::uint8_t{0xFFu});
        // Slot 2 and 3 also untouched.
        CHECK_EQ(ui.matchSetup().playerSlots[2].shipKindIdx, std::uint8_t{0xFFu});
        CHECK_EQ(ui.matchSetup().playerSlots[3].shipKindIdx, std::uint8_t{0xFFu});
    }

    // ---- formatRow paints slot prefix + value ----------------------
    {
        UISystem ui(nullptr, UIScreen::PlayerSetup);
        char buf[80];
        // Row 0 = slot 1 tag c1, default blank → underscore.
        ui.formatRow(0, buf, sizeof(buf));
        CHECK(std::strcmp(buf, "Slot 1 tag c1: _") == 0);
        // Row 3 = slot 1 role, default Auto.
        ui.formatRow(3, buf, sizeof(buf));
        CHECK(std::strcmp(buf, "Slot 1 role: Auto") == 0);
        // Row 4 = slot 1 ship, default Auto.
        ui.formatRow(4, buf, sizeof(buf));
        CHECK(std::strcmp(buf, "Slot 1 ship: Auto") == 0);
        // Row 5 = slot 1 palette, default Auto.
        ui.formatRow(5, buf, sizeof(buf));
        CHECK(std::strcmp(buf, "Slot 1 palette: Auto") == 0);
        // Set slot 0 tag char 0 to 'X' and re-render.
        ui.cycleFocused(+24);  // space → X
        ui.formatRow(0, buf, sizeof(buf));
        CHECK(std::strcmp(buf, "Slot 1 tag c1: X") == 0);
        // Row 12 = slot 3 tag c1 (slot 2 is 6..11, slot 3 starts at 12).
        ui.formatRow(12, buf, sizeof(buf));
        CHECK(std::strcmp(buf, "Slot 3 tag c1: _") == 0);
        // Row 24 = Back.
        ui.formatRow(24, buf, sizeof(buf));
        CHECK(std::strcmp(buf, "Back") == 0);
    }

    // ---- BackToMain from PlayerSetup routes to MatchSetup ----------
    {
        UISystem ui(nullptr, UIScreen::PlayerSetup);
        // Navigate to Back (row 24).
        for (int i = 0; i < 24; ++i) ui.moveFocus(+1);
        CHECK_EQ(ui.focusIndex(), std::int32_t{24});
        const MenuAction got = ui.acceptFocused();
        CHECK(got == MenuAction::BackToMain);
        // M6.3 — PlayerSetup Back goes to MatchSetup (not MainMenu).
        CHECK(ui.current() == UIScreen::MatchSetup);
        CHECK_EQ(ui.focusIndex(), std::int32_t{0});
    }

    // ---- Determinism contract: default MatchSetup bytewise equal ---
    // A direct-init MatchSetup must be byte-identical to the UISystem's
    // working MatchSetup before any edits — that's what underwrites
    // the M6.3 acceptance "unedited menu run == CLI run".
    {
        UISystem ui(nullptr, UIScreen::PlayerSetup);
        MatchSetup baseline{};
        CHECK(bytewiseEqual(baseline, ui.matchSetup()));
        // The per-slot defaults are the all-sentinel state.
        for (std::size_t i = 0; i < kMatchSetupSlotCount; ++i) {
            const PlayerSlotSetup& s = baseline.playerSlots[i];
            CHECK_EQ(s.tag[0], ' ');
            CHECK_EQ(s.tag[1], ' ');
            CHECK_EQ(s.tag[2], ' ');
            CHECK_EQ(s.role, std::uint8_t{0});
            CHECK_EQ(s.shipKindIdx, std::uint8_t{0xFFu});
            CHECK_EQ(s.paletteIdx, std::uint8_t{0xFFu});
        }
    }

    EXIT_WITH_RESULT();
}
