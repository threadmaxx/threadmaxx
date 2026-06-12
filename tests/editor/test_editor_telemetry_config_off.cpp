/// @file test_editor_telemetry_config_off.cpp
/// @brief E5 — showFPS = false removes the FPS line from the captured
/// frame.

#include "Check.hpp"
#include "EditorTestFixture.hpp"

#include <threadmaxx_editor/backends/headless.hpp>
#include <threadmaxx_editor/telemetry.hpp>

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
    cfg.showFPS = false;
    cfg.showSystemStats = false;
    threadmaxx::editor::TelemetryOverlay overlay{env.engine(), cfg};
    overlay.render(back);

    const auto& frame = back.capturedFrame();
    bool sawFPS = false;
    for (const auto& op : frame.ops) {
        if (op.op == threadmaxx::editor::CapturedOp::Op::DrawText &&
            startsWith(op.text, "FPS:")) {
            sawFPS = true;
        }
    }
    CHECK(!sawFPS);

    back.shutdown();
    EXIT_WITH_RESULT();
}
