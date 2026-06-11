/// @file test_input_ui_bridge_mouse.cpp
/// @brief Mouse state surfaces in the lowered UIInput POD: position,
/// delta, scroll, button state + press/release edges.

#include "Check.hpp"

#include "threadmaxx_input/backends/NullBackend.hpp"
#include "threadmaxx_input/context.hpp"
#include "threadmaxx_input/ui_bridge.hpp"

int main() {
    using namespace threadmaxx::input;

    NullBackend backend;
    InputContext ctx;
    ctx.setBackend(&backend);

    // Frame 1 — establish mouse state.
    backend.push(MouseMoveEvent{300.0f, 200.0f, 10.0f, -5.0f});
    backend.push(MouseButtonEvent{MouseButton::Left, true, 300.0f, 200.0f});
    backend.push(MouseWheelEvent{0.0f, 2.0f});
    ctx.beginFrame(1.0f / 60.0f);

    const auto ui = toUIInput(ctx);
    CHECK_EQ(ui.mousePos.x, 300);
    CHECK_EQ(ui.mousePos.y, 200);
    CHECK_EQ(ui.mouseDelta.x, 10);
    CHECK_EQ(ui.mouseDelta.y, -5);
    CHECK_EQ(ui.scrollY, 2);
    CHECK((ui.mouseButtons & threadmaxx::ui::MouseButton::Left) != 0);
    CHECK((ui.mouseButtonsPressed & threadmaxx::ui::MouseButton::Left) != 0);
    CHECK_EQ(ui.mouseButtonsReleased & threadmaxx::ui::MouseButton::Left, std::uint8_t{0});
    CHECK_EQ(ui.deltaTimeSeconds, 1.0f / 60.0f);

    ctx.endFrame();

    // Frame 2 — release. Released bit fires.
    backend.push(MouseButtonEvent{MouseButton::Left, false, 300.0f, 200.0f});
    ctx.beginFrame(1.0f / 60.0f);
    {
        const auto ui2 = toUIInput(ctx);
        CHECK((ui2.mouseButtonsReleased & threadmaxx::ui::MouseButton::Left) != 0);
        CHECK_EQ(ui2.mouseButtons & threadmaxx::ui::MouseButton::Left, std::uint8_t{0});
    }
    ctx.endFrame();

    EXIT_WITH_RESULT();
}
