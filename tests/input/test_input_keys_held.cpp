/// @file test_input_keys_held.cpp
/// @brief Pins the level + press-edge contract for keys. wasPressed fires
/// on the frame the down-event arrives and clears the next frame, while
/// isHeld stays sticky.

#include "Check.hpp"

#include "threadmaxx_input/backends/NullBackend.hpp"
#include "threadmaxx_input/context.hpp"

int main() {
    using namespace threadmaxx::input;

    NullBackend backend;
    InputContext ctx;
    ctx.setBackend(&backend);

    // Frame 0 — three keys go down within the same frame.
    backend.push(KeyEvent{Key::A, Modifiers::None, true});
    backend.push(KeyEvent{Key::W, Modifiers::None, true});
    backend.push(KeyEvent{Key::F1, Modifiers::None, true});
    ctx.beginFrame(1.0f / 60.0f);

    CHECK(ctx.isHeld(Key::A));
    CHECK(ctx.isHeld(Key::W));
    CHECK(ctx.isHeld(Key::F1));
    CHECK(ctx.wasPressed(Key::A));
    CHECK(ctx.wasPressed(Key::W));
    CHECK(ctx.wasPressed(Key::F1));
    CHECK(!ctx.isHeld(Key::B));  // unrelated key stays clear
    CHECK(!ctx.wasPressed(Key::B));

    ctx.endFrame();

    // Frame 1 — no events. isHeld remains sticky; wasPressed clears.
    ctx.beginFrame(1.0f / 60.0f);
    CHECK(ctx.isHeld(Key::A));
    CHECK(ctx.isHeld(Key::W));
    CHECK(!ctx.wasPressed(Key::A));
    CHECK(!ctx.wasPressed(Key::W));
    ctx.endFrame();

    // Frame 2 — another key joins; held keys still held; new key edge fires.
    backend.push(KeyEvent{Key::Space, Modifiers::None, true});
    ctx.beginFrame(1.0f / 60.0f);
    CHECK(ctx.isHeld(Key::A));
    CHECK(ctx.isHeld(Key::Space));
    CHECK(ctx.wasPressed(Key::Space));
    CHECK(!ctx.wasPressed(Key::A));  // still no new edge for sticky A
    ctx.endFrame();

    EXIT_WITH_RESULT();
}
