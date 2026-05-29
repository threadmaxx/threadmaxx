// tou2d_keymap_default_binding_test — pins the M6.0 KeyMap layout
// and the default per-slot binding table.
//
// Contract:
//   * `Action` enum positions are stable (settings.dat downstream
//     depends on this; reordering would invalidate any saved
//     bindings).
//   * `KeyMap` is plain POD with the documented size; new actions
//     append before kActionCount.
//   * `makeDefaultKeyMap()` populates the same four-slot table the
//     legacy `kRows[4]` shipped pre-M6.0 (so replay determinism is
//     preserved by construction — the same keypresses produce the
//     same `PlayerInput`).
//   * UI navigation bindings exist on slot 0 only (single-cursor
//     contract in v1).
//   * Out-of-range slot reads are zero (don't UB on slot ≥ 4).
//
// This test does NOT link InputSystem (avoids GLFW header bleed)
// — instead it pins the Action/KeyMap layout and recomputes the
// default bindings using GLFW key constants imported here.

#include "Check.hpp"

#include "../examples/tou2d/DemoTypes.hpp"
#include "../examples/tou2d/InputSystem.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cstdint>
#include <cstring>
#include <type_traits>

int main() {
    using tou2d::Action;
    using tou2d::KeyMap;
    using tou2d::kActionCount;
    using tou2d::kMaxHumans;
    using tou2d::kKeyUnbound;
    using tou2d::makeDefaultKeyMap;

    // ---- Action enum positions pinned ----------------------------
    CHECK_EQ(static_cast<std::uint8_t>(Action::Thrust),       std::uint8_t{0});
    CHECK_EQ(static_cast<std::uint8_t>(Action::Back),         std::uint8_t{1});
    CHECK_EQ(static_cast<std::uint8_t>(Action::TurnLeft),     std::uint8_t{2});
    CHECK_EQ(static_cast<std::uint8_t>(Action::TurnRight),    std::uint8_t{3});
    CHECK_EQ(static_cast<std::uint8_t>(Action::FireDumb),     std::uint8_t{4});
    CHECK_EQ(static_cast<std::uint8_t>(Action::FireSpecial),  std::uint8_t{5});
    CHECK_EQ(static_cast<std::uint8_t>(Action::MenuButton),   std::uint8_t{6});
    CHECK_EQ(static_cast<std::uint8_t>(Action::Pause),        std::uint8_t{7});
    CHECK_EQ(static_cast<std::uint8_t>(Action::UiUp),         std::uint8_t{8});
    CHECK_EQ(static_cast<std::uint8_t>(Action::UiDown),       std::uint8_t{9});
    CHECK_EQ(static_cast<std::uint8_t>(Action::UiLeft),       std::uint8_t{10});
    CHECK_EQ(static_cast<std::uint8_t>(Action::UiRight),      std::uint8_t{11});
    CHECK_EQ(static_cast<std::uint8_t>(Action::UiAccept),     std::uint8_t{12});
    CHECK_EQ(static_cast<std::uint8_t>(Action::UiCancel),     std::uint8_t{13});
    CHECK_EQ(kActionCount, std::size_t{14});

    // ---- KeyMap is trivially copyable + flat ---------------------
    static_assert(std::is_trivially_copyable_v<KeyMap>);
    static_assert(sizeof(KeyMap) == kMaxHumans * kActionCount * sizeof(std::uint16_t));
    CHECK_EQ(sizeof(KeyMap), std::size_t{4 * 14 * 2});  // 112 bytes

    // ---- Default bindings: legacy four-row table preserved -------
    const KeyMap km = makeDefaultKeyMap();

    auto bind = [&km](std::uint8_t slot, Action a) -> std::uint16_t {
        return km.binding[slot][static_cast<std::size_t>(a)];
    };

    // P1 ↑↓←→ + RShift + RCtrl + /
    CHECK_EQ(bind(0, Action::Thrust),      std::uint16_t{GLFW_KEY_UP});
    CHECK_EQ(bind(0, Action::Back),        std::uint16_t{GLFW_KEY_DOWN});
    CHECK_EQ(bind(0, Action::TurnLeft),    std::uint16_t{GLFW_KEY_LEFT});
    CHECK_EQ(bind(0, Action::TurnRight),   std::uint16_t{GLFW_KEY_RIGHT});
    CHECK_EQ(bind(0, Action::FireDumb),    std::uint16_t{GLFW_KEY_RIGHT_SHIFT});
    CHECK_EQ(bind(0, Action::FireSpecial), std::uint16_t{GLFW_KEY_RIGHT_CONTROL});
    CHECK_EQ(bind(0, Action::MenuButton),  std::uint16_t{GLFW_KEY_SLASH});

    // P2 WSAD + LShift + LCtrl + Tab
    CHECK_EQ(bind(1, Action::Thrust),      std::uint16_t{GLFW_KEY_W});
    CHECK_EQ(bind(1, Action::Back),        std::uint16_t{GLFW_KEY_S});
    CHECK_EQ(bind(1, Action::TurnLeft),    std::uint16_t{GLFW_KEY_A});
    CHECK_EQ(bind(1, Action::TurnRight),   std::uint16_t{GLFW_KEY_D});
    CHECK_EQ(bind(1, Action::FireDumb),    std::uint16_t{GLFW_KEY_LEFT_SHIFT});
    CHECK_EQ(bind(1, Action::FireSpecial), std::uint16_t{GLFW_KEY_LEFT_CONTROL});
    CHECK_EQ(bind(1, Action::MenuButton),  std::uint16_t{GLFW_KEY_TAB});

    // P3 IJKL + Y + H + U
    CHECK_EQ(bind(2, Action::Thrust),      std::uint16_t{GLFW_KEY_I});
    CHECK_EQ(bind(2, Action::Back),        std::uint16_t{GLFW_KEY_K});
    CHECK_EQ(bind(2, Action::TurnLeft),    std::uint16_t{GLFW_KEY_J});
    CHECK_EQ(bind(2, Action::TurnRight),   std::uint16_t{GLFW_KEY_L});
    CHECK_EQ(bind(2, Action::FireDumb),    std::uint16_t{GLFW_KEY_Y});
    CHECK_EQ(bind(2, Action::FireSpecial), std::uint16_t{GLFW_KEY_H});
    CHECK_EQ(bind(2, Action::MenuButton),  std::uint16_t{GLFW_KEY_U});

    // P4 numpad 8/2/4/6 + KP_0 + KP_DECIMAL + KP_ENTER
    CHECK_EQ(bind(3, Action::Thrust),      std::uint16_t{GLFW_KEY_KP_8});
    CHECK_EQ(bind(3, Action::Back),        std::uint16_t{GLFW_KEY_KP_2});
    CHECK_EQ(bind(3, Action::TurnLeft),    std::uint16_t{GLFW_KEY_KP_4});
    CHECK_EQ(bind(3, Action::TurnRight),   std::uint16_t{GLFW_KEY_KP_6});
    CHECK_EQ(bind(3, Action::FireDumb),    std::uint16_t{GLFW_KEY_KP_0});
    CHECK_EQ(bind(3, Action::FireSpecial), std::uint16_t{GLFW_KEY_KP_DECIMAL});
    CHECK_EQ(bind(3, Action::MenuButton),  std::uint16_t{GLFW_KEY_KP_ENTER});

    // ---- UI navigation lives on slot 0 only ---------------------
    CHECK_EQ(bind(0, Action::Pause),    std::uint16_t{GLFW_KEY_ESCAPE});
    CHECK_EQ(bind(0, Action::UiUp),     std::uint16_t{GLFW_KEY_UP});
    CHECK_EQ(bind(0, Action::UiDown),   std::uint16_t{GLFW_KEY_DOWN});
    CHECK_EQ(bind(0, Action::UiLeft),   std::uint16_t{GLFW_KEY_LEFT});
    CHECK_EQ(bind(0, Action::UiRight),  std::uint16_t{GLFW_KEY_RIGHT});
    CHECK_EQ(bind(0, Action::UiAccept), std::uint16_t{GLFW_KEY_ENTER});
    CHECK_EQ(bind(0, Action::UiCancel), std::uint16_t{GLFW_KEY_ESCAPE});

    // Slots 1..3 leave UI actions unbound.
    for (std::uint8_t slot = 1; slot < kMaxHumans; ++slot) {
        CHECK_EQ(bind(slot, Action::Pause),    kKeyUnbound);
        CHECK_EQ(bind(slot, Action::UiUp),     kKeyUnbound);
        CHECK_EQ(bind(slot, Action::UiDown),   kKeyUnbound);
        CHECK_EQ(bind(slot, Action::UiLeft),   kKeyUnbound);
        CHECK_EQ(bind(slot, Action::UiRight),  kKeyUnbound);
        CHECK_EQ(bind(slot, Action::UiAccept), kKeyUnbound);
        CHECK_EQ(bind(slot, Action::UiCancel), kKeyUnbound);
    }

    // ---- Two default keymaps are bit-identical (pure function) --
    {
        const KeyMap a = makeDefaultKeyMap();
        const KeyMap b = makeDefaultKeyMap();
        CHECK_EQ(std::memcmp(&a, &b, sizeof(KeyMap)), 0);
    }

    EXIT_WITH_RESULT();
}
