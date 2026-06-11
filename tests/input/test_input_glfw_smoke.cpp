/// @file test_input_glfw_smoke.cpp
/// @brief GlfwBackend translation layer — feed GLFW-style int-coded
/// callbacks; observe the right InputEvent variants land in the queue.

#include "Check.hpp"

#include "threadmaxx_input/backends/GlfwBackend.hpp"
#include "threadmaxx_input/context.hpp"

int main() {
    using namespace threadmaxx::input;

    GlfwBackend backend;
    InputContext ctx;
    ctx.setBackend(&backend);

    // Key press W (GLFW key 'W'=87, action 1=press, mods 0).
    backend.pushGlfwKey(87, 0, 1, 0);
    // Char callback for 'w'.
    backend.pushGlfwChar(0x77);
    // Mouse move.
    backend.pushGlfwCursorPos(50.0, 60.0);
    backend.pushGlfwCursorPos(55.0, 65.0);
    // Mouse button left (GLFW button 0, press).
    backend.pushGlfwMouseButton(0, 1, 0);
    // Scroll up by 1.
    backend.pushGlfwScroll(0.0, 1.0);
    // Gamepad button A on pad 0.
    backend.pushGlfwGamepadButton(kGamepad0DeviceId, 0, 1);
    // Gamepad axis LStickX = 0.5.
    backend.pushGlfwGamepadAxis(kGamepad0DeviceId, 0, 0.5f);

    ctx.beginFrame(1.0f / 60.0f);
    CHECK(ctx.isHeld(Key::W));
    CHECK_EQ(ctx.state().chars[0], std::uint32_t{0x77});
    CHECK_EQ(ctx.state().mouse.x, 55.0f);
    CHECK_EQ(ctx.state().mouse.y, 65.0f);
    // First move: dx=0 dy=0 (first sample baseline). Second: dx=5, dy=5.
    CHECK_EQ(ctx.state().mouse.dx, 5.0f);
    CHECK_EQ(ctx.state().mouse.dy, 5.0f);
    CHECK(ctx.isHeld(MouseButton::Left));
    CHECK_EQ(ctx.state().mouse.wheelDy, 1.0f);
    CHECK(ctx.isHeld(kGamepad0DeviceId, GamepadButton::A));
    CHECK_EQ(ctx.axis(kGamepad0DeviceId, GamepadAxis::LStickX), 0.5f);
    ctx.endFrame();

    // Repeat events (action=2) are dropped.
    const auto preCount = backend.pendingCount();
    backend.pushGlfwKey(87, 0, 2, 0);
    CHECK_EQ(backend.pendingCount(), preCount);

    // Cursor mode + connect/disconnect.
    backend.setCursorMode(CursorMode::Locked);
    CHECK_EQ(static_cast<int>(backend.cursorMode()), static_cast<int>(CursorMode::Locked));
    CHECK_EQ(backend.cursorModeChangeCount(), std::size_t{1});

    backend.pushDeviceConnect(gamepadDeviceId(2), true);
    CHECK_EQ(backend.connectedGamepads().size(), std::size_t{1});
    CHECK_EQ(backend.connectedGamepads()[0], gamepadDeviceId(2));
    backend.pushDeviceDisconnect(gamepadDeviceId(2));
    CHECK_EQ(backend.connectedGamepads().size(), std::size_t{0});

    EXIT_WITH_RESULT();
}
