/// @file test_studio_network_bandwidth_chart.cpp
/// @brief ST25 — `SnapshotDeltaPanel` tracks per-channel byte totals,
/// rolls the history at capacity, and emits per-channel + recent rows.

#include "Check.hpp"

#include <threadmaxx_editor/backends/headless.hpp>
#include <threadmaxx_studio/panels/snapshot_delta.hpp>

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
    studio::SnapshotDeltaPanel panel{4};
    CHECK_EQ(panel.historyCapacity(), 4u);

    editor::HeadlessBackend backend;
    backend.initialize();
    NullSource source;

    // Empty render — 1 header + 2 channel rows + 0 recent.
    backend.beginFrame();
    panel.render(backend, source);
    backend.endFrame();
    CHECK_EQ(panel.rowCount(), 1u + 2u);
    CHECK_EQ(countTextOps(backend.capturedFrame()), 3u);

    // Push 3 snapshots — 2 full + 1 delta.
    panel.recordSnapshot(100, studio::SnapshotChannel::Full,  1200);
    panel.recordSnapshot(101, studio::SnapshotChannel::Delta,  150);
    panel.recordSnapshot(102, studio::SnapshotChannel::Full,  1100);
    CHECK_EQ(panel.historyCount(), 3u);
    CHECK_EQ(panel.totalBytes(studio::SnapshotChannel::Full), 2300u);
    CHECK_EQ(panel.totalBytes(studio::SnapshotChannel::Delta), 150u);
    CHECK_EQ(panel.totalSnapshots(studio::SnapshotChannel::Full),  2u);
    CHECK_EQ(panel.totalSnapshots(studio::SnapshotChannel::Delta), 1u);

    backend.beginFrame();
    panel.render(backend, source);
    backend.endFrame();
    // 1 header + 2 channel + 3 recent = 6.
    CHECK_EQ(panel.rowCount(), 6u);

    // Overflow the ring — oldest entry drops; lifetime totals stay.
    panel.recordSnapshot(103, studio::SnapshotChannel::Delta, 200);
    panel.recordSnapshot(104, studio::SnapshotChannel::Delta, 200);
    CHECK_EQ(panel.historyCount(), 4u); // capped at 4
    CHECK_EQ(panel.totalSnapshots(studio::SnapshotChannel::Delta), 3u);
    CHECK_EQ(panel.totalBytes(studio::SnapshotChannel::Delta), 550u);

    // Recent rows cap respects setRecentRows.
    panel.setRecentRows(2);
    backend.beginFrame();
    panel.render(backend, source);
    backend.endFrame();
    CHECK_EQ(panel.rowCount(), 1u + 2u + 2u);

    // Clear zeroes everything.
    panel.clear();
    CHECK_EQ(panel.historyCount(), 0u);
    CHECK_EQ(panel.totalBytes(studio::SnapshotChannel::Full), 0u);

    backend.shutdown();
    EXIT_WITH_RESULT();
}
