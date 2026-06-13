/// @file panels/PacketTracePanel.cpp
/// @brief ST28 — `PacketTracePanel` implementation.

#include <threadmaxx_studio/panels/packet_trace.hpp>

#include <threadmaxx_editor/backend.hpp>

#include <cstdio>

namespace threadmaxx::studio {

namespace {

const char* typeName(network::PacketType t) noexcept {
    switch (t) {
        case network::PacketType::Hello:         return "Hello";
        case network::PacketType::Welcome:       return "Welcome";
        case network::PacketType::Input:         return "Input";
        case network::PacketType::Snapshot:      return "Snapshot";
        case network::PacketType::Delta:         return "Delta";
        case network::PacketType::Ack:           return "Ack";
        case network::PacketType::ResyncRequest: return "ResyncReq";
        case network::PacketType::ResyncReply:   return "ResyncReply";
        case network::PacketType::Ping:          return "Ping";
        case network::PacketType::Pong:          return "Pong";
        case network::PacketType::DesyncReport:  return "Desync";
        case network::PacketType::Disconnect:    return "Disconnect";
    }
    return "?";
}

} // namespace

PacketTracePanel::PacketTracePanel(std::size_t historyCapacity) noexcept
    : capacity_(historyCapacity == 0 ? 1u : historyCapacity) {}

void PacketTracePanel::recordPacket(const network::PacketHeader& header,
                                    std::size_t payloadBytes,
                                    PacketDirection direction) {
    Entry e;
    e.packetIndex = nextIndex_++;
    e.type        = header.type;
    e.direction   = direction;
    e.sequence    = header.sequence;
    e.tick        = header.tick.value;
    e.bytes       = payloadBytes;
    log_.push_back(e);
    while (log_.size() > capacity_) log_.pop_front();
}

void PacketTracePanel::setFilter(network::PacketType type) noexcept {
    filterActive_ = true;
    filter_       = type;
}

std::size_t PacketTracePanel::filteredCount() const noexcept {
    if (!filterActive_) return log_.size();
    std::size_t n = 0;
    for (const auto& e : log_) if (e.type == filter_) ++n;
    return n;
}

void PacketTracePanel::render(editor::IEditorBackend& backend,
                              IStudioDataSource&) {
    char buf[160];
    const auto matchCount = filteredCount();
    if (filterActive_) {
        std::snprintf(buf, sizeof(buf),
                      "Packets  log=%zu/%zu  filter=%s  match=%zu",
                      log_.size(), capacity_, typeName(filter_), matchCount);
    } else {
        std::snprintf(buf, sizeof(buf),
                      "Packets  log=%zu/%zu  filter=<none>",
                      log_.size(), capacity_);
    }
    backend.drawText(buf, 0.0f, 0.0f);

    float y = 16.0f;
    std::size_t shown = 0;
    for (auto it = log_.rbegin(); it != log_.rend(); ++it) {
        if (shown >= maxRows_) break;
        if (filterActive_ && it->type != filter_) continue;
        std::snprintf(buf, sizeof(buf),
                      "#%llu  %s  %-10.10s  seq=%u  tick=%u  bytes=%zu",
                      static_cast<unsigned long long>(it->packetIndex),
                      it->direction == PacketDirection::Inbound ? "RX" : "TX",
                      typeName(it->type),
                      it->sequence, it->tick, it->bytes);
        backend.drawText(buf, 0.0f, y);
        y += 14.0f;
        ++shown;
    }
    lastRows_ = 1 + shown;
}

} // namespace threadmaxx::studio
