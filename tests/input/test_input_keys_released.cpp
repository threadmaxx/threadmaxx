/// @file test_input_keys_released.cpp
/// @brief Pins the release-edge contract for keys. wasReleased fires on
/// the frame the up-event arrives and clears the next frame; isHeld goes
/// false the same frame.

#include "Check.hpp"

#include "threadmaxx_input/backends/NullBackend.hpp"
#include "threadmaxx_input/context.hpp"

int main() {
    using namespace threadmaxx::input;

    NullBackend backend;
    InputContext ctx;
    ctx.setBackend(&backend);

    // Hold A and W for two frames first.
    backend.push(KeyEvent{Key::A, Modifiers::None, true});
    backend.push(KeyEvent{Key::W, Modifiers::None, true});
    ctx.beginFrame(1.0f / 60.0f);
    ctx.endFrame();
    ctx.beginFrame(1.0f / 60.0f);
    ctx.endFrame();

    // Frame: release A only.
    backend.push(KeyEvent{Key::A, Modifiers::None, false});
    ctx.beginFrame(1.0f / 60.0f);
    CHECK(!ctx.isHeld(Key::A));
    CHECK(ctx.isHeld(Key::W));
    CHECK(ctx.wasReleased(Key::A));
    CHECK(!ctx.wasReleased(Key::W));
    CHECK(!ctx.wasPressed(Key::A));
    ctx.endFrame();

    // Next frame: release edge clears even though A is still up.
    ctx.beginFrame(1.0f / 60.0f);
    CHECK(!ctx.isHeld(Key::A));
    CHECK(!ctx.wasReleased(Key::A));
    ctx.endFrame();

    // Press + release in the same frame: the level state ends as released,
    // BUT both edges should NOT fire (the down was immediately undone).
    // This is the deterministic XOR semantics — same start, same end → no
    // diff. Documenting the choice.
    backend.push(KeyEvent{Key::Tab, Modifiers::None, true});
    backend.push(KeyEvent{Key::Tab, Modifiers::None, false});
    ctx.beginFrame(1.0f / 60.0f);
    CHECK(!ctx.isHeld(Key::Tab));
    CHECK(!ctx.wasPressed(Key::Tab));
    CHECK(!ctx.wasReleased(Key::Tab));
    ctx.endFrame();

    EXIT_WITH_RESULT();
}
