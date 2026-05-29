// tou2d_uiscreen_state_machine_test — M6.0b UISystem behavior pin.
//
// Pins:
//   * UIScreen enum positions (settings.dat will round-trip these).
//   * UIScreenChanged POD size (event-channel payload contract).
//   * UISystem(initial=None) is a no-op on tick (current()==None).
//   * setCurrent emits UIScreenChanged with correct from/to.
//   * Same-screen setCurrent is a no-op (no event emit).
//   * UISystem constructed without an Engine pointer still updates
//     its state (transition is silent).
//   * The skeleton system has empty reads()/writes() — won't conflict
//     with any other system's component access.

#include "Check.hpp"

#include "../examples/tou2d/DemoTypes.hpp"
#include "../examples/tou2d/UISystem.hpp"

#include <threadmaxx/Components.hpp>
#include <threadmaxx/Engine.hpp>
#include <threadmaxx/EventChannel.hpp>

#include <cstdint>
#include <type_traits>
#include <vector>

int main() {
    using tou2d::UIScreen;
    using tou2d::UIScreenChanged;
    using tou2d::UISystem;

    // ---- Enum positions pinned ------------------------------------
    CHECK_EQ(static_cast<std::uint8_t>(UIScreen::None),        std::uint8_t{0});
    CHECK_EQ(static_cast<std::uint8_t>(UIScreen::MainMenu),    std::uint8_t{1});
    CHECK_EQ(static_cast<std::uint8_t>(UIScreen::MatchSetup),  std::uint8_t{2});
    CHECK_EQ(static_cast<std::uint8_t>(UIScreen::PlayerSetup), std::uint8_t{3});
    CHECK_EQ(static_cast<std::uint8_t>(UIScreen::Options),     std::uint8_t{4});
    CHECK_EQ(static_cast<std::uint8_t>(UIScreen::Pause),       std::uint8_t{5});
    CHECK_EQ(static_cast<std::uint8_t>(UIScreen::Results),     std::uint8_t{6});
    CHECK_EQ(static_cast<std::uint8_t>(UIScreen::Credits),     std::uint8_t{7});

    // ---- UIScreenChanged payload size pinned ----------------------
    static_assert(std::is_trivially_copyable_v<UIScreenChanged>);
    static_assert(sizeof(UIScreenChanged) == 8);

    // ---- UISystem default state -----------------------------------
    {
        UISystem ui;
        CHECK_EQ(static_cast<std::uint8_t>(ui.current()),
                 static_cast<std::uint8_t>(UIScreen::None));
        CHECK(!ui.menuActive());
    }

    // ---- setCurrent without engine: state mutates silently --------
    {
        UISystem ui(nullptr, UIScreen::None);
        const bool fired = ui.setCurrent(UIScreen::MainMenu);
        CHECK(fired);
        CHECK_EQ(static_cast<std::uint8_t>(ui.current()),
                 static_cast<std::uint8_t>(UIScreen::MainMenu));
        CHECK(ui.menuActive());
        // Same-screen is no-op.
        CHECK(!ui.setCurrent(UIScreen::MainMenu));
    }

    // ---- setCurrent with engine: emits UIScreenChanged ------------
    {
        threadmaxx::Engine engine;
        UISystem ui(&engine, UIScreen::MainMenu);

        std::vector<UIScreenChanged> events;
        auto sub = engine.events<UIScreenChanged>().subscribeScoped(
            [&events](const UIScreenChanged& ev) {
                events.push_back(ev);
            });

        // No event on construction.
        CHECK_EQ(events.size(), std::size_t{0});

        const bool fired1 = ui.setCurrent(UIScreen::Options);
        CHECK(fired1);
        engine.events<UIScreenChanged>().drain();
        CHECK_EQ(events.size(), std::size_t{1});
        CHECK_EQ(static_cast<std::uint8_t>(events[0].from),
                 static_cast<std::uint8_t>(UIScreen::MainMenu));
        CHECK_EQ(static_cast<std::uint8_t>(events[0].to),
                 static_cast<std::uint8_t>(UIScreen::Options));

        const bool fired2 = ui.setCurrent(UIScreen::Options);  // no-op
        CHECK(!fired2);
        engine.events<UIScreenChanged>().drain();
        CHECK_EQ(events.size(), std::size_t{1});

        const bool fired3 = ui.setCurrent(UIScreen::None);
        CHECK(fired3);
        engine.events<UIScreenChanged>().drain();
        CHECK_EQ(events.size(), std::size_t{2});
        CHECK_EQ(static_cast<std::uint8_t>(events[1].from),
                 static_cast<std::uint8_t>(UIScreen::Options));
        CHECK_EQ(static_cast<std::uint8_t>(events[1].to),
                 static_cast<std::uint8_t>(UIScreen::None));
        CHECK(!ui.menuActive());
    }

    // ---- Empty reads/writes — won't fight any other system ------
    {
        UISystem ui;
        const threadmaxx::ComponentSet r = ui.reads();
        const threadmaxx::ComponentSet w = ui.writes();
        CHECK_EQ(r.bits(), std::uint64_t{0});
        CHECK_EQ(w.bits(), std::uint64_t{0});
    }

    EXIT_WITH_RESULT();
}
