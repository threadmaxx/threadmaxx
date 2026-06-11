/// @file test_input_mouse_buttons.cpp
/// @brief Pins mouse-button level + press/release edges (mirrors keys).

#include "Check.hpp"

#include "threadmaxx_input/backends/NullBackend.hpp"
#include "threadmaxx_input/context.hpp"

int main() {
    using namespace threadmaxx::input;

    NullBackend backend;
    InputContext ctx;
    ctx.setBackend(&backend);

    // Frame 0 — left button down.
    backend.push(MouseButtonEvent{MouseButton::Left, true, 10.0f, 20.0f});
    ctx.beginFrame(1.0f / 60.0f);
    CHECK(ctx.isHeld(MouseButton::Left));
    CHECK(ctx.wasPressed(MouseButton::Left));
    CHECK(!ctx.wasReleased(MouseButton::Left));
    CHECK(!ctx.isHeld(MouseButton::Right));
    ctx.endFrame();

    // Frame 1 — no events. Press edge clears; held stays.
    ctx.beginFrame(1.0f / 60.0f);
    CHECK(ctx.isHeld(MouseButton::Left));
    CHECK(!ctx.wasPressed(MouseButton::Left));
    ctx.endFrame();

    // Frame 2 — right button down WHILE left still held.
    backend.push(MouseButtonEvent{MouseButton::Right, true, 10.0f, 20.0f});
    ctx.beginFrame(1.0f / 60.0f);
    CHECK(ctx.isHeld(MouseButton::Left));
    CHECK(ctx.isHeld(MouseButton::Right));
    CHECK(ctx.wasPressed(MouseButton::Right));
    CHECK(!ctx.wasPressed(MouseButton::Left));
    ctx.endFrame();

    // Frame 3 — release left only.
    backend.push(MouseButtonEvent{MouseButton::Left, false, 10.0f, 20.0f});
    ctx.beginFrame(1.0f / 60.0f);
    CHECK(!ctx.isHeld(MouseButton::Left));
    CHECK(ctx.wasReleased(MouseButton::Left));
    CHECK(ctx.isHeld(MouseButton::Right));
    CHECK(!ctx.wasReleased(MouseButton::Right));
    ctx.endFrame();

    EXIT_WITH_RESULT();
}
