#pragma once

/// @file panels/interest.hpp
/// @brief ST26 — `InterestPanel` displays one row per tracked
/// peer in a bound `network::InterestManager`. Each row shows the
/// peer's focus position + a per-peer visibility tally fed by the
/// host (which calls `recordVisibility(peer, count)` after each
/// `buildVisibleSet`). The panel rolls a small histogram across
/// the last N visibility samples.

#include "../panel.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string_view>
#include <unordered_map>

namespace threadmaxx::network {
class InterestManager;
struct PeerId;
} // namespace threadmaxx::network

namespace threadmaxx::studio {

class InterestPanel : public IStudioPanel {
public:
    explicit InterestPanel(std::size_t historyCapacityPerPeer = 32) noexcept;

    void setManager(const network::InterestManager* mgr) noexcept {
        manager_ = mgr;
    }
    [[nodiscard]] const network::InterestManager* manager() const noexcept {
        return manager_;
    }

    /// @brief Push a `buildVisibleSet` result size for @p peer. The
    /// panel keeps the most recent `historyCapacityPerPeer` samples
    /// per peer.
    void recordVisibility(std::uint32_t peer, std::size_t visibleCount);

    /// @brief Drop every recorded history entry (the bound manager is
    /// untouched).
    void clearHistory() noexcept;

    [[nodiscard]] std::size_t sampleCount(std::uint32_t peer) const noexcept;
    [[nodiscard]] std::size_t lastVisibility(std::uint32_t peer) const noexcept;

    std::string_view id() const noexcept override {
        return "sibling.interest";
    }
    std::string_view title() const noexcept override { return "Interest / AOI"; }
    void render(editor::IEditorBackend& backend,
                IStudioDataSource& source) override;

    [[nodiscard]] std::size_t rowCount() const noexcept { return lastRows_; }

private:
    const network::InterestManager* manager_{nullptr};
    std::size_t                     capacity_;
    std::unordered_map<std::uint32_t, std::deque<std::size_t>> history_;
    std::size_t                     lastRows_{0};
};

} // namespace threadmaxx::studio
