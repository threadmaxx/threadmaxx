// tou2d_pause_test — M6.4 Pause screen contract.
//
// Pins:
//   * Pause screen row table — Resume, Restart match, Options, Level
//     setup, Return to main menu, Quit, in that on-screen order.
//   * Constructing UISystem with UIScreen::Pause lands focus on the
//     Resume row (first enabled).
//   * Resume accept → setCurrent(None). No pending* flags set.
//   * Restart match accept → pendingRestartMatch sticky + setCurrent(None).
//   * Return to main menu accept → pendingReturnToMainMenu sticky +
//     setCurrent(MainMenu). Focus resets to MainMenu's first enabled.
//   * Level setup accept → setCurrent(MatchSetup); focus to row 0.
//   * Quit accept → pendingQuit sticky; current() stays on Pause (Quit
//     is host-side termination, not a screen transition).
//   * Options accept → no transition, no pending flag (stub).
//   * formatRow paints the static label for every Pause row (all Action
//     rows; no scrollers in this screen).
//   * MainMenu's "Continue" row is greyed (placeholder for in-flight
//     resume — wires up in a focused follow-up).
//   * Pending flags are independent — clearing one doesn't affect the
//     others.

#include "Check.hpp"

#include "../examples/tou2d/DemoTypes.hpp"
#include "../examples/tou2d/UISystem.hpp"

#include <threadmaxx/Engine.hpp>

#include <cstring>

int main() {
    using tou2d::MenuAction;
    using tou2d::MenuRowKind;
    using tou2d::UIScreen;
    using tou2d::UISystem;

    // ---- Pause row table pinned ------------------------------------
    {
        UISystem ui(nullptr, UIScreen::Pause);
        const auto rs = ui.currentRows();
        CHECK_EQ(rs.size(), std::size_t{6});

        const MenuAction expectedActions[] = {
            MenuAction::Resume,
            MenuAction::RestartMatch,
            MenuAction::Options,
            MenuAction::LevelSetup,
            MenuAction::ReturnToMainMenu,
            MenuAction::Quit,
        };
        const char* expectedLabels[] = {
            "Resume",
            "Restart match",
            "Options",
            "Level setup",
            "Return to main menu",
            "Quit",
        };
        for (std::size_t i = 0; i < rs.size(); ++i) {
            CHECK(rs[i].kind == MenuRowKind::Action);
            CHECK(rs[i].action == expectedActions[i]);
            CHECK(rs[i].enabled);
            CHECK(std::strcmp(rs[i].label, expectedLabels[i]) == 0);
        }

        // Focus starts on Resume (first enabled row).
        CHECK_EQ(ui.focusIndex(), std::int32_t{0});
    }

    // ---- Resume accept: dismisses menu, no pending* flags ----------
    {
        UISystem ui(nullptr, UIScreen::Pause);
        CHECK(!ui.pendingRestartMatch());
        CHECK(!ui.pendingReturnToMainMenu());
        CHECK(!ui.pendingQuit());
        const MenuAction got = ui.acceptFocused();
        CHECK(got == MenuAction::Resume);
        CHECK(ui.current() == UIScreen::None);
        CHECK(!ui.pendingRestartMatch());
        CHECK(!ui.pendingReturnToMainMenu());
        CHECK(!ui.pendingQuit());
    }

    // ---- Restart match accept: sticky flag + dismiss menu ----------
    {
        UISystem ui(nullptr, UIScreen::Pause);
        ui.moveFocus(+1);  // Restart match (index 1)
        CHECK_EQ(ui.focusIndex(), std::int32_t{1});
        CHECK(!ui.pendingRestartMatch());
        const MenuAction got = ui.acceptFocused();
        CHECK(got == MenuAction::RestartMatch);
        CHECK(ui.pendingRestartMatch());
        CHECK(ui.current() == UIScreen::None);
        // Sticky until cleared.
        CHECK(ui.pendingRestartMatch());
        ui.clearPendingRestartMatch();
        CHECK(!ui.pendingRestartMatch());
    }

    // ---- Return to main menu accept: sticky + jump to MainMenu -----
    {
        UISystem ui(nullptr, UIScreen::Pause);
        // ReturnToMainMenu is index 4: Resume(0) RestartMatch(1)
        // Options(2) LevelSetup(3) ReturnToMainMenu(4).
        for (int i = 0; i < 4; ++i) ui.moveFocus(+1);
        CHECK_EQ(ui.focusIndex(), std::int32_t{4});
        CHECK(!ui.pendingReturnToMainMenu());
        const MenuAction got = ui.acceptFocused();
        CHECK(got == MenuAction::ReturnToMainMenu);
        CHECK(ui.pendingReturnToMainMenu());
        CHECK(ui.current() == UIScreen::MainMenu);
        // MainMenu's first enabled row is Single Match (index 1).
        CHECK_EQ(ui.focusIndex(), std::int32_t{1});
        ui.clearPendingReturnToMainMenu();
        CHECK(!ui.pendingReturnToMainMenu());
    }

    // ---- Level setup from Pause: jump to MatchSetup ----------------
    {
        UISystem ui(nullptr, UIScreen::Pause);
        // LevelSetup is index 3.
        for (int i = 0; i < 3; ++i) ui.moveFocus(+1);
        CHECK_EQ(ui.focusIndex(), std::int32_t{3});
        const MenuAction got = ui.acceptFocused();
        CHECK(got == MenuAction::LevelSetup);
        CHECK(ui.current() == UIScreen::MatchSetup);
        CHECK_EQ(ui.focusIndex(), std::int32_t{0});
        // No Pause-specific pending flags fired.
        CHECK(!ui.pendingRestartMatch());
        CHECK(!ui.pendingReturnToMainMenu());
    }

    // ---- Quit from Pause: sticky, current() stays on Pause ---------
    {
        UISystem ui(nullptr, UIScreen::Pause);
        // Quit is the last row (index 5).
        for (int i = 0; i < 5; ++i) ui.moveFocus(+1);
        CHECK_EQ(ui.focusIndex(), std::int32_t{5});
        CHECK(!ui.pendingQuit());
        const MenuAction got = ui.acceptFocused();
        CHECK(got == MenuAction::Quit);
        CHECK(ui.pendingQuit());
        CHECK(ui.current() == UIScreen::Pause);
        ui.clearPendingQuit();
        CHECK(!ui.pendingQuit());
    }

    // ---- Options from Pause: stub log, stays on Pause --------------
    {
        UISystem ui(nullptr, UIScreen::Pause);
        ui.moveFocus(+1);  // RestartMatch (1)
        ui.moveFocus(+1);  // Options       (2)
        CHECK_EQ(ui.focusIndex(), std::int32_t{2});
        const MenuAction got = ui.acceptFocused();
        CHECK(got == MenuAction::Options);
        CHECK(ui.current() == UIScreen::Pause);
        CHECK(!ui.pendingRestartMatch());
        CHECK(!ui.pendingReturnToMainMenu());
    }

    // ---- formatRow paints the static label for every Pause row -----
    {
        UISystem ui(nullptr, UIScreen::Pause);
        char buf[64];
        const char* expectedLabels[] = {
            "Resume",
            "Restart match",
            "Options",
            "Level setup",
            "Return to main menu",
            "Quit",
        };
        for (std::int32_t i = 0; i < 6; ++i) {
            ui.formatRow(i, buf, sizeof(buf));
            CHECK(std::strcmp(buf, expectedLabels[i]) == 0);
        }
        // Out-of-range row index yields empty string.
        ui.formatRow(99, buf, sizeof(buf));
        CHECK_EQ(std::strlen(buf), std::size_t{0});
    }

    // ---- MainMenu's "Continue" row remains greyed ------------------
    // Continue is the placeholder for "resume in-flight match"; its
    // proper wiring depends on the same engine-restart machinery
    // Restart match is waiting on. The Pause screen's Resume row is
    // the working analogue today (Resume returns to live gameplay
    // because the engine was paused, not torn down).
    {
        UISystem ui(nullptr, UIScreen::MainMenu);
        const auto rs = ui.currentRows();
        CHECK(rs[0].action == MenuAction::Continue);
        CHECK(!rs[0].enabled);
    }

    // ---- Pending flags are independent -----------------------------
    {
        UISystem ui(nullptr, UIScreen::Pause);
        // Trigger pendingRestartMatch_.
        ui.moveFocus(+1);
        ui.acceptFocused();
        CHECK(ui.pendingRestartMatch());
        // Re-enter Pause and trigger pendingReturnToMainMenu_.
        ui.setCurrent(UIScreen::Pause);
        for (int i = 0; i < 4; ++i) ui.moveFocus(+1);
        ui.acceptFocused();
        CHECK(ui.pendingRestartMatch());
        CHECK(ui.pendingReturnToMainMenu());
        // Clearing one leaves the other.
        ui.clearPendingRestartMatch();
        CHECK(!ui.pendingRestartMatch());
        CHECK(ui.pendingReturnToMainMenu());
        ui.clearPendingReturnToMainMenu();
        CHECK(!ui.pendingReturnToMainMenu());
    }

    EXIT_WITH_RESULT();
}
