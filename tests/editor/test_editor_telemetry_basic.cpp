/// @file test_editor_telemetry_basic.cpp
/// @brief E5 — overlay enabled; advance the engine; backend's
/// captured frame contains "FPS:" + "Frame:" text ops.

#include "Check.hpp"
#include "EditorTestFixture.hpp"

#include <threadmaxx_editor/backends/headless.hpp>
#include <threadmaxx_editor/telemetry.hpp>

#include <string>

namespace {

bool startsWith(const std::string& s, const char* prefix) {
    const std::size_t n = std::strlen(prefix);
    return s.size() >= n && s.compare(0, n, prefix) == 0;
}

} // namespace

int main() {
    threadmaxx::editor::test::ScopedEngine env;
    env.engine().step();

    threadmaxx::editor::HeadlessBackend back;
    CHECK(back.initialize());

    threadmaxx::editor::OverlayConfig cfg{};
    cfg.showSystemStats = false; // keep the assertion narrow
    threadmaxx::editor::TelemetryOverlay overlay{env.engine(), cfg};
    overlay.render(back);

    const auto& frame = back.capturedFrame();
    CHECK(!frame.empty());
    bool sawFPS = false, sawFrame = false;
    for (const auto& op : frame.ops) {
        if (op.op == threadmaxx::editor::CapturedOp::Op::DrawText) {
            if (startsWith(op.text, "FPS:"))   sawFPS = true;
            if (startsWith(op.text, "Frame:")) sawFrame = true;
        }
    }
    CHECK(sawFPS);
    CHECK(sawFrame);

    back.shutdown();
    EXIT_WITH_RESULT();
}
