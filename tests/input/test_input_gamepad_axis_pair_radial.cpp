/// @file test_input_gamepad_axis_pair_radial.cpp
/// @brief Radial deadzone math for paired-axis sticks: zeros below inner,
/// unit-magnitude at outer, clamped past outer, direction preserved.

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

    // (0.1, 0.1) — magnitude ~ 0.141, BELOW inner deadzone (0.15) → (0, 0).
    backend.push(GamepadAxisEvent{kGamepad0DeviceId, GamepadAxis::LStickX, 0.1f});
    backend.push(GamepadAxisEvent{kGamepad0DeviceId, GamepadAxis::LStickY, 0.1f});
    ctx.beginFrame(1.0f / 60.0f);
    {
        const auto s = ctx.stickXY(kGamepad0DeviceId, Stick::Left);
        CHECK_EQ(s.x, 0.0f);
        CHECK_EQ(s.y, 0.0f);
    }
    ctx.endFrame();

    // Diagonal at exactly outer (0.95 / sqrt(2) ≈ 0.6717 each axis) →
    // magnitude 1, same diagonal direction.
    const float diag = 0.95f / std::sqrt(2.0f);
    backend.push(GamepadAxisEvent{kGamepad0DeviceId, GamepadAxis::LStickX, diag});
    backend.push(GamepadAxisEvent{kGamepad0DeviceId, GamepadAxis::LStickY, diag});
    ctx.beginFrame(1.0f / 60.0f);
    {
        const auto s = ctx.stickXY(kGamepad0DeviceId, Stick::Left);
        const float mag = std::sqrt(s.x * s.x + s.y * s.y);
        CHECK(approx(mag, 1.0f));
        // Direction preserved: equal x, y components.
        CHECK(approx(s.x, s.y));
    }
    ctx.endFrame();

    // Off-diagonal at maximum (1.0, 1.0) — magnitude 1.414, way past outer
    // → clamped to unit length, equal x/y components.
    backend.push(GamepadAxisEvent{kGamepad0DeviceId, GamepadAxis::LStickX, 1.0f});
    backend.push(GamepadAxisEvent{kGamepad0DeviceId, GamepadAxis::LStickY, 1.0f});
    ctx.beginFrame(1.0f / 60.0f);
    {
        const auto s = ctx.stickXY(kGamepad0DeviceId, Stick::Left);
        const float mag = std::sqrt(s.x * s.x + s.y * s.y);
        CHECK(approx(mag, 1.0f));
        CHECK(approx(s.x, s.y));
        CHECK(s.x > 0.0f);
    }
    ctx.endFrame();

    // Direction preserved at a non-cardinal angle. (0.6, 0.0) - in band,
    // post-deadzone vector must still point along +X.
    backend.push(GamepadAxisEvent{kGamepad0DeviceId, GamepadAxis::LStickX, 0.6f});
    backend.push(GamepadAxisEvent{kGamepad0DeviceId, GamepadAxis::LStickY, 0.0f});
    ctx.beginFrame(1.0f / 60.0f);
    {
        const auto s = ctx.stickXY(kGamepad0DeviceId, Stick::Left);
        CHECK(s.x > 0.0f);
        CHECK_EQ(s.y, 0.0f);
        CHECK(s.x < 1.0f);
    }
    ctx.endFrame();

    EXIT_WITH_RESULT();
}
