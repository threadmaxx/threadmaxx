/// @file test_ui_inspect_vector.cpp

#include "Check.hpp"
#include "threadmaxx_ui/context.hpp"
#include "threadmaxx_ui/input.hpp"
#include "threadmaxx_ui/inspect.hpp"

int main() {
    using namespace threadmaxx::ui;

    UIContext ctx;
    const Rect row{0, 0, 300, 22};
    float x = 0.0f, y = 0.0f, z = 0.0f;

    // Drag the X component. The value rect starts at x ≈ 124 (0 + 40% of
    // 300 + 4 spacing). Each subfield is ~58 px wide.
    UIInput dragX;
    dragX.mousePos = Vec2i{140, 11};  // inside X cell
    dragX.mouseDelta = Vec2i{5, 0};
    dragX.mouseButtons = MouseButton::Left;
    dragX.mouseButtonsPressed = MouseButton::Left;
    ctx.setInput(dragX);
    ctx.beginFrame();
    inspect(ctx, WidgetID{0xC0FFEE}, row, "pos", &x, &y, &z);
    ctx.endFrame();
    CHECK(x != 0.0f);
    CHECK(y == 0.0f);
    CHECK(z == 0.0f);

    // Drag the Y component (mid).
    UIInput dragY;
    dragY.mousePos = Vec2i{200, 11};
    dragY.mouseDelta = Vec2i{5, 0};
    dragY.mouseButtons = MouseButton::Left;
    dragY.mouseButtonsPressed = MouseButton::Left;
    ctx.setInput(dragY);
    ctx.beginFrame();
    inspect(ctx, WidgetID{0xC0FFEE}, row, "pos", &x, &y, &z);
    ctx.endFrame();
    CHECK(y != 0.0f);
    CHECK(z == 0.0f);

    // Drag Z.
    UIInput dragZ;
    dragZ.mousePos = Vec2i{260, 11};
    dragZ.mouseDelta = Vec2i{5, 0};
    dragZ.mouseButtons = MouseButton::Left;
    dragZ.mouseButtonsPressed = MouseButton::Left;
    ctx.setInput(dragZ);
    ctx.beginFrame();
    inspect(ctx, WidgetID{0xC0FFEE}, row, "pos", &x, &y, &z);
    ctx.endFrame();
    CHECK(z != 0.0f);

    EXIT_WITH_RESULT();
}
