/// @file test_ui_debug_hud.cpp

#include "Check.hpp"
#include "threadmaxx_ui/context.hpp"
#include "threadmaxx_ui/debug.hpp"
#include "threadmaxx_ui/draw.hpp"
#include "threadmaxx_ui/input.hpp"

int main() {
    using namespace threadmaxx::ui;

    UIContext ctx;
    UIInput none;
    ctx.setInput(none);

    ctx.beginFrame();
    debug::beginHud(ctx);
    debug::row(ctx, "FPS: 144");
    debug::row(ctx, "Frame: 1234");
    debug::kvInt(ctx, "count", 42);
    debug::kvFloat(ctx, "dt", 0.0166f);
    debug::kv(ctx, "build", "Release");
    ctx.endFrame();

    // Five text draws + zero rects (no panels here).
    int textCount = 0;
    for (const auto& c : ctx.drawList().commands()) {
        if (c.kind == DrawCmdKind::Text) ++textCount;
    }
    CHECK_EQ(textCount, 5);

    // Frame 2: rebuild from scratch — no carryover from frame 1.
    ctx.beginFrame();
    debug::beginHud(ctx);
    debug::row(ctx, "only");
    ctx.endFrame();
    textCount = 0;
    for (const auto& c : ctx.drawList().commands()) {
        if (c.kind == DrawCmdKind::Text) ++textCount;
    }
    CHECK_EQ(textCount, 1);

    EXIT_WITH_RESULT();
}
