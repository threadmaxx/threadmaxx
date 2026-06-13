/// @file test_studio_input_panel_records_trace.cpp
/// @brief ST17 — InputPanel surfaces InputState header + mouse row;
/// `recordCurrentFrame` appends a frame to the bound `InputTrace`;
/// `replayFrame` pushes events into the bound `NullBackend`.

#include "Check.hpp"

#include <threadmaxx_editor/backends/headless.hpp>
#include <threadmaxx_studio/panels/input.hpp>

#include <threadmaxx_input/backends/NullBackend.hpp>
#include <threadmaxx_input/context.hpp>
#include <threadmaxx_input/events.hpp>
#include <threadmaxx_input/trace.hpp>
#include <threadmaxx_input/types.hpp>

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
    studio::InputPanel panel;

    editor::HeadlessBackend backend;
    backend.initialize();
    NullSource source;

    // Detached.
    backend.beginFrame();
    panel.render(backend, source);
    backend.endFrame();
    CHECK_EQ(panel.rowCount(), 1u);

    // Wire a context with an event the panel can see.
    input::NullBackend inBackend;
    input::InputContext ctx;
    ctx.setBackend(&inBackend);
    inBackend.push(input::KeyEvent{input::Key::Space, input::Modifiers::None, true});
    ctx.beginFrame(1.0f / 60.0f);
    panel.setContext(&ctx);

    backend.beginFrame();
    panel.render(backend, source);
    backend.endFrame();
    CHECK_EQ(panel.rowCount(), 2u); // header + mouse
    CHECK_EQ(countTextOps(backend.capturedFrame()), 2u);

    // Bind an InputTrace; recordCurrentFrame appends a frame.
    input::InputTrace trace;
    panel.setTrace(&trace);
    CHECK(panel.recordCurrentFrame());
    CHECK_EQ(trace.frameCount(), 1u);

    // Render now shows a 3rd row (trace summary).
    backend.beginFrame();
    panel.render(backend, source);
    backend.endFrame();
    CHECK_EQ(panel.rowCount(), 3u);

    // Replay without a backend bound → reject.
    CHECK(!panel.replayFrame(0));

    // Bind a replay backend → replay frame 0 succeeds, OOB rejected.
    input::NullBackend replayBackend;
    panel.setReplayBackend(&replayBackend);
    CHECK(panel.replayFrame(0));
    CHECK(!panel.replayFrame(99));

    // Detached recordCurrentFrame rejected.
    panel.setContext(nullptr);
    CHECK(!panel.recordCurrentFrame());

    ctx.endFrame();
    backend.shutdown();
    EXIT_WITH_RESULT();
}
