/// @file test_ui_tree_expand.cpp

#include "Check.hpp"
#include "threadmaxx_ui/context.hpp"
#include "threadmaxx_ui/input.hpp"
#include "threadmaxx_ui/tree.hpp"

int main() {
    using namespace threadmaxx::ui;

    UIContext ctx;
    const WidgetID root{0xF001};
    const Rect r{0, 0, 200, 20};

    // Default closed.
    UIInput none;
    ctx.setInput(none);
    ctx.beginFrame();
    CHECK(!treeNodeBegin(ctx, root, r, "Root"));
    ctx.endFrame();
    CHECK(!isTreeOpen(ctx, root));

    // Click the chevron -> opens.
    UIInput press;
    press.mousePos = Vec2i{5, 5};
    press.mouseButtons = MouseButton::Left;
    press.mouseButtonsPressed = MouseButton::Left;
    ctx.setInput(press);
    ctx.beginFrame();
    treeNodeBegin(ctx, root, r, "Root");
    ctx.endFrame();
    UIInput rel;
    rel.mousePos = Vec2i{5, 5};
    rel.mouseButtonsReleased = MouseButton::Left;
    ctx.setInput(rel);
    ctx.beginFrame();
    CHECK(treeNodeBegin(ctx, root, r, "Root"));
    treeNodeEnd(ctx);
    ctx.endFrame();
    CHECK(isTreeOpen(ctx, root));

    // State persists across idle frames.
    ctx.setInput(none);
    ctx.beginFrame();
    CHECK(treeNodeBegin(ctx, root, r, "Root"));
    treeNodeEnd(ctx);
    ctx.endFrame();

    // Click again -> closes.
    ctx.setInput(press);
    ctx.beginFrame();
    treeNodeBegin(ctx, root, r, "Root");
    treeNodeEnd(ctx);
    ctx.endFrame();
    ctx.setInput(rel);
    ctx.beginFrame();
    CHECK(!treeNodeBegin(ctx, root, r, "Root"));
    ctx.endFrame();

    // setTreeOpen programmatic.
    setTreeOpen(ctx, root, true);
    CHECK(isTreeOpen(ctx, root));
    setTreeOpen(ctx, root, false);
    CHECK(!isTreeOpen(ctx, root));

    EXIT_WITH_RESULT();
}
