/// @file RemoteDataSource.cpp
/// @brief ST30 — RemoteDataSource implementation. See
/// `remote_data_source.hpp` for the cache contract.

#include <threadmaxx_studio/remote_data_source.hpp>

#include <threadmaxx_studio/agent.hpp>

#include <array>
#include <span>

namespace threadmaxx::studio {

RemoteDataSource::RemoteDataSource(network::ITransport& transport,
                                  network::PeerId agentPeer) noexcept
    : transport_(&transport), agentPeer_(agentPeer) {}

std::optional<EngineFrameSummary>
RemoteDataSource::engineSnapshot() const {
    return engineSnapshotCache_;
}

namespace {
// Auth is a one-shot setup call and bypasses the per-tick budget;
// every other request uses the helper below to charge the budget.
} // namespace

std::uint32_t RemoteDataSource::authenticate(std::string_view token) {
    if (transport_ == nullptr) return 0;
    const auto requestId = nextRequestId_++;
    const auto bytes = encodeAuthenticateRequest(requestId, token);
    const network::PacketView view{bytes.data(), bytes.size()};
    transport_->send(agentPeer_, view);
    return requestId;
}

std::uint32_t RemoteDataSource::requestEngineSnapshot() {
    if (transport_ == nullptr) return 0;
    if (requestsPerTickBudget_ != 0 &&
        requestsThisTick_ >= requestsPerTickBudget_) {
        ++requestsDropped_;
        return 0;
    }
    const auto requestId = nextRequestId_++;
    const auto bytes = encodeGetEngineSnapshotRequest(requestId);
    const network::PacketView view{bytes.data(), bytes.size()};
    if (transport_->send(agentPeer_, view)) {
        lastEngineSnapshotRequestId_ = requestId;
        ++requestsThisTick_;
    }
    return requestId;
}

bool RemoteDataSource::submitCommand(std::string_view label) {
    if (transport_ == nullptr) return false;
    if (requestsPerTickBudget_ != 0 &&
        requestsThisTick_ >= requestsPerTickBudget_) {
        ++requestsDropped_;
        return false;
    }
    const auto requestId = nextRequestId_++;
    const auto bytes = encodeSubmitCommandRequest(requestId, label);
    const network::PacketView view{bytes.data(), bytes.size()};
    if (!transport_->send(agentPeer_, view)) return false;
    lastCommandRequestId_ = requestId;
    ++requestsThisTick_;
    return true;
}

std::uint32_t RemoteDataSource::pushClientFocus() {
    if (transport_ == nullptr || !hasClientFocus_) return 0;
    if (requestsPerTickBudget_ != 0 &&
        requestsThisTick_ >= requestsPerTickBudget_) {
        ++requestsDropped_;
        return 0;
    }
    const auto requestId = nextRequestId_++;
    const auto bytes = encodeSetClientFocusRequest(requestId, clientFocus_);
    const network::PacketView view{bytes.data(), bytes.size()};
    if (transport_->send(agentPeer_, view)) {
        ++requestsThisTick_;
    }
    return requestId;
}

std::size_t RemoteDataSource::pump() {
    if (transport_ == nullptr) return 0;

    transport_->poll();

    std::array<network::ReceivedPacket, 16> inbox{};
    std::size_t processedThisCall = 0;

    while (true) {
        const auto received = transport_->receive(inbox);
        if (received == 0) break;
        for (std::size_t i = 0; i < received; ++i) {
            const auto& pkt = inbox[i];
            if (pkt.peer != agentPeer_) continue;
            std::span<const std::byte> view{pkt.payload.data(),
                                            pkt.payload.size()};
            auto decoded = decodeAgentResponse(view);
            if (!decoded.has_value()) continue;
            bytesReceived_ += pkt.payload.size();
            ++processedThisCall;
            switch (decoded->tag) {
                case AgentResponseTag::AuthResult:
                    lastAuthAccepted_ = decoded->ok;
                    ++authResponsesReceived_;
                    break;
                case AgentResponseTag::EngineSnapshot:
                    if (decoded->ok) {
                        engineSnapshotCache_ = decoded->engineSnapshot;
                    } else {
                        engineSnapshotCache_.reset();
                    }
                    break;
                case AgentResponseTag::CommandResult:
                    lastCommandAccepted_ = decoded->ok;
                    ++commandResponsesReceived_;
                    break;
                case AgentResponseTag::FocusAck:
                    lastFocusAccepted_ = decoded->ok;
                    ++focusResponsesReceived_;
                    break;
            }
        }
        if (received < inbox.size()) break;
    }

    responsesReceived_ += processedThisCall;
    return processedThisCall;
}

void RemoteDataSource::invalidateCache() noexcept {
    engineSnapshotCache_.reset();
}

} // namespace threadmaxx::studio
