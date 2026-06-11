#pragma once

#include <array>
#include <cstdint>

#include "threadmaxx_input/config.hpp"
#include "threadmaxx_input/detail/edge_buffer.hpp"
#include "threadmaxx_input/types.hpp"

namespace threadmaxx::input {

struct MouseState {
    float x{};
    float y{};
    float dx{};
    float dy{};
    float wheelDx{};
    float wheelDy{};
    std::uint8_t buttons{};
    std::uint8_t buttonsPressed{};
    std::uint8_t buttonsReleased{};
};

struct GamepadState {
    bool connected{};
    std::uint16_t buttons{};
    std::uint16_t buttonsPressed{};
    std::uint16_t buttonsReleased{};
    std::array<float, static_cast<std::size_t>(GamepadAxis::Count)> axes{};
};

struct InputState {
    std::uint8_t modifiers{};
    detail::KeyBitset keys{};
    detail::KeyBitset keysPressed{};
    detail::KeyBitset keysReleased{};
    MouseState mouse{};
    std::array<GamepadState, kMaxGamepads> gamepads{};
    std::array<std::uint32_t, kMaxCharsPerFrame> chars{};
    std::uint8_t charCount{};
};

}  // namespace threadmaxx::input
