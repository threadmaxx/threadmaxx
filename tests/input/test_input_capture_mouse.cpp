/// @file test_input_capture_mouse.cpp
/// @brief setCaptureMouse(true) flips wantsMouse(); raw events keep
/// flowing into state() so the host can still observe them.

#include "Check.hpp"

#include "threadmaxx_input/backends/NullBackend.hpp"
#include "threadmaxx_input/context.hpp"

int main() {
    using namespace threadmaxx::input;

    NullBackend backend;
    InputContext ctx;
    ctx.setBackend(&backend);

    CHECK(!ctx.wantsMouse());

    ctx.setCaptureMouse(true);
    CHECK(ctx.wantsMouse());

    // Capture is a SIGNAL; raw events still update the state.
    backend.push(MouseButtonEvent{MouseButton::Left, true, 10.0f, 20.0f});
    backend.push(MouseMoveEvent{30.0f, 40.0f, 5.0f, 5.0f});
    ctx.beginFrame(1.0f / 60.0f);
    CHECK(ctx.isHeld(MouseButton::Left));
    CHECK_EQ(ctx.state().mouse.x, 30.0f);
    CHECK(ctx.wantsMouse());  // still captured
    ctx.endFrame();

    // Capture persists across frames (sticky semantics).
    ctx.beginFrame(1.0f / 60.0f);
    CHECK(ctx.wantsMouse());
    ctx.endFrame();

    ctx.setCaptureMouse(false);
    CHECK(!ctx.wantsMouse());

    EXIT_WITH_RESULT();
}
