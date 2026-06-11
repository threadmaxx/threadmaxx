/// @file test_ui_inspect_primitives.cpp

#include "Check.hpp"
#include <cstring>
#include "threadmaxx_ui/context.hpp"
#include "threadmaxx_ui/input.hpp"
#include "threadmaxx_ui/inspect.hpp"

int main() {
    using namespace threadmaxx::ui;

    UIContext ctx;
    const Rect row{0, 0, 200, 22};

    auto fullClick = [&](auto drawCb) {
        UIInput press;
        press.mousePos = Vec2i{180, 11};  // inside value cell
        press.mouseButtons = MouseButton::Left;
        press.mouseButtonsPressed = MouseButton::Left;
        ctx.setInput(press);
        ctx.beginFrame();
        drawCb();
        ctx.endFrame();
        UIInput rel;
        rel.mousePos = Vec2i{180, 11};
        rel.mouseButtonsReleased = MouseButton::Left;
        ctx.setInput(rel);
        ctx.beginFrame();
        drawCb();
        ctx.endFrame();
    };

    // bool
    bool b = false;
    fullClick([&]{ inspect(ctx, WidgetID{0xB1}, row, "Enabled", &b); });
    CHECK(b == true);

    // int — drag changes the bound int.
    std::int32_t i = 10;
    UIInput drag;
    drag.mousePos = Vec2i{180, 11};
    drag.mouseDelta = Vec2i{20, 0};
    drag.mouseButtons = MouseButton::Left;
    drag.mouseButtonsPressed = MouseButton::Left;
    ctx.setInput(drag);
    ctx.beginFrame();
    inspect(ctx, WidgetID{0x10}, row, "Count", &i);
    ctx.endFrame();
    CHECK(i != 10);  // value changed (drift in either direction OK)

    // float — drag changes the bound float.
    float f = 0.0f;
    ctx.setInput(drag);
    ctx.beginFrame();
    inspect(ctx, WidgetID{0xF0}, row, "Speed", &f);
    ctx.endFrame();
    CHECK(f != 0.0f);

    // string — type "hi"
    char buf[16] = {};
    // First Tab to focus
    UIInput tab;
    tab.navKeysPressed = NavKey::Tab;
    ctx.setInput(tab);
    ctx.beginFrame();
    inspect(ctx, WidgetID{0x50}, row, "Name", buf, sizeof(buf));
    ctx.endFrame();
    UIInput type;
    type.chars[0] = 'h';
    type.chars[1] = 'i';
    type.charsCount = 2;
    ctx.setInput(type);
    ctx.beginFrame();
    inspect(ctx, WidgetID{0x50}, row, "Name", buf, sizeof(buf));
    ctx.endFrame();
    CHECK_EQ(std::strcmp(buf, "hi"), 0);

    EXIT_WITH_RESULT();
}
