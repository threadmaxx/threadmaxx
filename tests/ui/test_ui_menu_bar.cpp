/// @file test_ui_menu_bar.cpp

#include "Check.hpp"
#include "threadmaxx_ui/context.hpp"
#include "threadmaxx_ui/input.hpp"
#include "threadmaxx_ui/menu.hpp"

int main() {
    using namespace threadmaxx::ui;

    UIContext ctx;
    const WidgetID fileMenu{0xF101};
    const WidgetID editMenu{0xF102};
    const Rect barBounds{0, 0, 200, 20};
    const Rect fileBtn{0, 0, 50, 20};
    const Rect editBtn{60, 0, 50, 20};

    auto runFrame = [&](const UIInput& in) {
        ctx.setInput(in);
        ctx.beginFrame();
        beginMenuBar(ctx, barBounds);
        if (beginMenu(ctx, fileMenu, fileBtn, "File")) endMenu(ctx);
        if (beginMenu(ctx, editMenu, editBtn, "Edit")) endMenu(ctx);
        endMenuBar(ctx);
        ctx.endFrame();
    };

    // Click File -> File popup open.
    UIInput press;
    press.mousePos = Vec2i{10, 5};
    press.mouseButtons = MouseButton::Left;
    press.mouseButtonsPressed = MouseButton::Left;
    runFrame(press);
    UIInput rel;
    rel.mousePos = Vec2i{10, 5};
    rel.mouseButtonsReleased = MouseButton::Left;
    runFrame(rel);
    CHECK(isPopupOpen(ctx, fileMenu));
    CHECK(ctx.menuBarActive());

    // Hovering Edit while bar is active switches the open menu to Edit.
    UIInput hover;
    hover.mousePos = Vec2i{70, 5};
    runFrame(hover);
    CHECK(isPopupOpen(ctx, editMenu));
    CHECK(!isPopupOpen(ctx, fileMenu));

    // Click somewhere outside the bar -> popup closes.
    UIInput pressOutside;
    pressOutside.mousePos = Vec2i{300, 200};
    pressOutside.mouseButtons = MouseButton::Left;
    pressOutside.mouseButtonsPressed = MouseButton::Left;
    runFrame(pressOutside);
    CHECK(!isPopupOpen(ctx, fileMenu));
    CHECK(!isPopupOpen(ctx, editMenu));
    CHECK(!ctx.menuBarActive());

    EXIT_WITH_RESULT();
}
