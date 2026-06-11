/// @file test_input_gamepad_buttons.cpp
/// @brief Gamepad button down/up flips InputState::gamepads[].buttons and
/// fires per-frame press/release edges, mirroring the keyboard contract.

#include "Check.hpp"

#include "threadmaxx_input/backends/NullBackend.hpp"
#include "threadmaxx_input/context.hpp"

int main() {
    using namespace threadmaxx::input;

    NullBackend backend;
    InputContext ctx;
    ctx.setBackend(&backend);

    // Connect + press A on pad 0.
    backend.push(DeviceConnectEvent{kGamepad0DeviceId, true});
    backend.push(GamepadButtonEvent{kGamepad0DeviceId, GamepadButton::A, true});
    ctx.beginFrame(1.0f / 60.0f);
    CHECK(ctx.isHeld(kGamepad0DeviceId, GamepadButton::A));
    CHECK(!ctx.isHeld(kGamepad0DeviceId, GamepadButton::B));
    const auto& pad = ctx.state().gamepads[0];
    CHECK((pad.buttonsPressed & (1u << static_cast<unsigned>(GamepadButton::A))) != 0u);
    CHECK((pad.buttonsReleased & (1u << static_cast<unsigned>(GamepadButton::A))) == 0u);
    ctx.endFrame();

    // Next frame — no events. Held stays, press edge clears.
    ctx.beginFrame(1.0f / 60.0f);
    CHECK(ctx.isHeld(kGamepad0DeviceId, GamepadButton::A));
    const auto& padN = ctx.state().gamepads[0];
    CHECK_EQ(padN.buttonsPressed & (1u << static_cast<unsigned>(GamepadButton::A)),
             std::uint16_t{0});
    ctx.endFrame();

    // Release A.
    backend.push(GamepadButtonEvent{kGamepad0DeviceId, GamepadButton::A, false});
    ctx.beginFrame(1.0f / 60.0f);
    CHECK(!ctx.isHeld(kGamepad0DeviceId, GamepadButton::A));
    const auto& padR = ctx.state().gamepads[0];
    CHECK((padR.buttonsReleased & (1u << static_cast<unsigned>(GamepadButton::A))) != 0u);
    ctx.endFrame();

    // Two pads — buttons on pad 0 don't leak to pad 1.
    backend.push(DeviceConnectEvent{gamepadDeviceId(1), true});
    backend.push(GamepadButtonEvent{kGamepad0DeviceId, GamepadButton::X, true});
    ctx.beginFrame(1.0f / 60.0f);
    CHECK(ctx.isHeld(kGamepad0DeviceId, GamepadButton::X));
    CHECK(!ctx.isHeld(gamepadDeviceId(1), GamepadButton::X));
    ctx.endFrame();

    EXIT_WITH_RESULT();
}
