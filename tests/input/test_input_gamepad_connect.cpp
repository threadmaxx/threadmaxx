/// @file test_input_gamepad_connect.cpp
/// @brief DeviceConnectEvent flips connected; the context query forwards
/// to the gamepad slot derived from DeviceId.

#include "Check.hpp"

#include "threadmaxx_input/backends/NullBackend.hpp"
#include "threadmaxx_input/context.hpp"

int main() {
    using namespace threadmaxx::input;

    NullBackend backend;
    InputContext ctx;
    ctx.setBackend(&backend);

    // Initially nothing connected.
    ctx.beginFrame(1.0f / 60.0f);
    CHECK(!ctx.isGamepadConnected(kGamepad0DeviceId));
    CHECK(!ctx.isGamepadConnected(gamepadDeviceId(1)));
    ctx.endFrame();

    // Connect pad 0.
    backend.push(DeviceConnectEvent{kGamepad0DeviceId, true});
    ctx.beginFrame(1.0f / 60.0f);
    CHECK(ctx.isGamepadConnected(kGamepad0DeviceId));
    CHECK(!ctx.isGamepadConnected(gamepadDeviceId(1)));
    ctx.endFrame();

    // Connect pad 1.
    backend.push(DeviceConnectEvent{gamepadDeviceId(1), true});
    ctx.beginFrame(1.0f / 60.0f);
    CHECK(ctx.isGamepadConnected(kGamepad0DeviceId));
    CHECK(ctx.isGamepadConnected(gamepadDeviceId(1)));
    ctx.endFrame();

    // Non-gamepad device ids (keyboard, mouse) report disconnected for the
    // gamepad query — sanity bound.
    CHECK(!ctx.isGamepadConnected(kKeyboardDeviceId));
    CHECK(!ctx.isGamepadConnected(kMouseDeviceId));

    EXIT_WITH_RESULT();
}
