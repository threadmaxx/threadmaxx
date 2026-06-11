/// @file test_ui_widget_tooltip.cpp

#include "Check.hpp"
#include "threadmaxx_ui/context.hpp"
#include "threadmaxx_ui/input.hpp"
#include "threadmaxx_ui/widget.hpp"

int main() {
    using namespace threadmaxx::ui;

    UIContext ctx;
    const WidgetID host{0x88};
    const Rect r{20, 20, 100, 30};

    // Frame 1: hovered, but only 0.1 s of dwell -> no tooltip.
    UIInput in;
    in.mousePos = Vec2i{40, 25};
    in.deltaTimeSeconds = 0.1f;
    ctx.setInput(in);
    ctx.beginFrame();
    interact(ctx, host, r);
    CHECK(!tooltip(ctx, host, r, "Hi"));
    ctx.endFrame();

    // Frames 2..6: keep hovering until total dwell > 0.5 s.
    UIInput hold = in;
    hold.deltaTimeSeconds = 0.12f;
    bool fired = false;
    for (int i = 0; i < 5; ++i) {
        ctx.setInput(hold);
        ctx.beginFrame();
        interact(ctx, host, r);
        if (tooltip(ctx, host, r, "Hi")) fired = true;
        ctx.endFrame();
    }
    CHECK(fired);

    // Move mouse off — tooltip resets and stops firing.
    UIInput off;
    off.mousePos = Vec2i{500, 500};
    off.deltaTimeSeconds = 0.6f;
    ctx.setInput(off);
    ctx.beginFrame();
    interact(ctx, host, r);
    CHECK(!tooltip(ctx, host, r, "Hi"));
    ctx.endFrame();

    // Returning to the host requires a fresh dwell period.
    UIInput retry;
    retry.mousePos = Vec2i{40, 25};
    retry.deltaTimeSeconds = 0.1f;
    ctx.setInput(retry);
    ctx.beginFrame();
    interact(ctx, host, r);
    CHECK(!tooltip(ctx, host, r, "Hi"));
    ctx.endFrame();

    EXIT_WITH_RESULT();
}
