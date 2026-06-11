/// @file test_input_gamepad_axis_deadzone.cpp
/// @brief 1D trigger threshold + 1D stick deadzone behave as advertised:
/// inputs at or below threshold resolve to exactly 0; values above scale
/// monotonically toward 1.

#include "Check.hpp"

#include <cmath>

#include "threadmaxx_input/backends/NullBackend.hpp"
#include "threadmaxx_input/context.hpp"

namespace {
constexpr bool approx(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) <= eps;
}
}  // namespace

int main() {
    using namespace threadmaxx::input;

    NullBackend backend;
    InputContext ctx;
    ctx.setBackend(&backend);

    // Trigger threshold 0.05 by default. 0.05 → exactly 0.
    backend.push(GamepadAxisEvent{kGamepad0DeviceId, GamepadAxis::LTrigger, 0.05f});
    ctx.beginFrame(1.0f / 60.0f);
    CHECK_EQ(ctx.trigger(kGamepad0DeviceId, Trigger::Left), 0.0f);
    ctx.endFrame();

    // 0.5 → ≈ (0.5 - 0.05) / 0.95 = 0.4737
    backend.push(GamepadAxisEvent{kGamepad0DeviceId, GamepadAxis::LTrigger, 0.5f});
    ctx.beginFrame(1.0f / 60.0f);
    const float mid = ctx.trigger(kGamepad0DeviceId, Trigger::Left);
    CHECK(approx(mid, 0.473684f));
    ctx.endFrame();

    // 1.0 → 1.0
    backend.push(GamepadAxisEvent{kGamepad0DeviceId, GamepadAxis::LTrigger, 1.0f});
    ctx.beginFrame(1.0f / 60.0f);
    CHECK_EQ(ctx.trigger(kGamepad0DeviceId, Trigger::Left), 1.0f);
    ctx.endFrame();

    // Stick: an input at (0, inner=0.15) has magnitude inner exactly — at
    // the inner border, still inside, output is (0, 0).
    backend.push(GamepadAxisEvent{kGamepad0DeviceId, GamepadAxis::LStickX, 0.0f});
    backend.push(GamepadAxisEvent{kGamepad0DeviceId, GamepadAxis::LStickY, 0.15f});
    ctx.beginFrame(1.0f / 60.0f);
    {
        const auto s = ctx.stickXY(kGamepad0DeviceId, Stick::Left);
        CHECK_EQ(s.x, 0.0f);
        CHECK_EQ(s.y, 0.0f);
    }
    ctx.endFrame();

    // A 1D input at the outer band (magnitude 0.95) should produce
    // magnitude 1 (outer maps to 1).
    backend.push(GamepadAxisEvent{kGamepad0DeviceId, GamepadAxis::LStickX, 0.95f});
    backend.push(GamepadAxisEvent{kGamepad0DeviceId, GamepadAxis::LStickY, 0.0f});
    ctx.beginFrame(1.0f / 60.0f);
    {
        const auto s = ctx.stickXY(kGamepad0DeviceId, Stick::Left);
        CHECK(approx(s.x, 1.0f));
        CHECK_EQ(s.y, 0.0f);
    }
    ctx.endFrame();

    // Past outer — clamped, still unit magnitude, direction preserved.
    backend.push(GamepadAxisEvent{kGamepad0DeviceId, GamepadAxis::LStickX, 1.0f});
    backend.push(GamepadAxisEvent{kGamepad0DeviceId, GamepadAxis::LStickY, 0.0f});
    ctx.beginFrame(1.0f / 60.0f);
    {
        const auto s = ctx.stickXY(kGamepad0DeviceId, Stick::Left);
        CHECK(approx(s.x, 1.0f));
        CHECK_EQ(s.y, 0.0f);
    }
    ctx.endFrame();

    EXIT_WITH_RESULT();
}
