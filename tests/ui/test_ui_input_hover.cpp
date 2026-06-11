/// @file test_ui_input_hover.cpp
/// @brief Pins hover resolution — pixel inside a hit-test region is hovered;
/// overlapping regions use last-registered-wins (= topmost in draw order).

#include "Check.hpp"

#include "threadmaxx_ui/context.hpp"
#include "threadmaxx_ui/input.hpp"

int main() {
    using namespace threadmaxx::ui;

    UIContext ctx;
    UIInput input;
    input.mousePos = Vec2i{75, 75};
    ctx.setInput(input);
    ctx.beginFrame();

    const WidgetID idA{0xAAAA};
    const WidgetID idB{0xBBBB};
    const WidgetID idC{0xCCCC};

    // A and C are at (75, 75); B is elsewhere.
    const auto resA = interact(ctx, idA, Rect{50, 50, 50, 50});
    const auto resB = interact(ctx, idB, Rect{0,  0, 30, 30});
    const auto resC = interact(ctx, idC, Rect{60, 60, 40, 40});

    // Last-registered-wins among A/C: C is on top because it registered last.
    CHECK(!isHovered(ctx, idA));
    CHECK(!isHovered(ctx, idB));
    CHECK(isHovered(ctx, idC));
    CHECK_EQ(hoveredId(ctx), idC);

    // The result returned by interact() reflects the LIVE resolution at the
    // time of the call — A was hovered when registered, then C displaced it.
    CHECK(resA.hovered);  // at the time of registration A was hovered
    CHECK(!resB.hovered);
    CHECK(resC.hovered);

    ctx.endFrame();

    // Mouse outside everything — nothing hovered.
    UIInput offscreen;
    offscreen.mousePos = Vec2i{-5, -5};
    ctx.setInput(offscreen);
    ctx.beginFrame();
    interact(ctx, idA, Rect{50, 50, 50, 50});
    interact(ctx, idC, Rect{60, 60, 40, 40});
    CHECK_EQ(hoveredId(ctx), (WidgetID{}));
    ctx.endFrame();

    // NoHover skips a region — its bounds don't count toward hover even when
    // the cursor is inside.
    UIInput insideA;
    insideA.mousePos = Vec2i{55, 55};
    ctx.setInput(insideA);
    ctx.beginFrame();
    interact(ctx, idA, Rect{50, 50, 50, 50}, HitTestFlags::NoHover);
    CHECK(!isHovered(ctx, idA));
    interact(ctx, idC, Rect{50, 50, 50, 50});
    CHECK(isHovered(ctx, idC));
    ctx.endFrame();

    EXIT_WITH_RESULT();
}
