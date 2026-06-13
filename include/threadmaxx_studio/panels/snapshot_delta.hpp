#pragma once

/// @file panels/snapshot_delta.hpp
/// @brief ST25 — `SnapshotDeltaPanel` is a host-fed ring of the last
/// N snapshot byte counts plus a per-channel bandwidth rollup.
///
/// The host calls `recordSnapshot(tick, channel, bytes)` whenever a
/// snapshot fragment goes out (or comes in); the panel maintains a
/// fixed-size circular history and renders a per-channel total +
/// the recent per-snapshot bytes. Gates on network NW11.

#include "../panel.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string_view>

namespace threadmaxx::studio {

/// @brief Channels SnapshotDeltaPanel rolls up. Matches the v1.0
/// network public surface (`SnapshotEncoder` produces both kinds).
enum class SnapshotChannel : std::uint8_t {
    Full  = 0,
    Delta = 1,
};

inline constexpr std::size_t kSnapshotChannelCount = 2;

class SnapshotDeltaPanel : public IStudioPanel {
public:
    explicit SnapshotDeltaPanel(std::size_t historyCapacity = 64) noexcept;

    /// @brief Record a snapshot of @p bytes bytes on @p channel at
    /// @p tick. Drops the oldest entry when the ring is full.
    void recordSnapshot(std::uint64_t tick, SnapshotChannel channel,
                        std::size_t bytes);

    /// @brief Reset every counter (history + lifetime totals).
    void clear() noexcept;

    [[nodiscard]] std::size_t historyCount() const noexcept {
        return history_.size();
    }
    [[nodiscard]] std::size_t historyCapacity() const noexcept {
        return capacity_;
    }
    [[nodiscard]] std::uint64_t totalBytes(SnapshotChannel c) const noexcept;
    [[nodiscard]] std::uint64_t totalSnapshots(SnapshotChannel c) const noexcept;

    std::string_view id() const noexcept override {
        return "sibling.snapshot_delta";
    }
    std::string_view title() const noexcept override {
        return "Snapshot Bandwidth";
    }
    void render(editor::IEditorBackend& backend,
                IStudioDataSource& source) override;

    [[nodiscard]] std::size_t rowCount() const noexcept { return lastRows_; }
    /// @brief Recent-rows shown after the per-channel rollup. Caps at
    /// `historyCapacity()`. Default 8.
    void setRecentRows(std::size_t n) noexcept { recentRows_ = n; }

private:
    struct Entry {
        std::uint64_t   tick{0};
        SnapshotChannel channel{SnapshotChannel::Full};
        std::size_t     bytes{0};
    };

    std::size_t                                       capacity_;
    std::deque<Entry>                                 history_;
    std::array<std::uint64_t, kSnapshotChannelCount>  totalBytes_{};
    std::array<std::uint64_t, kSnapshotChannelCount>  totalCount_{};
    std::size_t                                       recentRows_{8};
    std::size_t                                       lastRows_{0};
};

} // namespace threadmaxx::studio
