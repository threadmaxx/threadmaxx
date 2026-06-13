#pragma once

/// @file panels/bandwidth.hpp
/// @brief ST33 — `BandwidthPanel` surfaces what the M7 attach costs
/// the game per tick: agent bytes-sent, remote bytes-received,
/// request/response counts. Also exposes the throttle knob on the
/// bound `RemoteDataSource`.
///
/// The panel is read-only over the data source / agent — it never
/// dispatches RPCs. Hosts call `sample(tick)` once per studio frame
/// after they have called `RemoteDataSource::pump()` and
/// `StudioAgent::pump()`.

#include "../panel.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string_view>

namespace threadmaxx::studio {

class RemoteDataSource;
class StudioAgent;

class BandwidthPanel : public IStudioPanel {
public:
    /// @brief One per-tick sample of attach-cost counters.
    struct Sample {
        std::uint64_t tick{0};
        std::size_t   remoteBytesReceived{0};
        std::size_t   remoteResponsesReceived{0};
        std::size_t   agentBytesSent{0};
        std::size_t   agentRequestsHandled{0};
        std::uint32_t requestsThisTick{0};
        std::uint32_t requestsDropped{0};
    };

    /// @brief @p agent is optional (nullable) — if unset, the
    /// `agentBytesSent` / `agentRequestsHandled` fields stay zero
    /// (studio host might be on the wire but lack a direct handle).
    BandwidthPanel(RemoteDataSource& remote,
                   StudioAgent* agent = nullptr,
                   std::size_t historyCapacity = 120) noexcept;

    /// @brief Append a sample of the current counters, keyed by
    /// @p tick. Drops the oldest entry once capacity is reached.
    void sample(std::uint64_t tick);

    /// @brief Snapshot of the most recent sample. Returns a
    /// zero-filled default when the ring is empty.
    [[nodiscard]] Sample latest() const noexcept;

    [[nodiscard]] std::size_t sampleCount() const noexcept {
        return samples_.size();
    }
    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

    /// @brief Forward to `RemoteDataSource::setRequestsPerTickBudget`.
    void setRequestsPerTickBudget(std::uint32_t n) noexcept;

    void clear() noexcept { samples_.clear(); }

    std::string_view id() const noexcept override {
        return "sibling.bandwidth";
    }
    std::string_view title() const noexcept override { return "Bandwidth"; }
    void render(editor::IEditorBackend& backend,
                IStudioDataSource& source) override;

    [[nodiscard]] std::size_t rowCount() const noexcept { return lastRows_; }
    void setMaxRows(std::size_t n) noexcept { maxRows_ = n; }

private:
    RemoteDataSource*    remote_;
    StudioAgent*         agent_;
    std::size_t          capacity_;
    std::deque<Sample>   samples_;
    std::size_t          maxRows_{8};
    std::size_t          lastRows_{0};
};

} // namespace threadmaxx::studio
