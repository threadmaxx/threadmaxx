/// @file test_studio_network_desync_log.cpp
/// @brief ST27 — DesyncPanel renders SyncTracker counters + a log
/// of recorded DesyncReports, rolling at capacity.

#include "Check.hpp"

#include <threadmaxx_editor/backends/headless.hpp>
#include <threadmaxx_studio/panels/desync.hpp>

#include <threadmaxx_network/diagnostics.hpp>
#include <threadmaxx_network/ids.hpp>

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
    studio::DesyncPanel panel{3};

    editor::HeadlessBackend backend;
    backend.initialize();
    NullSource source;

    // Detached.
    backend.beginFrame();
    panel.render(backend, source);
    backend.endFrame();
    CHECK_EQ(panel.rowCount(), 1u);
    CHECK_EQ(countTextOps(backend.capturedFrame()), 1u);

    // Bind tracker + drive desyncs via record/record-pair.
    network::SyncTracker tracker;
    bool callbackFired = false;
    panel.setTracker(&tracker);

    // Wire the tracker callback into the panel.
    tracker.onDesync([&](const network::DesyncReport& r) {
        panel.recordReport(r);
        callbackFired = true;
    });

    // Record a matching local+remote pair → no desync.
    tracker.recordLocal(network::TickId{10}, 0xAAull);
    tracker.recordRemote(network::TickId{10}, 0xAAull);
    CHECK_EQ(tracker.desyncCount(), 0u);
    CHECK_EQ(panel.logSize(), 0u);
    CHECK(!callbackFired);

    // Mismatched pair → 1 desync.
    tracker.recordLocal(network::TickId{11}, 0xBBull);
    tracker.recordRemote(network::TickId{11}, 0xCCull);
    CHECK(callbackFired);
    CHECK_EQ(tracker.desyncCount(), 1u);
    CHECK_EQ(panel.logSize(), 1u);

    backend.beginFrame();
    panel.render(backend, source);
    backend.endFrame();
    CHECK_EQ(panel.rowCount(), 2u); // header + 1 log entry

    // Drive 4 more desyncs → ring caps at 3 log entries (lifetime
    // desync count keeps growing).
    for (std::uint32_t t = 20; t < 24; ++t) {
        tracker.recordLocal(network::TickId{t}, 1ull);
        tracker.recordRemote(network::TickId{t}, 2ull);
    }
    CHECK_EQ(tracker.desyncCount(), 5u);
    CHECK_EQ(panel.logSize(), 3u);

    backend.beginFrame();
    panel.render(backend, source);
    backend.endFrame();
    CHECK_EQ(panel.rowCount(), 4u); // header + 3 capped entries

    panel.clearLog();
    CHECK_EQ(panel.logSize(), 0u);

    backend.shutdown();
    EXIT_WITH_RESULT();
}
