/// @file test_input_binding_single_source.cpp
/// @brief Single-source binding fires pressed once on the down-event
/// frame, stays held until release, fires released on the up-event frame.

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
    ctx.setBindings(bs);

    // Frame 0 — quiet.
    ctx.beginFrame(1.0f / 60.0f);
    {
        const auto t = ctx.action("Jump");
        CHECK(!t.held);
        CHECK(!t.pressed);
        CHECK(!t.released);
        CHECK_EQ(t.value, 0.0f);
    }
    ctx.endFrame();

    // Frame 1 — Space down.
    backend.push(KeyEvent{Key::Space, Modifiers::None, true});
    ctx.beginFrame(1.0f / 60.0f);
    {
        const auto t = ctx.action("Jump");
        CHECK(t.held);
        CHECK(t.pressed);
        CHECK(!t.released);
        CHECK_EQ(t.value, 1.0f);
    }
    ctx.endFrame();

    // Frame 2 — still held; press edge clears.
    ctx.beginFrame(1.0f / 60.0f);
    {
        const auto t = ctx.action("Jump");
        CHECK(t.held);
        CHECK(!t.pressed);
        CHECK(!t.released);
    }
    ctx.endFrame();

    // Frame 3 — release.
    backend.push(KeyEvent{Key::Space, Modifiers::None, false});
    ctx.beginFrame(1.0f / 60.0f);
    {
        const auto t = ctx.action("Jump");
        CHECK(!t.held);
        CHECK(!t.pressed);
        CHECK(t.released);
    }
    ctx.endFrame();

    // Frame 4 — release edge clears.
    ctx.beginFrame(1.0f / 60.0f);
    {
        const auto t = ctx.action("Jump");
        CHECK(!t.held);
        CHECK(!t.released);
    }
    ctx.endFrame();

    // Unknown action returns idle.
    const auto unknown = ctx.action("NeverBound");
    CHECK(!unknown.held);
    CHECK(!unknown.pressed);
    CHECK(!unknown.released);

    EXIT_WITH_RESULT();
}
