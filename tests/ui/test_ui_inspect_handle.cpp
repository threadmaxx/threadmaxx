/// @file test_ui_inspect_handle.cpp

#include "Check.hpp"
#include "threadmaxx_ui/context.hpp"
#include "threadmaxx_ui/input.hpp"
#include "threadmaxx_ui/inspect.hpp"

int main() {
    using namespace threadmaxx::ui;

    UIContext ctx;
    const Rect row{0, 0, 200, 22};
    const std::uint64_t handleA = 0xCAFEBABEULL;

    // Read-only handle never returns true.
    UIInput press;
    press.mousePos = Vec2i{180, 11};
    press.mouseButtons = MouseButton::Left;
    press.mouseButtonsPressed = MouseButton::Left;
    ctx.setInput(press);
    ctx.beginFrame();
    CHECK(!inspect(ctx, WidgetID{0xA1}, row, "Entity", handleA));
    ctx.endFrame();

    UIInput rel;
    rel.mousePos = Vec2i{180, 11};
    rel.mouseButtonsReleased = MouseButton::Left;
    ctx.setInput(rel);
    ctx.beginFrame();
    CHECK(!inspect(ctx, WidgetID{0xA1}, row, "Entity", handleA));
    ctx.endFrame();

    EXIT_WITH_RESULT();
}
