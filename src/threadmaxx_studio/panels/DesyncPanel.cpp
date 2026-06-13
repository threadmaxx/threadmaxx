/// @file panels/DesyncPanel.cpp
/// @brief ST27 — `DesyncPanel` implementation.

#include <threadmaxx_studio/panels/desync.hpp>

#include <threadmaxx_editor/backend.hpp>

#include <threadmaxx_network/diagnostics.hpp>

#include <cstdio>

namespace threadmaxx::studio {

DesyncPanel::DesyncPanel(std::size_t historyCapacity) noexcept
    : capacity_(historyCapacity == 0 ? 1u : historyCapacity) {}

void DesyncPanel::recordReport(const network::DesyncReport& r) {
    log_.push_back(LogEntry{r.tick.value, r.localHash, r.remoteHash});
    while (log_.size() > capacity_) log_.pop_front();
}

void DesyncPanel::render(editor::IEditorBackend& backend,
                         IStudioDataSource&) {
    char buf[160];
    if (tracker_ == nullptr) {
        std::snprintf(buf, sizeof(buf),
                      "Desync  <no tracker>  log=%zu/%zu",
                      log_.size(), capacity_);
        backend.drawText(buf, 0.0f, 0.0f);
        lastRows_ = 1;
        return;
    }

    std::snprintf(buf, sizeof(buf),
                  "Desync  history=%zu  desyncs=%llu  log=%zu/%zu",
                  tracker_->historyCount(),
                  static_cast<unsigned long long>(tracker_->desyncCount()),
                  log_.size(), capacity_);
    backend.drawText(buf, 0.0f, 0.0f);

    float y = 16.0f;
    // Newest first.
    for (auto it = log_.rbegin(); it != log_.rend(); ++it) {
        std::snprintf(buf, sizeof(buf),
                      "tick=%llu  local=0x%016llX  remote=0x%016llX",
                      static_cast<unsigned long long>(it->tick),
                      static_cast<unsigned long long>(it->localHash),
                      static_cast<unsigned long long>(it->remoteHash));
        backend.drawText(buf, 0.0f, y);
        y += 14.0f;
    }
    lastRows_ = 1 + log_.size();
}

} // namespace threadmaxx::studio
