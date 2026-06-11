/// @file test_ui_tree_keyboard_nav.cpp
/// @brief Left collapses, Right expands; Up/Down focus walk skips collapsed
/// subtrees (because the caller doesn't emit those nodes).

#include "Check.hpp"
#include "threadmaxx_ui/context.hpp"
#include "threadmaxx_ui/input.hpp"
#include "threadmaxx_ui/tree.hpp"

int main() {
    using namespace threadmaxx::ui;

    UIContext ctx;
    const WidgetID parent{0xF010};
    const WidgetID childA{0xF011};
    const WidgetID childB{0xF012};

    setTreeOpen(ctx, parent, true);

    auto runFrame = [&](const UIInput& in) {
        ctx.setInput(in);
        ctx.beginFrame();
        const bool parentOpen = treeNodeBegin(ctx, parent, Rect{0, 0, 200, 20}, "Parent");
        if (parentOpen) {
            treeNodeBegin(ctx, childA, Rect{20, 22, 180, 20}, "A");
            treeNodeEnd(ctx);
            treeNodeBegin(ctx, childB, Rect{20, 44, 180, 20}, "B");
            treeNodeEnd(ctx);
            treeNodeEnd(ctx);
        }
        ctx.endFrame();
    };

    // Down arrow walks parent -> A -> B (focus cycle includes all visible
    // focusables).
    UIInput downArrow;
    downArrow.navKeysPressed = NavKey::Down;
    runFrame(downArrow);
    CHECK_EQ(focusedId(ctx), parent);
    runFrame(downArrow);
    CHECK_EQ(focusedId(ctx), childA);
    runFrame(downArrow);
    CHECK_EQ(focusedId(ctx), childB);

    // Up arrow walks back.
    UIInput upArrow;
    upArrow.navKeysPressed = NavKey::Up;
    runFrame(upArrow);
    CHECK_EQ(focusedId(ctx), childA);

    // Left collapses the focused parent's open state. Walk focus back to
    // parent first.
    runFrame(upArrow);
    CHECK_EQ(focusedId(ctx), parent);
    UIInput leftArrow;
    leftArrow.navKeysPressed = NavKey::Left;
    runFrame(leftArrow);
    CHECK(!isTreeOpen(ctx, parent));

    // After collapsing, only parent is in the focus cycle — A and B are no
    // longer emitted. Down arrow stays on parent (only one focusable).
    UIInput noKey;
    runFrame(noKey);  // re-register without keys
    runFrame(downArrow);
    CHECK_EQ(focusedId(ctx), parent);
    runFrame(downArrow);
    CHECK_EQ(focusedId(ctx), parent);

    // Right re-opens parent.
    UIInput rightArrow;
    rightArrow.navKeysPressed = NavKey::Right;
    runFrame(rightArrow);
    CHECK(isTreeOpen(ctx, parent));

    EXIT_WITH_RESULT();
}
