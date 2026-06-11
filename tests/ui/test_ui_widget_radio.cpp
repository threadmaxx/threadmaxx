/// @file test_ui_widget_radio.cpp

#include "Check.hpp"
#include "threadmaxx_ui/context.hpp"
#include "threadmaxx_ui/input.hpp"
#include "threadmaxx_ui/widget.hpp"

int main() {
    using namespace threadmaxx::ui;

    UIContext ctx;
    const WidgetID rA{0xA001};
    const WidgetID rB{0xB001};
    const WidgetID rC{0xC001};
    std::int32_t selected = 1;  // start on B

    auto pressRelease = [&](const Rect& target) {
        UIInput press;
        press.mousePos = Vec2i{target.x + 1, target.y + 1};
        press.mouseButtons = MouseButton::Left;
        press.mouseButtonsPressed = MouseButton::Left;
        ctx.setInput(press);
        ctx.beginFrame();
        radioOption(ctx, rA, Rect{0,  0, 80, 20}, "A", &selected, 0);
        radioOption(ctx, rB, Rect{0, 25, 80, 20}, "B", &selected, 1);
        radioOption(ctx, rC, Rect{0, 50, 80, 20}, "C", &selected, 2);
        ctx.endFrame();
        UIInput rel;
        rel.mousePos = Vec2i{target.x + 1, target.y + 1};
        rel.mouseButtonsReleased = MouseButton::Left;
        ctx.setInput(rel);
        ctx.beginFrame();
        radioOption(ctx, rA, Rect{0,  0, 80, 20}, "A", &selected, 0);
        radioOption(ctx, rB, Rect{0, 25, 80, 20}, "B", &selected, 1);
        radioOption(ctx, rC, Rect{0, 50, 80, 20}, "C", &selected, 2);
        ctx.endFrame();
    };

    CHECK_EQ(selected, 1);
    pressRelease(Rect{0, 0, 80, 20});
    CHECK_EQ(selected, 0);
    pressRelease(Rect{0, 50, 80, 20});
    CHECK_EQ(selected, 2);
    pressRelease(Rect{0, 25, 80, 20});
    CHECK_EQ(selected, 1);

    EXIT_WITH_RESULT();
}
