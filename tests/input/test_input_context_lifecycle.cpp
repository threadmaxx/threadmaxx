/// @file test_input_context_lifecycle.cpp
/// @brief Pins the per-frame loop: beginFrame drains the backend and
/// derives edges; endFrame advances the frame index; previous-state
/// snapshot drives the press/release rising/falling edges.

#include "Check.hpp"

#include "threadmaxx_input/backends/NullBackend.hpp"
#include "threadmaxx_input/context.hpp"

int main() {
    using namespace threadmaxx::input;

    NullBackend backend;
    InputContext ctx;
    ctx.setBackend(&backend);

    // Initial state: no frame open, idle counters.
    CHECK(!ctx.frameOpen());
    CHECK_EQ(ctx.frameIndex(), std::uint64_t{0});
    CHECK_EQ(ctx.lastFrameEventCount(), std::size_t{0});

    // Frame 0 — Space goes down.
    backend.push(KeyEvent{Key::Space, Modifiers::None, true});
    ctx.beginFrame(1.0f / 60.0f);
    CHECK(ctx.frameOpen());
    CHECK_EQ(ctx.lastFrameEventCount(), std::size_t{1});
    CHECK(ctx.isHeld(Key::Space));
    CHECK(ctx.wasPressed(Key::Space));
    CHECK(!ctx.wasReleased(Key::Space));
    ctx.endFrame();
    CHECK(!ctx.frameOpen());
    CHECK_EQ(ctx.frameIndex(), std::uint64_t{1});

    // Frame 1 — no events. Space is still held; press edge should clear.
    ctx.beginFrame(1.0f / 60.0f);
    CHECK(ctx.isHeld(Key::Space));
    CHECK(!ctx.wasPressed(Key::Space));
    CHECK(!ctx.wasReleased(Key::Space));
    CHECK_EQ(ctx.lastFrameEventCount(), std::size_t{0});
    ctx.endFrame();

    // Frame 2 — Space released.
    backend.push(KeyEvent{Key::Space, Modifiers::None, false});
    ctx.beginFrame(1.0f / 60.0f);
    CHECK(!ctx.isHeld(Key::Space));
    CHECK(!ctx.wasPressed(Key::Space));
    CHECK(ctx.wasReleased(Key::Space));
    ctx.endFrame();

    // Frame 3 — release edge should clear.
    ctx.beginFrame(1.0f / 60.0f);
    CHECK(!ctx.isHeld(Key::Space));
    CHECK(!ctx.wasReleased(Key::Space));
    ctx.endFrame();

    // Simulation time accumulates across frames.
    CHECK(ctx.simulationTime() > 0.06);  // 4 frames @ 1/60
    CHECK_EQ(ctx.frameIndex(), std::uint64_t{4});

    EXIT_WITH_RESULT();
}
