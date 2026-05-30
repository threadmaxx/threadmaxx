// tou2d_options_screen_test — M6.5 contract pin for the Options menu
// surface.
//
// Pins:
//   * UIScreen enum stability — Options + the six sub-screen values
//     have specific numeric IDs (settings.dat would store transitions
//     under future replay support; reorder = break wire shape).
//   * MainMenu Options row transitions to UIScreen::Options.
//   * Options → category Action rows transition to the matching
//     sub-screen (Video / Audio / Controls / Gameplay / Accessibility
//     / Benchmark).
//   * Options sub-screen Back rows return to UIScreen::Options.
//   * Options → Back transitions to MainMenu AND sets
//     `pendingSettingsSave_`.
//   * cycleFocused on an Options→Audio knob mutates `settings_.audio.*`
//     and emits exactly one `AudioVolumeChanged` event per cycle.
//   * cycleFocused on an Options→Accessibility toggle wraps 0↔1.
//   * cycleFocused on an Options→Video UI-scale advances through the
//     preset list 75/100/125/150.
//   * Benchmark presets pre-fill matchSetup_ + set pendingStartMatch_
//     + dismiss to UIScreen::None.

#include "Check.hpp"

#include "../examples/tou2d/DemoTypes.hpp"
#include "../examples/tou2d/Settings.hpp"
#include "../examples/tou2d/UISystem.hpp"

#include <threadmaxx/Engine.hpp>
#include <threadmaxx/EventChannel.hpp>

#include <cstdio>
#include <string>

int main() {
    using tou2d::AudioVolumeChanged;
    using tou2d::MenuAction;
    using tou2d::MenuRowKind;
    using tou2d::Settings;
    using tou2d::SettingsKnob;
    using tou2d::UIScreen;
    using tou2d::UISystem;

    // ---- 1. UIScreen enum IDs ---------------------------------------
    CHECK_EQ(static_cast<std::uint8_t>(UIScreen::Options),              std::uint8_t{4});
    CHECK_EQ(static_cast<std::uint8_t>(UIScreen::OptionsVideo),         std::uint8_t{8});
    CHECK_EQ(static_cast<std::uint8_t>(UIScreen::OptionsAudio),         std::uint8_t{9});
    CHECK_EQ(static_cast<std::uint8_t>(UIScreen::OptionsControls),      std::uint8_t{10});
    CHECK_EQ(static_cast<std::uint8_t>(UIScreen::OptionsGameplay),      std::uint8_t{11});
    CHECK_EQ(static_cast<std::uint8_t>(UIScreen::OptionsAccessibility), std::uint8_t{12});
    CHECK_EQ(static_cast<std::uint8_t>(UIScreen::OptionsBenchmark),     std::uint8_t{13});

    // ---- 2. MainMenu Options → UIScreen::Options --------------------
    {
        UISystem ui(nullptr, UIScreen::MainMenu);
        // MainMenu rows: Continue(disabled, skipped), SingleMatch,
        // LevelSetup, Options, Benchmark/Stress, Credits, Quit.
        // Default focus skips Continue (it's disabled) → focus = 1.
        CHECK_EQ(ui.focusIndex(), std::int32_t{1});
        ui.moveFocus(+2);  // Options (index 3)
        CHECK_EQ(ui.focusIndex(), std::int32_t{3});
        const MenuAction got = ui.acceptFocused();
        CHECK(got == MenuAction::Options);
        CHECK(ui.current() == UIScreen::Options);
    }

    // ---- 3. Options → categories transition into sub-screens -------
    struct Case {
        std::int32_t focus;   // which row to accept
        MenuAction   expectedAction;
        UIScreen     expectedScreen;
    };
    const Case cases[] = {
        { 0, MenuAction::OptionsVideo,         UIScreen::OptionsVideo },
        { 1, MenuAction::OptionsAudio,         UIScreen::OptionsAudio },
        { 2, MenuAction::OptionsControls,      UIScreen::OptionsControls },
        { 3, MenuAction::OptionsGameplay,      UIScreen::OptionsGameplay },
        { 4, MenuAction::OptionsAccessibility, UIScreen::OptionsAccessibility },
        { 5, MenuAction::OptionsBenchmark,     UIScreen::OptionsBenchmark },
    };
    for (const auto& c : cases) {
        UISystem ui(nullptr, UIScreen::Options);
        for (std::int32_t i = 0; i < c.focus; ++i) ui.moveFocus(+1);
        CHECK_EQ(ui.focusIndex(), c.focus);
        const MenuAction got = ui.acceptFocused();
        CHECK(got == c.expectedAction);
        CHECK(ui.current() == c.expectedScreen);
    }

    // ---- 4. Sub-screen Back → UIScreen::Options ---------------------
    {
        UISystem ui(nullptr, UIScreen::OptionsAudio);
        // Audio rows: Master(0), Music(1), SFX(2), Back(3).
        ui.moveFocus(+3);
        CHECK_EQ(ui.focusIndex(), std::int32_t{3});
        const MenuAction got = ui.acceptFocused();
        CHECK(got == MenuAction::BackToMain);
        CHECK(ui.current() == UIScreen::Options);
        // pendingSettingsSave_ NOT set yet — only top-level Options Back
        // triggers the save.
        CHECK(!ui.pendingSettingsSave());
    }

    // ---- 5. Options → Back → MainMenu + pendingSettingsSave -------
    {
        UISystem ui(nullptr, UIScreen::Options);
        // Options rows: Video(0), Audio(1), Controls(2), Gameplay(3),
        // Accessibility(4), Benchmark/Presets(5), Back(6).
        ui.moveFocus(+6);
        CHECK_EQ(ui.focusIndex(), std::int32_t{6});
        const MenuAction got = ui.acceptFocused();
        CHECK(got == MenuAction::BackToMain);
        CHECK(ui.current() == UIScreen::MainMenu);
        CHECK(ui.pendingSettingsSave());
        ui.clearPendingSettingsSave();
        CHECK(!ui.pendingSettingsSave());
    }

    // ---- 6. Audio cycle mutates settings_ + emits AudioVolumeChanged
    {
        threadmaxx::Engine eng{};
        UISystem ui(&eng, UIScreen::OptionsAudio);

        std::size_t evCount = 0;
        AudioVolumeChanged last{};
        auto sub = eng.events<AudioVolumeChanged>().subscribeScoped(
            [&](const AudioVolumeChanged& ev) {
                ++evCount;
                last = ev;
            });

        // Default master = 80 (Settings default). focus = Master row.
        CHECK_EQ(ui.focusIndex(), std::int32_t{0});
        CHECK_EQ(ui.settings().audio.master, std::uint8_t{80});

        ui.cycleFocused(+1);
        CHECK_EQ(ui.settings().audio.master, std::uint8_t{85});

        // Drain event channel so the subscription fires.
        eng.events<AudioVolumeChanged>().drain();
        CHECK_EQ(evCount, std::size_t{1});
        CHECK_EQ(last.master, std::uint8_t{85});
        CHECK_EQ(last.music,  std::uint8_t{80});
        CHECK_EQ(last.sfx,    std::uint8_t{80});

        // Negative cycle from 85 → 80.
        ui.cycleFocused(-1);
        CHECK_EQ(ui.settings().audio.master, std::uint8_t{80});
        eng.events<AudioVolumeChanged>().drain();
        CHECK_EQ(evCount, std::size_t{2});

        // Wraparound: from 0 → 100 with delta = -1.
        // First cycle down 16 times: 80→0.
        for (int i = 0; i < 16; ++i) ui.cycleFocused(-1);
        CHECK_EQ(ui.settings().audio.master, std::uint8_t{0});
        ui.cycleFocused(-1);
        CHECK_EQ(ui.settings().audio.master, std::uint8_t{100});
    }

    // ---- 7. Accessibility toggle wraps 0↔1 -------------------------
    {
        UISystem ui(nullptr, UIScreen::OptionsAccessibility);
        // Rows: HudScale(0), BigWarnings(1), ScreenShake(2),
        // Photosensitive(3), Back(4). Focus the BigWarnings toggle.
        ui.moveFocus(+1);
        CHECK_EQ(ui.focusIndex(), std::int32_t{1});
        CHECK_EQ(ui.settings().accessibility.bigWarnings, std::uint8_t{0});
        ui.cycleFocused(+1);
        CHECK_EQ(ui.settings().accessibility.bigWarnings, std::uint8_t{1});
        ui.cycleFocused(+1);
        CHECK_EQ(ui.settings().accessibility.bigWarnings, std::uint8_t{0});
    }

    // ---- 8. Video UI-scale advances through presets ----------------
    {
        UISystem ui(nullptr, UIScreen::OptionsVideo);
        // Rows: Resolution(Display)(0), Fullscreen(1), VSync(2),
        // UiScale(3), Back(4). Initial focus skips Display → index 1.
        CHECK_EQ(ui.focusIndex(), std::int32_t{1});
        ui.moveFocus(+2);  // UiScale (3)
        CHECK_EQ(ui.focusIndex(), std::int32_t{3});
        CHECK_EQ(ui.settings().video.uiScale, std::uint8_t{100});
        ui.cycleFocused(+1);
        CHECK_EQ(ui.settings().video.uiScale, std::uint8_t{125});
        ui.cycleFocused(+1);
        CHECK_EQ(ui.settings().video.uiScale, std::uint8_t{150});
        ui.cycleFocused(+1);  // wraps to 75
        CHECK_EQ(ui.settings().video.uiScale, std::uint8_t{75});
        ui.cycleFocused(-1);  // wraps back to 150
        CHECK_EQ(ui.settings().video.uiScale, std::uint8_t{150});
    }

    // ---- 9. Benchmark preset pre-fills matchSetup + starts ---------
    {
        UISystem ui(nullptr, UIScreen::OptionsBenchmark);
        // Rows: TraceSink(0), ScriptedSkip(1), Preset1(2), Preset2(3),
        // Preset3(4), Back(5). Focus Preset2.
        ui.moveFocus(+3);
        CHECK_EQ(ui.focusIndex(), std::int32_t{3});
        const MenuAction got = ui.acceptFocused();
        CHECK(got == MenuAction::BenchmarkPreset2);
        CHECK_EQ(ui.matchSetup().numHumans, std::uint8_t{1});
        CHECK_EQ(ui.matchSetup().numBots,   std::uint8_t{32});
        CHECK(ui.matchSetup().useGen);
        CHECK(ui.pendingStartMatch());
        CHECK(ui.current() == UIScreen::None);
        ui.clearPendingStartMatch();
    }

    // ---- 10. Resolution Display row formats current res ------------
    {
        UISystem ui(nullptr, UIScreen::OptionsVideo);
        Settings s{};
        s.video.resolutionW = 2560;
        s.video.resolutionH = 1440;
        ui.setSettings(s);
        char buf[64];
        const std::size_t n = ui.formatRow(0, buf, sizeof(buf));
        CHECK(n > 0);
        CHECK_EQ(std::string{buf}, std::string{"Resolution: 2560x1440"});
    }

    EXIT_WITH_RESULT();
}
