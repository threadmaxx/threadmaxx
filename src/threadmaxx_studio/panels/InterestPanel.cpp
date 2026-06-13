/// @file panels/InterestPanel.cpp
/// @brief ST26 — `InterestPanel` implementation.

#include <threadmaxx_studio/panels/interest.hpp>

#include <threadmaxx_editor/backend.hpp>

#include <threadmaxx_network/ids.hpp>
#include <threadmaxx_network/interest.hpp>

#include <algorithm>
#include <cstdio>
#include <vector>

namespace threadmaxx::studio {

InterestPanel::InterestPanel(std::size_t historyCapacityPerPeer) noexcept
    : capacity_(historyCapacityPerPeer == 0 ? 1u : historyCapacityPerPeer) {}

void InterestPanel::recordVisibility(std::uint32_t peer,
                                     std::size_t visibleCount) {
    auto& ring = history_[peer];
    ring.push_back(visibleCount);
    while (ring.size() > capacity_) ring.pop_front();
}

void InterestPanel::clearHistory() noexcept {
    history_.clear();
}

std::size_t InterestPanel::sampleCount(std::uint32_t peer) const noexcept {
    auto it = history_.find(peer);
    return it == history_.end() ? 0u : it->second.size();
}

std::size_t InterestPanel::lastVisibility(std::uint32_t peer) const noexcept {
    auto it = history_.find(peer);
    if (it == history_.end() || it->second.empty()) return 0u;
    return it->second.back();
}

void InterestPanel::render(editor::IEditorBackend& backend,
                           IStudioDataSource&) {
    char buf[160];
    if (manager_ == nullptr) {
        std::snprintf(buf, sizeof(buf),
                      "Interest / AOI  <detached>  history-peers=%zu",
                      history_.size());
        backend.drawText(buf, 0.0f, 0.0f);
        lastRows_ = 1;
        return;
    }
    std::snprintf(buf, sizeof(buf),
                  "Interest / AOI  managed-peers=%zu  history-peers=%zu",
                  manager_->focusCount(), history_.size());
    backend.drawText(buf, 0.0f, 0.0f);

    // Stable ordering across renders so tests can index by row.
    std::vector<std::uint32_t> ids;
    ids.reserve(history_.size());
    for (const auto& kv : history_) ids.push_back(kv.first);
    std::sort(ids.begin(), ids.end());

    float y = 16.0f;
    for (auto pid : ids) {
        const auto& ring = history_[pid];
        std::size_t maxV = 0;
        std::uint64_t sum = 0;
        for (auto v : ring) {
            sum += static_cast<std::uint64_t>(v);
            if (v > maxV) maxV = v;
        }
        const double avg = ring.empty()
                               ? 0.0
                               : static_cast<double>(sum)
                                     / static_cast<double>(ring.size());

        const auto* focus = manager_->focus(network::PeerId{pid});
        if (focus != nullptr) {
            std::snprintf(buf, sizeof(buf),
                          "peer#%u  focus=(%.1f,%.1f,%.1f)  "
                          "vis last=%zu  avg=%.1f  max=%zu  n=%zu",
                          pid,
                          static_cast<double>(focus->x),
                          static_cast<double>(focus->y),
                          static_cast<double>(focus->z),
                          ring.empty() ? 0u : ring.back(),
                          avg, maxV, ring.size());
        } else {
            std::snprintf(buf, sizeof(buf),
                          "peer#%u  <untracked>  "
                          "vis last=%zu  avg=%.1f  max=%zu  n=%zu",
                          pid,
                          ring.empty() ? 0u : ring.back(),
                          avg, maxV, ring.size());
        }
        backend.drawText(buf, 0.0f, y);
        y += 14.0f;
    }
    lastRows_ = 1 + ids.size();
}

} // namespace threadmaxx::studio
