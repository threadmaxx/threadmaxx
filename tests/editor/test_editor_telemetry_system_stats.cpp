/// @file test_editor_telemetry_system_stats.cpp
/// @brief E5 — showSystemStats = true produces one text op per
/// registered system.

#include "Check.hpp"
#include "EditorTestFixture.hpp"

#include <threadmaxx_editor/backends/headless.hpp>
#include <threadmaxx_editor/telemetry.hpp>

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/System.hpp>

namespace {

struct NamedSystem final : threadmaxx::ISystem {
    const char* n;
    explicit NamedSystem(const char* nn) : n(nn) {}
    const char* name() const noexcept override { return n; }
    void update(threadmaxx::SystemContext&) override {}
};

struct Game final : threadmaxx::IGame {
    void onSetup(threadmaxx::Engine& engine,
                 threadmaxx::World&,
                 threadmaxx::CommandBuffer&) override {
        engine.registerSystem(std::make_unique<NamedSystem>("sys_a"));
        engine.registerSystem(std::make_unique<NamedSystem>("sys_b"));
        engine.registerSystem(std::make_unique<NamedSystem>("sys_c"));
    }
};

} // namespace

int main() {
    threadmaxx::Engine engine{threadmaxx::Config{}};
    Game game;
    CHECK(engine.initialize(game));
    engine.step();

    threadmaxx::editor::HeadlessBackend back;
    CHECK(back.initialize());

    threadmaxx::editor::OverlayConfig cfg{};
    cfg.showFPS = false;
    cfg.showFrameTime = false;
    cfg.showSystemStats = true;
    threadmaxx::editor::TelemetryOverlay overlay{engine, cfg};
    overlay.render(back);

    const auto& frame = back.capturedFrame();
    int matched = 0;
    bool sawA = false, sawB = false, sawC = false;
    for (const auto& op : frame.ops) {
        if (op.op != threadmaxx::editor::CapturedOp::Op::DrawText) continue;
        if (op.text.find("sys_a:") == 0) { sawA = true; ++matched; }
        if (op.text.find("sys_b:") == 0) { sawB = true; ++matched; }
        if (op.text.find("sys_c:") == 0) { sawC = true; ++matched; }
    }
    CHECK_EQ(matched, 3);
    CHECK(sawA);
    CHECK(sawB);
    CHECK(sawC);

    back.shutdown();
    engine.shutdown();
    EXIT_WITH_RESULT();
}
