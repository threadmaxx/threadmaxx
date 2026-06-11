/// @file test_ui_draw_list.cpp
/// @brief Pins the draw-list command stream. Emit a fixed mix of primitives,
/// hand the list to a NullBackend, and verify the backend sees exactly the
/// same command count + text byte count we emitted.

#include "Check.hpp"

#include "threadmaxx_ui/backends/NullBackend.hpp"
#include "threadmaxx_ui/context.hpp"
#include "threadmaxx_ui/draw.hpp"

int main() {
    using namespace threadmaxx::ui;

    NullBackend backend;
    UIContext ctx;
    ctx.setBackend(&backend);

    ctx.beginFrame();
    auto& dl = ctx.drawList();
    dl.emitRect(Rect{0, 0, 100, 100}, Color{255, 0, 0, 255});
    dl.emitRect(Rect{10, 10, 50, 50}, Color{0, 255, 0, 255});
    dl.emitRect(Rect{20, 20, 30, 30}, Color{0, 0, 255, 255});
    dl.emitRect(Rect{40, 40, 10, 10}, Color{255, 255, 0, 255}, /*thickness*/ 2);
    dl.emitLine(Vec2i{0, 0}, Vec2i{100, 100}, Color{255, 255, 255, 255});
    dl.emitLine(Vec2i{100, 0}, Vec2i{0, 100}, Color{200, 200, 200, 255});
    dl.emitClipPush(Rect{0, 0, 50, 50});
    dl.emitText(Vec2i{5, 5}, Color{255, 255, 255, 255}, "hello world");
    dl.emitClipPop();
    ctx.endFrame();

    // 4 rects + 2 lines + clip push + 1 text + clip pop = 9 commands.
    CHECK_EQ(dl.commands().size(), std::size_t{9});
    CHECK_EQ(dl.textBytes().size(), std::size_t{11});
    CHECK_EQ(backend.submitCount(), std::uint64_t{1});
    CHECK_EQ(backend.lastCommands(), std::size_t{9});
    CHECK_EQ(backend.lastTextBytes(), std::size_t{11});

    // Each kind landed in registration order.
    const auto& cmds = dl.commands();
    CHECK(cmds[0].kind == DrawCmdKind::Rect);
    CHECK(cmds[3].kind == DrawCmdKind::Rect);
    CHECK_EQ(cmds[3].payload.rect.thickness, 2);
    CHECK(cmds[4].kind == DrawCmdKind::Line);
    CHECK(cmds[6].kind == DrawCmdKind::ClipPush);
    CHECK(cmds[7].kind == DrawCmdKind::Text);
    CHECK(cmds[8].kind == DrawCmdKind::ClipPop);

    // Text round-trips through the arena.
    const std::string_view round = dl.textOf(cmds[7]);
    CHECK_EQ(round, std::string_view{"hello world"});

    // textOf() of a non-text command returns empty.
    CHECK(dl.textOf(cmds[0]).empty());

    // clear() drops the contents but keeps capacity (no way to assert
    // capacity from a test without poking std::vector internals, so we just
    // verify the size collapses to zero).
    dl.clear();
    CHECK_EQ(dl.commands().size(), std::size_t{0});
    CHECK_EQ(dl.textBytes().size(), std::size_t{0});

    EXIT_WITH_RESULT();
}
