/// @file test_ui_panel_resize.cpp

#include "Check.hpp"
#include "threadmaxx_ui/context.hpp"
#include "threadmaxx_ui/input.hpp"
#include "threadmaxx_ui/panel.hpp"

int main() {
    using namespace threadmaxx::ui;

    UIContext ctx;
    setHostRect(ctx, Rect{0, 0, 800, 600});
    PanelState ps;
    ps.bounds = Rect{100, 100, 200, 150};
    ps.minSize = Vec2i{80, 60};
    const WidgetID id{0x9002};

    auto runFrame = [&](const UIInput& in) {
        ctx.setInput(in);
        ctx.beginFrame();
        if (beginPanel(ctx, id, "Panel", ps)) endPanel(ctx);
        ctx.endFrame();
    };

    // Press inside the resize handle (bottom-right corner of bounds).
    const std::int32_t rhX = ps.bounds.x + ps.bounds.w - 6;  // inside the 12-px handle
    const std::int32_t rhY = ps.bounds.y + ps.bounds.h - 6;
    UIInput press;
    press.mousePos = Vec2i{rhX, rhY};
    press.mouseButtons = MouseButton::Left;
    press.mouseButtonsPressed = MouseButton::Left;
    runFrame(press);

    UIInput drag;
    drag.mousePos = Vec2i{rhX + 40, rhY + 30};
    drag.mouseDelta = Vec2i{40, 30};
    drag.mouseButtons = MouseButton::Left;
    runFrame(drag);
    CHECK_EQ(ps.bounds.w, 240);
    CHECK_EQ(ps.bounds.h, 180);

    // Drag inward past minimum -> clamps to minSize.
    UIInput shrink;
    shrink.mousePos = Vec2i{rhX, rhY};
    shrink.mouseDelta = Vec2i{-500, -500};
    shrink.mouseButtons = MouseButton::Left;
    runFrame(shrink);
    CHECK_EQ(ps.bounds.w, ps.minSize.x);
    CHECK_EQ(ps.bounds.h, ps.minSize.y);

    EXIT_WITH_RESULT();
}
