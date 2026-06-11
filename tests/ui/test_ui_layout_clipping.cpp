/// @file test_ui_layout_clipping.cpp
/// @brief Pins `pushClip` / `popClip` — emits the `ClipPush` / `ClipPop` pair
/// AND intersects with the parent clip so nested clips never escape.

#include "Check.hpp"

#include "threadmaxx_ui/backends/NullBackend.hpp"
#include "threadmaxx_ui/context.hpp"
#include "threadmaxx_ui/layout.hpp"

int main() {
    using namespace threadmaxx::ui;

    NullBackend backend;
    UIContext ctx;
    ctx.setBackend(&backend);
    ctx.beginFrame();

    // Single push/pop — backend sees the rect we requested.
    const Rect outer{0, 0, 100, 100};
    const Rect outerEff = pushClip(ctx, outer);
    CHECK_EQ(outerEff, outer);
    CHECK_EQ(ctx.clipStack().depth(), std::size_t{1});

    // Nested push — intersected with the parent.
    const Rect inner = pushClip(ctx, Rect{50, 50, 100, 100});
    CHECK_EQ(inner, (Rect{50, 50, 50, 50}));
    CHECK_EQ(ctx.clipStack().depth(), std::size_t{2});

    popClip(ctx);
    CHECK_EQ(ctx.clipStack().depth(), std::size_t{1});
    popClip(ctx);
    CHECK_EQ(ctx.clipStack().depth(), std::size_t{0});

    ctx.endFrame();

    // Check the draw stream emitted ClipPush / ClipPush / ClipPop / ClipPop.
    const auto& cmds = ctx.drawList().commands();
    CHECK_EQ(cmds.size(), std::size_t{4});
    CHECK(cmds[0].kind == DrawCmdKind::ClipPush);
    CHECK_EQ(cmds[0].payload.clip.bounds, outer);
    CHECK(cmds[1].kind == DrawCmdKind::ClipPush);
    CHECK_EQ(cmds[1].payload.clip.bounds, (Rect{50, 50, 50, 50}));
    CHECK(cmds[2].kind == DrawCmdKind::ClipPop);
    CHECK(cmds[3].kind == DrawCmdKind::ClipPop);

    EXIT_WITH_RESULT();
}
