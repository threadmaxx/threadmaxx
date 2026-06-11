/// @file test_input_cursor_locked_relative.cpp
/// @brief In Locked cursor mode the context exposes only relative deltas;
/// absolute mouse.x/y is left frozen at the pre-lock value even as
/// MouseMoveEvents continue to arrive.

#include "Check.hpp"

#include "threadmaxx_input/backends/NullBackend.hpp"
#include "threadmaxx_input/context.hpp"

int main() {
    using namespace threadmaxx::input;

    NullBackend backend;
    InputContext ctx;
    ctx.setBackend(&backend);

    // Frame 0 — set an initial absolute position in Visible mode.
    backend.push(MouseMoveEvent{100.0f, 200.0f, 0.0f, 0.0f});
    ctx.beginFrame(1.0f / 60.0f);
    CHECK_EQ(ctx.state().mouse.x, 100.0f);
    CHECK_EQ(ctx.state().mouse.y, 200.0f);
    ctx.endFrame();

    // Lock the cursor.
    ctx.setCursorMode(CursorMode::Locked);

    // Frame 1 — multiple move events. dx/dy MUST accumulate; x/y MUST
    // stay frozen at the pre-lock value.
    backend.push(MouseMoveEvent{999.0f, 999.0f, 5.0f, 3.0f});
    backend.push(MouseMoveEvent{888.0f, 888.0f, -2.0f, 1.0f});
    ctx.beginFrame(1.0f / 60.0f);
    CHECK_EQ(ctx.state().mouse.x, 100.0f);
    CHECK_EQ(ctx.state().mouse.y, 200.0f);
    CHECK_EQ(ctx.state().mouse.dx, 3.0f);
    CHECK_EQ(ctx.state().mouse.dy, 4.0f);
    ctx.endFrame();

    // Frame 2 — mouse button event also doesn't move the absolute cursor.
    backend.push(MouseButtonEvent{MouseButton::Left, true, 50.0f, 60.0f});
    ctx.beginFrame(1.0f / 60.0f);
    CHECK_EQ(ctx.state().mouse.x, 100.0f);
    CHECK_EQ(ctx.state().mouse.y, 200.0f);
    CHECK(ctx.isHeld(MouseButton::Left));
    ctx.endFrame();

    // Unlock — absolute position resumes tracking next move event.
    ctx.setCursorMode(CursorMode::Visible);
    backend.push(MouseMoveEvent{500.0f, 500.0f, 10.0f, 10.0f});
    ctx.beginFrame(1.0f / 60.0f);
    CHECK_EQ(ctx.state().mouse.x, 500.0f);
    CHECK_EQ(ctx.state().mouse.y, 500.0f);
    ctx.endFrame();

    EXIT_WITH_RESULT();
}
