/// @file test_input_gamepad_action_binding.cpp
/// @brief Bindings against a gamepad button + axis behave identically to
/// keyboard bindings — same press/release/value semantics.

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
    bs.bind("Jump", Binding::gamepadButton(GamepadButton::A));
    bs.bind("Forward", Binding::gamepadAxisPositive(GamepadAxis::LStickY, 0.4f));
    ctx.setBindings(bs);

    // Frame 0 — quiet.
    ctx.beginFrame(1.0f / 60.0f);
    CHECK(!ctx.action("Jump").held);
    CHECK(!ctx.action("Forward").held);
    ctx.endFrame();

    // Frame 1 — A down + axis at 0.8.
    backend.push(GamepadButtonEvent{kGamepad0DeviceId, GamepadButton::A, true});
    backend.push(GamepadAxisEvent{kGamepad0DeviceId, GamepadAxis::LStickY, 0.8f});
    ctx.beginFrame(1.0f / 60.0f);
    {
        const auto jump = ctx.action("Jump");
        CHECK(jump.held);
        CHECK(jump.pressed);
        CHECK_EQ(jump.value, 1.0f);

        const auto fwd = ctx.action("Forward");
        CHECK(fwd.held);
        CHECK(fwd.pressed);
        CHECK(fwd.value > 0.0f);
        CHECK(fwd.value <= 1.0f);
    }
    ctx.endFrame();

    // Frame 2 — axis drops into the deadband (below threshold). Forward
    // releases.
    backend.push(GamepadAxisEvent{kGamepad0DeviceId, GamepadAxis::LStickY, 0.2f});
    ctx.beginFrame(1.0f / 60.0f);
    {
        const auto fwd = ctx.action("Forward");
        CHECK(!fwd.held);
        CHECK(fwd.released);
        CHECK_EQ(fwd.value, 0.0f);

        // Jump still held since A still down.
        CHECK(ctx.action("Jump").held);
    }
    ctx.endFrame();

    // Frame 3 — negative axis direction should NOT trigger positive-bound
    // Forward action.
    backend.push(GamepadAxisEvent{kGamepad0DeviceId, GamepadAxis::LStickY, -0.8f});
    ctx.beginFrame(1.0f / 60.0f);
    CHECK(!ctx.action("Forward").held);
    ctx.endFrame();

    EXIT_WITH_RESULT();
}
