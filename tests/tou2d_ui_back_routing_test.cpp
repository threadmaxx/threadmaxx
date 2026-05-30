// tou2d_ui_back_routing_test — M6.10 universal-Esc routing contract.
//
// Pins `UISystem::triggerBack()` — the host's UiCancel-edge handler in
// main.cpp routes every in-menu Esc press through this method. The pre-
// M6.10 inline UiCancel branch in main.cpp short-cut-jumped any non-
// Pause sub-screen straight to MainMenu, which silently skipped:
//   1. The PARENT screen in nested trees (OptionsVideo → Options →
//      MainMenu collapsed to OptionsVideo → MainMenu).
//   2. The `pendingSettingsSave_` flip that lives on the Options →
//      MainMenu arm — Options sub-screen edits made via Esc-out were
//      silently dropped.
//
// Acceptance:
//   * From every UIScreen, `triggerBack()` lands at the documented
//     parent in one call.
//   * MainMenu / None are no-ops.
//   * The Options → MainMenu hop is the ONLY arm that flips
//     `pendingSettingsSave_`. The OptionsX → Options hop must NOT.
//   * The full screen graph is reachable from any starting screen with
//     ≤ 3 successive `triggerBack()` calls — the "Esc-Esc-Esc lands at
//     MainMenu" universal-abort contract from M6.10.

#include "Check.hpp"

#include "../examples/tou2d/DemoTypes.hpp"
#include "../examples/tou2d/UISystem.hpp"

#include <threadmaxx/Engine.hpp>

#include <array>

int main() {
    using tou2d::UIScreen;
    using tou2d::UISystem;

    // ---- Single-hop parent contract --------------------------------
    {
        UISystem ui(nullptr, UIScreen::Pause);
        CHECK(ui.triggerBack() == UIScreen::None);
        CHECK(ui.current() == UIScreen::None);
        CHECK(!ui.pendingSettingsSave());
    }
    {
        UISystem ui(nullptr, UIScreen::PlayerSetup);
        CHECK(ui.triggerBack() == UIScreen::MatchSetup);
        CHECK(ui.current() == UIScreen::MatchSetup);
        CHECK(!ui.pendingSettingsSave());
    }
    {
        UISystem ui(nullptr, UIScreen::MatchSetup);
        CHECK(ui.triggerBack() == UIScreen::MainMenu);
        CHECK(!ui.pendingSettingsSave());
    }
    {
        UISystem ui(nullptr, UIScreen::Results);
        CHECK(ui.triggerBack() == UIScreen::MainMenu);
        CHECK(!ui.pendingSettingsSave());
    }
    {
        UISystem ui(nullptr, UIScreen::Credits);
        CHECK(ui.triggerBack() == UIScreen::MainMenu);
        CHECK(!ui.pendingSettingsSave());
    }

    // ---- Options sub-screens collapse to Options first -------------
    // PRE-M6.10 BUG REGRESSION: jumped Options* → MainMenu in one hop,
    // also dropping the settings save. Post-M6.10: Options* → Options
    // (no save), THEN Options → MainMenu (save fires).
    constexpr std::array<UIScreen, 6> kOptionsSubs = {
        UIScreen::OptionsVideo,
        UIScreen::OptionsAudio,
        UIScreen::OptionsControls,
        UIScreen::OptionsGameplay,
        UIScreen::OptionsAccessibility,
        UIScreen::OptionsBenchmark,
    };
    for (UIScreen sub : kOptionsSubs) {
        UISystem ui(nullptr, sub);
        // Hop 1 — sub-screen → Options. NO save yet.
        CHECK(ui.triggerBack() == UIScreen::Options);
        CHECK(!ui.pendingSettingsSave());
        // Hop 2 — Options → MainMenu. Save fires here.
        CHECK(ui.triggerBack() == UIScreen::MainMenu);
        CHECK(ui.pendingSettingsSave());
        ui.clearPendingSettingsSave();
        CHECK(!ui.pendingSettingsSave());
    }

    // ---- Options direct back-out also saves ------------------------
    {
        UISystem ui(nullptr, UIScreen::Options);
        CHECK(!ui.pendingSettingsSave());
        CHECK(ui.triggerBack() == UIScreen::MainMenu);
        CHECK(ui.pendingSettingsSave());
    }

    // ---- MainMenu + None are no-ops -------------------------------
    {
        UISystem ui(nullptr, UIScreen::MainMenu);
        CHECK(ui.triggerBack() == UIScreen::MainMenu);
        CHECK(!ui.pendingSettingsSave());
    }
    {
        UISystem ui(nullptr, UIScreen::None);
        CHECK(ui.triggerBack() == UIScreen::None);
        CHECK(!ui.pendingSettingsSave());
    }

    // ---- Universal abort: ≤ 3 Esc presses from any screen lands at
    //      MainMenu (or None, for Pause). The deepest in-menu state is
    //      an Options sub-screen at depth 2 (sub → Options → MainMenu),
    //      so 3 presses always lands the user somewhere stable.
    constexpr std::array<UIScreen, 12> kAllScreens = {
        UIScreen::None,
        UIScreen::MainMenu,
        UIScreen::MatchSetup,
        UIScreen::PlayerSetup,
        UIScreen::Options,
        UIScreen::Pause,
        UIScreen::Results,
        UIScreen::Credits,
        UIScreen::OptionsVideo,
        UIScreen::OptionsAudio,
        UIScreen::OptionsControls,
        UIScreen::OptionsGameplay,
    };
    for (UIScreen start : kAllScreens) {
        UISystem ui(nullptr, start);
        for (int i = 0; i < 3; ++i) {
            ui.triggerBack();
        }
        // Pause collapses to None (gameplay) — universal abort while
        // in-menu means MainMenu OR back to the game (Pause is a
        // gameplay-side overlay; you're already "out" once dismissed).
        const UIScreen end = ui.current();
        CHECK(end == UIScreen::MainMenu || end == UIScreen::None);
    }

    return 0;
}
