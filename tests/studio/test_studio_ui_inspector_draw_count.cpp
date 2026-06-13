/// @file test_studio_ui_inspector_draw_count.cpp
/// @brief ST19 — bind a UIContext, emit a mix of draw commands,
/// `lastDrawCount()` reports the total and the panel renders a
/// header + per-kind tally row.

#include "Check.hpp"

#include <threadmaxx_editor/backends/headless.hpp>
#include <threadmaxx_studio/panels/ui_inspector.hpp>

#include <threadmaxx_ui/backends/NullBackend.hpp>
#include <threadmaxx_ui/context.hpp>
#include <threadmaxx_ui/draw.hpp>

namespace {

std::size_t countTextOps(
    const threadmaxx::editor::CapturedFrame& frame) {
    std::size_t n = 0;
    for (const auto& op : frame.ops) {
        if (op.op == threadmaxx::editor::CapturedOp::Op::DrawText) ++n;
    }
    return n;
}

struct NullSource : threadmaxx::studio::IStudioDataSource {
    threadmaxx::studio::AttachMode mode() const noexcept override {
        return threadmaxx::studio::AttachMode::Direct;
    }
};

} // namespace

int main() {
    using namespace threadmaxx;
    ui::NullBackend uiBackend;
    ui::UIContext ctx;
    ctx.setBackend(&uiBackend);

    ctx.beginFrame();
    ctx.drawList().emitRect(ui::Rect{0, 0, 10, 10}, ui::Color{0, 0, 0, 255});
    ctx.drawList().emitRect(ui::Rect{5, 5, 1, 1},   ui::Color{0, 0, 0, 255});
    ctx.drawList().emitLine(ui::Vec2i{0, 0}, ui::Vec2i{5, 5},
                            ui::Color{0, 0, 0, 255});
    ctx.drawList().emitText(ui::Vec2i{2, 2}, ui::Color{0, 0, 0, 255}, "hi");
    ctx.endFrame();
    // After endFrame, the drawList persists until next beginFrame.

    studio::UIInspectorPanel panel{ctx};
    editor::HeadlessBackend backend;
    backend.initialize();
    NullSource source;

    backend.beginFrame();
    panel.render(backend, source);
    backend.endFrame();
    CHECK(!panel.lastWasStub());
    CHECK_EQ(panel.lastDrawCount(), 4u); // 2 rects + 1 line + 1 text
    CHECK_EQ(countTextOps(backend.capturedFrame()), 2u);

    backend.shutdown();
    EXIT_WITH_RESULT();
}
