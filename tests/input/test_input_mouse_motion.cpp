/// @file test_input_mouse_motion.cpp
/// @brief Pins mouse-motion tracking: absolute position follows the most
/// recent event; per-frame delta accumulates across multiple events; the
/// delta resets to 0 each beginFrame; wheel deltas accumulate similarly.

#include "Check.hpp"

#include "threadmaxx_input/backends/NullBackend.hpp"
#include "threadmaxx_input/context.hpp"

int main() {
    using namespace threadmaxx::input;

    NullBackend backend;
    InputContext ctx;
    ctx.setBackend(&backend);

    // Frame 0 — three move events accumulate into a single delta sum, and
    // the absolute position reflects the LAST event.
    backend.push(MouseMoveEvent{10.0f, 20.0f, 5.0f, 5.0f});
    backend.push(MouseMoveEvent{12.0f, 22.0f, 2.0f, 2.0f});
    backend.push(MouseMoveEvent{15.0f, 25.0f, 3.0f, 3.0f});
    ctx.beginFrame(1.0f / 60.0f);
    CHECK_EQ(ctx.state().mouse.x, 15.0f);
    CHECK_EQ(ctx.state().mouse.y, 25.0f);
    CHECK_EQ(ctx.state().mouse.dx, 10.0f);
    CHECK_EQ(ctx.state().mouse.dy, 10.0f);
    ctx.endFrame();

    // Frame 1 — no events. dx/dy MUST reset; absolute position stays.
    ctx.beginFrame(1.0f / 60.0f);
    CHECK_EQ(ctx.state().mouse.x, 15.0f);
    CHECK_EQ(ctx.state().mouse.y, 25.0f);
    CHECK_EQ(ctx.state().mouse.dx, 0.0f);
    CHECK_EQ(ctx.state().mouse.dy, 0.0f);
    ctx.endFrame();

    // Frame 2 — wheel deltas accumulate the same way.
    backend.push(MouseWheelEvent{0.5f, 1.0f});
    backend.push(MouseWheelEvent{0.25f, -0.5f});
    ctx.beginFrame(1.0f / 60.0f);
    CHECK_EQ(ctx.state().mouse.wheelDx, 0.75f);
    CHECK_EQ(ctx.state().mouse.wheelDy, 0.5f);
    ctx.endFrame();

    // Frame 3 — wheel deltas also reset each frame.
    ctx.beginFrame(1.0f / 60.0f);
    CHECK_EQ(ctx.state().mouse.wheelDx, 0.0f);
    CHECK_EQ(ctx.state().mouse.wheelDy, 0.0f);
    ctx.endFrame();

    EXIT_WITH_RESULT();
}
