/// @file test_input_modifier_exact_match.cpp
/// @brief Modifier mask is exact — Ctrl+S does NOT fire on Shift+Ctrl+S.

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
    bs.bind("Save", Binding::key(Key::S, Modifiers::Ctrl));
    ctx.setBindings(bs);

    // Frame 0 — quiet.
    ctx.beginFrame(1.0f / 60.0f);
    CHECK(!ctx.action("Save").held);
    ctx.endFrame();

    // Frame 1 — Ctrl held, but no S yet.
    backend.push(KeyEvent{Key::LCtrl, Modifiers::Ctrl, true});
    ctx.beginFrame(1.0f / 60.0f);
    CHECK(!ctx.action("Save").held);
    ctx.endFrame();

    // Frame 2 — Ctrl+S. Save fires.
    backend.push(KeyEvent{Key::S, Modifiers::Ctrl, true});
    ctx.beginFrame(1.0f / 60.0f);
    {
        const auto t = ctx.action("Save");
        CHECK(t.held);
        CHECK(t.pressed);
    }
    ctx.endFrame();

    // Frame 3 — Shift added. Now Shift+Ctrl+S. Save MUST stop firing.
    backend.push(KeyEvent{Key::LShift, Modifiers::Shift | Modifiers::Ctrl, true});
    ctx.beginFrame(1.0f / 60.0f);
    {
        const auto t = ctx.action("Save");
        CHECK(!t.held);
        CHECK(t.released);  // transitioned from held to not-held
    }
    ctx.endFrame();

    // Frame 4 — release Shift. Back to Ctrl+S (S still held, Ctrl still held).
    // Save fires again.
    backend.push(KeyEvent{Key::LShift, Modifiers::Ctrl, false});
    ctx.beginFrame(1.0f / 60.0f);
    {
        const auto t = ctx.action("Save");
        CHECK(t.held);
        CHECK(t.pressed);
    }
    ctx.endFrame();

    // A binding without modifier requirement should also exact-match
    // (modifier mask == 0 means "no modifiers"). Confirm: bind a plain
    // "Forward" to W; pressing W while Ctrl is held does NOT fire.
    bs.bind("Forward", Binding::key(Key::W));
    ctx.setBindings(bs);

    backend.push(KeyEvent{Key::W, Modifiers::Ctrl, true});
    ctx.beginFrame(1.0f / 60.0f);
    CHECK(!ctx.action("Forward").held);
    ctx.endFrame();

    // Release Ctrl, W still held → Forward fires.
    backend.push(KeyEvent{Key::LCtrl, Modifiers::None, false});
    ctx.beginFrame(1.0f / 60.0f);
    CHECK(ctx.action("Forward").held);
    ctx.endFrame();

    EXIT_WITH_RESULT();
}
