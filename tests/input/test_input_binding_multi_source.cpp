/// @file test_input_binding_multi_source.cpp
/// @brief Multi-source bindings OR for held; pressed semantics suppress
/// a second source's down-event when the action is already held by another
/// source (avoids double-fire).

#include "Check.hpp"

#include "threadmaxx_input/backends/NullBackend.hpp"
#include "threadmaxx_input/binding.hpp"
#include "threadmaxx_input/context.hpp"

int main() {
    using namespace threadmaxx::input;

    NullBackend backend;
    InputContext ctx;
    ctx.setBackend(&backend);

    BindingSet bs;
    bs.bind("Jump", Binding::key(Key::Space));
    bs.bind("Jump", Binding::gamepadButton(GamepadButton::A));
    ctx.setBindings(bs);

    // Frame 0 — quiet.
    ctx.beginFrame(1.0f / 60.0f);
    CHECK(!ctx.action("Jump").held);
    ctx.endFrame();

    // Frame 1 — Space down. Pressed fires.
    backend.push(KeyEvent{Key::Space, Modifiers::None, true});
    ctx.beginFrame(1.0f / 60.0f);
    {
        const auto t = ctx.action("Jump");
        CHECK(t.held);
        CHECK(t.pressed);
    }
    ctx.endFrame();

    // Frame 2 — Gamepad A also down WHILE Space still held. Held stays true,
    // pressed must NOT fire again — that's the multi-source de-dup contract.
    backend.push(GamepadButtonEvent{kGamepad0DeviceId, GamepadButton::A, true});
    ctx.beginFrame(1.0f / 60.0f);
    {
        const auto t = ctx.action("Jump");
        CHECK(t.held);
        CHECK(!t.pressed);
    }
    ctx.endFrame();

    // Frame 3 — Release Space. A still held → held stays true, released does
    // NOT fire (the action only releases when ALL sources go up).
    backend.push(KeyEvent{Key::Space, Modifiers::None, false});
    ctx.beginFrame(1.0f / 60.0f);
    {
        const auto t = ctx.action("Jump");
        CHECK(t.held);
        CHECK(!t.released);
    }
    ctx.endFrame();

    // Frame 4 — Release A. Now released fires (no source remains held).
    backend.push(GamepadButtonEvent{kGamepad0DeviceId, GamepadButton::A, false});
    ctx.beginFrame(1.0f / 60.0f);
    {
        const auto t = ctx.action("Jump");
        CHECK(!t.held);
        CHECK(t.released);
    }
    ctx.endFrame();

    // Either source ALONE still drives the action.
    backend.push(GamepadButtonEvent{kGamepad0DeviceId, GamepadButton::A, true});
    ctx.beginFrame(1.0f / 60.0f);
    {
        const auto t = ctx.action("Jump");
        CHECK(t.held);
        CHECK(t.pressed);
    }
    ctx.endFrame();

    EXIT_WITH_RESULT();
}
