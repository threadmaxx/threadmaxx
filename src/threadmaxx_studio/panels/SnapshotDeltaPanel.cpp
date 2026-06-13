/// @file panels/SnapshotDeltaPanel.cpp
/// @brief ST25 — `SnapshotDeltaPanel` implementation.

#include <threadmaxx_studio/panels/snapshot_delta.hpp>

#include <threadmaxx_editor/backend.hpp>

#include <cstdio>

namespace threadmaxx::studio {

namespace {

const char* channelName(SnapshotChannel c) noexcept {
    switch (c) {
        case SnapshotChannel::Full:  return "full";
        case SnapshotChannel::Delta: return "delta";
    }
    return "?";
}

} // namespace

SnapshotDeltaPanel::SnapshotDeltaPanel(std::size_t historyCapacity) noexcept
    : capacity_(historyCapacity == 0 ? 1u : historyCapacity) {}

void SnapshotDeltaPanel::recordSnapshot(std::uint64_t tick,
                                        SnapshotChannel channel,
                                        std::size_t bytes) {
    history_.push_back(Entry{tick, channel, bytes});
    while (history_.size() > capacity_) history_.pop_front();
    const auto idx = static_cast<std::size_t>(channel);
    totalBytes_[idx] += static_cast<std::uint64_t>(bytes);
    ++totalCount_[idx];
}

void SnapshotDeltaPanel::clear() noexcept {
    history_.clear();
    for (auto& v : totalBytes_) v = 0;
    for (auto& v : totalCount_) v = 0;
}

std::uint64_t SnapshotDeltaPanel::totalBytes(SnapshotChannel c) const noexcept {
    return totalBytes_[static_cast<std::size_t>(c)];
}

std::uint64_t SnapshotDeltaPanel::totalSnapshots(SnapshotChannel c) const noexcept {
    return totalCount_[static_cast<std::size_t>(c)];
}

void SnapshotDeltaPanel::render(editor::IEditorBackend& backend,
                                IStudioDataSource&) {
    char buf[160];
    std::snprintf(buf, sizeof(buf),
                  "Snapshot bandwidth  history=%zu/%zu",
                  history_.size(), capacity_);
    backend.drawText(buf, 0.0f, 0.0f);

    float y = 16.0f;
    for (std::size_t c = 0; c < kSnapshotChannelCount; ++c) {
        const auto ch = static_cast<SnapshotChannel>(c);
        std::snprintf(buf, sizeof(buf),
                      "%-5s  snapshots=%llu  bytes=%llu",
                      channelName(ch),
                      static_cast<unsigned long long>(totalCount_[c]),
                      static_cast<unsigned long long>(totalBytes_[c]));
        backend.drawText(buf, 0.0f, y);
        y += 14.0f;
    }

    std::size_t shown = 0;
    const std::size_t cap = recentRows_ < history_.size()
                                ? recentRows_ : history_.size();
    // Walk from newest backwards so the panel's recent list reads
    // newest-first.
    auto it = history_.rbegin();
    while (shown < cap && it != history_.rend()) {
        std::snprintf(buf, sizeof(buf),
                      "tick=%llu  %-5s  bytes=%zu",
                      static_cast<unsigned long long>(it->tick),
                      channelName(it->channel),
                      it->bytes);
        backend.drawText(buf, 0.0f, y);
        y += 14.0f;
        ++it;
        ++shown;
    }
    lastRows_ = 1 + kSnapshotChannelCount + shown;
}

} // namespace threadmaxx::studio
