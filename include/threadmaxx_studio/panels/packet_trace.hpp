#pragma once

/// @file panels/packet_trace.hpp
/// @brief ST28 — `PacketTracePanel` is a host-fed log of recent
/// packets. Each entry carries `PacketType + tick + sequence +
/// payload size` so the panel can render a filterable trace. Host
/// calls `recordPacket(header, payloadBytes, direction)` from its
/// transport hook.

#include "../panel.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string_view>

#include <threadmaxx_network/packets.hpp>

namespace threadmaxx::studio {

enum class PacketDirection : std::uint8_t {
    Inbound  = 0,
    Outbound = 1,
};

class PacketTracePanel : public IStudioPanel {
public:
    explicit PacketTracePanel(std::size_t historyCapacity = 64) noexcept;

    /// @brief Append a record. Drops the oldest at capacity.
    void recordPacket(const network::PacketHeader& header,
                      std::size_t payloadBytes,
                      PacketDirection direction);

    /// @brief Restrict subsequent renders to a single packet type.
    /// `clearFilter` removes the filter.
    void setFilter(network::PacketType type) noexcept;
    void clearFilter() noexcept { filterActive_ = false; }
    [[nodiscard]] bool hasFilter() const noexcept { return filterActive_; }
    [[nodiscard]] network::PacketType filter() const noexcept { return filter_; }

    void clear() noexcept { log_.clear(); }
    [[nodiscard]] std::size_t logSize() const noexcept { return log_.size(); }
    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

    /// @brief Count of buffered entries that match the active filter
    /// (or every entry when no filter is active).
    [[nodiscard]] std::size_t filteredCount() const noexcept;

    std::string_view id() const noexcept override {
        return "sibling.packet_trace";
    }
    std::string_view title() const noexcept override { return "Packet Trace"; }
    void render(editor::IEditorBackend& backend,
                IStudioDataSource& source) override;

    [[nodiscard]] std::size_t rowCount() const noexcept { return lastRows_; }
    void setMaxRows(std::size_t n) noexcept { maxRows_ = n; }

private:
    struct Entry {
        std::uint64_t       packetIndex{0};
        network::PacketType type{network::PacketType::Hello};
        PacketDirection     direction{PacketDirection::Inbound};
        std::uint32_t       sequence{0};
        std::uint32_t       tick{0};
        std::size_t         bytes{0};
    };

    std::size_t          capacity_;
    std::deque<Entry>    log_;
    std::uint64_t        nextIndex_{1};
    bool                 filterActive_{false};
    network::PacketType  filter_{network::PacketType::Hello};
    std::size_t          maxRows_{16};
    std::size_t          lastRows_{0};
};

} // namespace threadmaxx::studio
