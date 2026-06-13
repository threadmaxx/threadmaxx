/// @file test_studio_network_aoi_view.cpp
/// @brief ST26 — InterestPanel reads InterestManager focuses,
/// rolls per-peer visibility samples, and renders one row per
/// tracked peer.

#include "Check.hpp"

#include <threadmaxx_editor/backends/headless.hpp>
#include <threadmaxx_studio/panels/interest.hpp>

#include <threadmaxx_network/ids.hpp>
#include <threadmaxx_network/interest.hpp>

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
    studio::InterestPanel panel{8};

    editor::HeadlessBackend backend;
    backend.initialize();
    NullSource source;

    // Detached.
    backend.beginFrame();
    panel.render(backend, source);
    backend.endFrame();
    CHECK_EQ(panel.rowCount(), 1u);
    CHECK_EQ(countTextOps(backend.capturedFrame()), 1u);

    // Bind an InterestManager with two focused peers.
    network::InterestManager mgr;
    network::ClientFocus f1{};
    f1.peer = network::PeerId{1}; f1.x = 0.0f; f1.y = 0.0f; f1.z = 0.0f;
    network::ClientFocus f2{};
    f2.peer = network::PeerId{2}; f2.x = 5.0f; f2.y = 0.0f; f2.z = 0.0f;
    mgr.setFocus(f1);
    mgr.setFocus(f2);
    panel.setManager(&mgr);

    // No samples yet → header only.
    backend.beginFrame();
    panel.render(backend, source);
    backend.endFrame();
    CHECK_EQ(panel.rowCount(), 1u);

    // Record samples for both peers; panel emits one row per peer.
    panel.recordVisibility(1, 3);
    panel.recordVisibility(1, 5);
    panel.recordVisibility(2, 8);
    CHECK_EQ(panel.sampleCount(1), 2u);
    CHECK_EQ(panel.sampleCount(2), 1u);
    CHECK_EQ(panel.lastVisibility(1), 5u);
    CHECK_EQ(panel.lastVisibility(2), 8u);

    backend.beginFrame();
    panel.render(backend, source);
    backend.endFrame();
    CHECK_EQ(panel.rowCount(), 3u);
    CHECK_EQ(countTextOps(backend.capturedFrame()), 3u);

    // Overflow the ring → cap respected.
    for (int i = 0; i < 20; ++i) panel.recordVisibility(1, 1);
    CHECK_EQ(panel.sampleCount(1), 8u);

    // Record for an unknown peer (no focus) → still rendered; tagged
    // <untracked>.
    panel.recordVisibility(99, 0);
    backend.beginFrame();
    panel.render(backend, source);
    backend.endFrame();
    CHECK_EQ(panel.rowCount(), 4u);

    // clearHistory drops rows but the manager binding survives.
    panel.clearHistory();
    CHECK_EQ(panel.sampleCount(1), 0u);
    backend.beginFrame();
    panel.render(backend, source);
    backend.endFrame();
    CHECK_EQ(panel.rowCount(), 1u);

    backend.shutdown();
    EXIT_WITH_RESULT();
}
