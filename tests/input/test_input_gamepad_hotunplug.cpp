/// @file test_input_gamepad_hotunplug.cpp
/// @brief DeviceDisconnectEvent clears state for the device's slot —
/// buttons drop, axes return to 0, connected goes false. State of OTHER
/// pads is unchanged.

#include "Check.hpp"

#include "threadmaxx_input/backends/NullBackend.hpp"
#include "threadmaxx_input/context.hpp"

int main() {
    using namespace threadmaxx::input;

    NullBackend backend;
    InputContext ctx;
    ctx.setBackend(&backend);

    // Connect two pads, press buttons + tilt sticks on both.
    backend.push(DeviceConnectEvent{kGamepad0DeviceId, true});
    backend.push(DeviceConnectEvent{gamepadDeviceId(1), true});
    backend.push(GamepadButtonEvent{kGamepad0DeviceId, GamepadButton::A, true});
    backend.push(GamepadButtonEvent{gamepadDeviceId(1), GamepadButton::B, true});
    backend.push(GamepadAxisEvent{kGamepad0DeviceId, GamepadAxis::LStickX, 0.5f});
    backend.push(GamepadAxisEvent{gamepadDeviceId(1), GamepadAxis::RStickY, -0.4f});
    ctx.beginFrame(1.0f / 60.0f);
    CHECK(ctx.isGamepadConnected(kGamepad0DeviceId));
    CHECK(ctx.isGamepadConnected(gamepadDeviceId(1)));
    CHECK(ctx.isHeld(kGamepad0DeviceId, GamepadButton::A));
    CHECK(ctx.isHeld(gamepadDeviceId(1), GamepadButton::B));
    CHECK_EQ(ctx.axis(kGamepad0DeviceId, GamepadAxis::LStickX), 0.5f);
    ctx.endFrame();

    // Unplug pad 0.
    backend.push(DeviceDisconnectEvent{kGamepad0DeviceId});
    ctx.beginFrame(1.0f / 60.0f);
    CHECK(!ctx.isGamepadConnected(kGamepad0DeviceId));
    CHECK(!ctx.isHeld(kGamepad0DeviceId, GamepadButton::A));
    CHECK_EQ(ctx.axis(kGamepad0DeviceId, GamepadAxis::LStickX), 0.0f);

    // Pad 1 unaffected.
    CHECK(ctx.isGamepadConnected(gamepadDeviceId(1)));
    CHECK(ctx.isHeld(gamepadDeviceId(1), GamepadButton::B));
    CHECK_EQ(ctx.axis(gamepadDeviceId(1), GamepadAxis::RStickY), -0.4f);
    ctx.endFrame();

    // Reconnect pad 0 — slot returns clean (no leftover button held).
    backend.push(DeviceConnectEvent{kGamepad0DeviceId, true});
    ctx.beginFrame(1.0f / 60.0f);
    CHECK(ctx.isGamepadConnected(kGamepad0DeviceId));
    CHECK(!ctx.isHeld(kGamepad0DeviceId, GamepadButton::A));
    ctx.endFrame();

    EXIT_WITH_RESULT();
}
