/// @file test_studio_network_packet_filter.cpp
/// @brief ST28 — PacketTracePanel records every packet; filter
/// narrows render rows + filteredCount.

#include "Check.hpp"

#include <threadmaxx_editor/backends/headless.hpp>
#include <threadmaxx_studio/panels/packet_trace.hpp>

#include <threadmaxx_network/packets.hpp>

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

threadmaxx::network::PacketHeader hdr(threadmaxx::network::PacketType t,
                                      std::uint32_t seq,
                                      std::uint32_t tick) {
    threadmaxx::network::PacketHeader h;
    h.type = t;
    h.sequence = seq;
    h.tick = threadmaxx::network::TickId{tick};
    return h;
}

} // namespace

int main() {
    using namespace threadmaxx;
    studio::PacketTracePanel panel{16};

    editor::HeadlessBackend backend;
    backend.initialize();
    NullSource source;

    // Empty.
    backend.beginFrame();
    panel.render(backend, source);
    backend.endFrame();
    CHECK_EQ(panel.logSize(), 0u);
    CHECK_EQ(panel.rowCount(), 1u);
    CHECK_EQ(countTextOps(backend.capturedFrame()), 1u);

    // Two of each Input + Snapshot + Ack (six packets total).
    panel.recordPacket(hdr(network::PacketType::Input,    1, 10),  20,
                       studio::PacketDirection::Inbound);
    panel.recordPacket(hdr(network::PacketType::Snapshot, 2, 11),  500,
                       studio::PacketDirection::Outbound);
    panel.recordPacket(hdr(network::PacketType::Ack,      3, 11),  8,
                       studio::PacketDirection::Inbound);
    panel.recordPacket(hdr(network::PacketType::Input,    4, 12),  20,
                       studio::PacketDirection::Inbound);
    panel.recordPacket(hdr(network::PacketType::Snapshot, 5, 12),  450,
                       studio::PacketDirection::Outbound);
    panel.recordPacket(hdr(network::PacketType::Ack,      6, 12),  8,
                       studio::PacketDirection::Inbound);
    CHECK_EQ(panel.logSize(), 6u);
    CHECK_EQ(panel.filteredCount(), 6u);

    backend.beginFrame();
    panel.render(backend, source);
    backend.endFrame();
    // header + 6 rows (under the 16-row cap).
    CHECK_EQ(panel.rowCount(), 7u);
    CHECK_EQ(countTextOps(backend.capturedFrame()), 7u);

    // Filter on Input → only 2 rows.
    panel.setFilter(network::PacketType::Input);
    CHECK(panel.hasFilter());
    CHECK_EQ(panel.filteredCount(), 2u);
    backend.beginFrame();
    panel.render(backend, source);
    backend.endFrame();
    CHECK_EQ(panel.rowCount(), 3u);
    CHECK_EQ(countTextOps(backend.capturedFrame()), 3u);

    // Clearing the filter restores unfiltered render.
    panel.clearFilter();
    CHECK(!panel.hasFilter());
    backend.beginFrame();
    panel.render(backend, source);
    backend.endFrame();
    CHECK_EQ(panel.rowCount(), 7u);

    // maxRows clamps the rendered slice.
    panel.setMaxRows(3);
    backend.beginFrame();
    panel.render(backend, source);
    backend.endFrame();
    CHECK_EQ(panel.rowCount(), 4u);

    backend.shutdown();
    EXIT_WITH_RESULT();
}
