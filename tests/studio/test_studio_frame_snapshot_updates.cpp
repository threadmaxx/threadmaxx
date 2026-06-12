/// @file test_studio_frame_snapshot_updates.cpp
/// @brief ST10 — FrameSnapshotPanel doubles as an ITraceSink. Wire
/// it to the engine; step N times → sampleCount = N; latestTick
/// tracks engine.tick; render produces an FPS row.

#include "Check.hpp"
#include "studio/StudioTestFixture.hpp"

#include <threadmaxx_editor/backends/headless.hpp>
#include <threadmaxx_studio/panels/frame_snapshot.hpp>

namespace {

struct NullSource : threadmaxx::studio::IStudioDataSource {
    threadmaxx::studio::AttachMode mode() const noexcept override {
        return threadmaxx::studio::AttachMode::Direct;
    }
};

} // namespace

int main() {
    threadmaxx::studio::test::ScopedSession env{};
    threadmaxx::studio::FrameSnapshotPanel panel{16};

    env.engine().setTraceSink(&panel);

    CHECK_EQ(panel.sampleCount(), 0u);
    CHECK_EQ(panel.latestTick(), 0u);
    CHECK_EQ(panel.latestFps(), 0);

    for (int i = 0; i < 10; ++i) {
        env.engine().step();
    }
    env.engine().setTraceSink(nullptr);

    CHECK_EQ(panel.sampleCount(), 10u);
    CHECK_EQ(panel.latestTick(), env.engine().tick());

    threadmaxx::editor::HeadlessBackend backend;
    CHECK(backend.initialize());
    NullSource src;
    backend.beginFrame();
    panel.render(backend, src);
    backend.endFrame();

    // Top row contains "FPS"; at least one histogram row exists too.
    bool fpsRow = false;
    bool binRow = false;
    for (const auto& op : backend.capturedFrame().ops) {
        if (op.op == threadmaxx::editor::CapturedOp::Op::DrawText) {
            if (op.text.find("FPS") != std::string::npos) fpsRow = true;
            if (op.text.find("bin ") != std::string::npos) binRow = true;
        }
    }
    CHECK(fpsRow);
    CHECK(binRow);

    // Ring buffer caps at capacity; further steps don't grow past 16.
    env.engine().setTraceSink(&panel);
    for (int i = 0; i < 20; ++i) {
        env.engine().step();
    }
    env.engine().setTraceSink(nullptr);
    CHECK_EQ(panel.sampleCount(), 16u);

    backend.shutdown();
    EXIT_WITH_RESULT();
}
