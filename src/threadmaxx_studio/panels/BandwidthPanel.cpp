/// @file panels/BandwidthPanel.cpp
/// @brief ST33 — `BandwidthPanel` implementation.

#include <threadmaxx_studio/agent.hpp>
#include <threadmaxx_studio/panels/bandwidth.hpp>
#include <threadmaxx_studio/remote_data_source.hpp>

#include <threadmaxx_editor/backend.hpp>

#include <cstdio>

namespace threadmaxx::studio {

BandwidthPanel::BandwidthPanel(RemoteDataSource& remote,
                               StudioAgent* agent,
                               std::size_t historyCapacity) noexcept
    : remote_(&remote),
      agent_(agent),
      capacity_(historyCapacity == 0 ? 1u : historyCapacity) {}

void BandwidthPanel::sample(std::uint64_t tick) {
    Sample s{};
    s.tick = tick;
    s.remoteBytesReceived     = remote_->bytesReceived();
    s.remoteResponsesReceived = remote_->responsesReceived();
    s.requestsThisTick        = remote_->requestsThisTick();
    s.requestsDropped         = remote_->requestsDropped();
    if (agent_ != nullptr) {
        s.agentBytesSent       = agent_->bytesSent();
        s.agentRequestsHandled = agent_->requestsHandled();
    }
    samples_.push_back(s);
    while (samples_.size() > capacity_) samples_.pop_front();
}

BandwidthPanel::Sample BandwidthPanel::latest() const noexcept {
    if (samples_.empty()) return Sample{};
    return samples_.back();
}

void BandwidthPanel::setRequestsPerTickBudget(std::uint32_t n) noexcept {
    if (remote_ != nullptr) remote_->setRequestsPerTickBudget(n);
}

void BandwidthPanel::render(editor::IEditorBackend& backend,
                            IStudioDataSource&) {
    char buf[200];
    const auto budget = remote_ != nullptr
                          ? remote_->requestsPerTickBudget()
                          : 0u;
    std::snprintf(buf, sizeof(buf),
                  "Bandwidth  samples=%zu/%zu  budget=%u/tick",
                  samples_.size(), capacity_, budget);
    backend.drawText(buf, 0.0f, 0.0f);

    float y = 16.0f;
    std::size_t shown = 0;
    for (auto it = samples_.rbegin(); it != samples_.rend(); ++it) {
        if (shown >= maxRows_) break;
        std::snprintf(buf, sizeof(buf),
                      "tick=%llu  rx=%zu B / %zu  tx=%zu B / %zu  req=%u  drop=%u",
                      static_cast<unsigned long long>(it->tick),
                      it->remoteBytesReceived, it->remoteResponsesReceived,
                      it->agentBytesSent, it->agentRequestsHandled,
                      it->requestsThisTick, it->requestsDropped);
        backend.drawText(buf, 0.0f, y);
        y += 14.0f;
        ++shown;
    }
    lastRows_ = 1 + shown;
}

} // namespace threadmaxx::studio
