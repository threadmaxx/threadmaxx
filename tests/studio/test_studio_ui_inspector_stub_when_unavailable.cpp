/// @file test_studio_ui_inspector_stub_when_unavailable.cpp
/// @brief ST19 — when no UIContext is bound the panel renders the
/// "available in v1.x" placeholder.

#include "Check.hpp"

#include <threadmaxx_editor/backends/headless.hpp>
#include <threadmaxx_studio/panels/ui_inspector.hpp>

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
    studio::UIInspectorPanel panel;
    CHECK(panel.context() == nullptr);

    editor::HeadlessBackend backend;
    backend.initialize();
    NullSource source;

    backend.beginFrame();
    panel.render(backend, source);
    backend.endFrame();
    CHECK(panel.lastWasStub());
    CHECK_EQ(panel.lastDrawCount(), 0u);
    CHECK_EQ(countTextOps(backend.capturedFrame()), 1u);

    backend.shutdown();
    EXIT_WITH_RESULT();
}
