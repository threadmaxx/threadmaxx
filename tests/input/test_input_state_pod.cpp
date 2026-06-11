/// @file test_input_state_pod.cpp
/// @brief Pins the InputState POD shape: default-constructs to all zero,
/// trivially copyable, and stays comfortably under the size budget.

#include "Check.hpp"

#include <type_traits>

#include "threadmaxx_input/state.hpp"

int main() {
    using namespace threadmaxx::input;

    // 1) Trivially copyable + standard layout — both required for the POD
    //    contract the spec promises.
    CHECK(std::is_trivially_copyable_v<InputState>);
    CHECK(std::is_trivially_copyable_v<MouseState>);
    CHECK(std::is_trivially_copyable_v<GamepadState>);
    CHECK(std::is_standard_layout_v<InputState>);

    // 2) Default-constructs to all-zero. Every accessor should report idle.
    InputState s{};
    CHECK_EQ(s.modifiers, std::uint8_t{0});
    CHECK_EQ(s.charCount, std::uint8_t{0});
    CHECK_EQ(s.mouse.x, 0.0f);
    CHECK_EQ(s.mouse.y, 0.0f);
    CHECK_EQ(s.mouse.buttons, std::uint8_t{0});
    CHECK_EQ(s.mouse.buttonsPressed, std::uint8_t{0});
    CHECK_EQ(s.mouse.buttonsReleased, std::uint8_t{0});

    for (const auto& pad : s.gamepads) {
        CHECK(!pad.connected);
        CHECK_EQ(pad.buttons, std::uint16_t{0});
        for (float v : pad.axes) CHECK_EQ(v, 0.0f);
    }

    // 3) Key bitset is initially clear.
    for (auto k : {Key::A, Key::Space, Key::Escape, Key::F1, Key::Enter}) {
        CHECK(!s.keys.test(k));
        CHECK(!s.keysPressed.test(k));
        CHECK(!s.keysReleased.test(k));
    }

    // 4) Size budget — InputState carries 8 gamepads + 32 chars + bitsets.
    //    Sanity gate: stays well under a kilobyte.
    CHECK(sizeof(InputState) < 1024);

    EXIT_WITH_RESULT();
}
