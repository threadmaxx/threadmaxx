#pragma once

/// @file panels/desync.hpp
/// @brief ST27 — `DesyncPanel` shows the bound `SyncTracker`'s
/// status (desync count + history depth) and the panel's own log
/// of recent `DesyncReport`s. The host wires the panel's
/// `recordReport` into `SyncTracker::onDesync` to populate the
/// log.

#include "../panel.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string_view>

namespace threadmaxx::network {
class SyncTracker;
struct DesyncReport;
} // namespace threadmaxx::network

namespace threadmaxx::studio {

class DesyncPanel : public IStudioPanel {
public:
    explicit DesyncPanel(std::size_t historyCapacity = 32) noexcept;

    void setTracker(const network::SyncTracker* tracker) noexcept {
        tracker_ = tracker;
    }
    [[nodiscard]] const network::SyncTracker* tracker() const noexcept {
        return tracker_;
    }

    /// @brief Append one report to the panel's log. Drops the oldest
    /// when at capacity. Wire this into `SyncTracker::onDesync`.
    void recordReport(const network::DesyncReport& r);

    void clearLog() noexcept { log_.clear(); }
    [[nodiscard]] std::size_t logSize() const noexcept { return log_.size(); }
    [[nodiscard]] std::size_t logCapacity() const noexcept { return capacity_; }

    std::string_view id() const noexcept override {
        return "sibling.desync";
    }
    std::string_view title() const noexcept override { return "Desync"; }
    void render(editor::IEditorBackend& backend,
                IStudioDataSource& source) override;

    [[nodiscard]] std::size_t rowCount() const noexcept { return lastRows_; }

private:
    struct LogEntry {
        std::uint64_t tick{0};
        std::uint64_t localHash{0};
        std::uint64_t remoteHash{0};
    };

    const network::SyncTracker* tracker_{nullptr};
    std::size_t                 capacity_;
    std::deque<LogEntry>        log_;
    std::size_t                 lastRows_{0};
};

} // namespace threadmaxx::studio
