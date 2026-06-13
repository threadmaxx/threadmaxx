/// @file StudioAgent.cpp
/// @brief ST29 — StudioAgent implementation. See `agent.hpp` for the
/// wire-format layout.

#include <threadmaxx_studio/agent.hpp>

#include <threadmaxx_editor/commands.hpp>

#include <array>
#include <cstring>
#include <utility>

namespace threadmaxx::studio {

namespace {

// Host-endian POD writers / readers. The agent wire is host-endian
// (same caveat as WorldSnapshot + editor RemoteBackend); studio and
// agent must be the same architecture.

template <class T>
void appendPod(std::vector<std::byte>& dst, const T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    const auto offset = dst.size();
    dst.resize(offset + sizeof(T));
    std::memcpy(dst.data() + offset, &value, sizeof(T));
}

template <class T>
bool readPod(std::span<const std::byte>& src, T& out) {
    static_assert(std::is_trivially_copyable_v<T>);
    if (src.size() < sizeof(T)) return false;
    std::memcpy(&out, src.data(), sizeof(T));
    src = src.subspan(sizeof(T));
    return true;
}

void encodeEngineSnapshotPayload(std::vector<std::byte>& dst,
                                 const EngineFrameSummary& s) {
    appendPod(dst, s.tick);
    appendPod(dst, s.lastStepSeconds);
    appendPod(dst, static_cast<std::uint8_t>(s.paused ? 1 : 0));
    appendPod(dst, s.systemCount);
    appendPod(dst, s.workerCount);
}

bool decodeEngineSnapshotPayload(std::span<const std::byte> bytes,
                                 EngineFrameSummary& out) {
    if (!readPod(bytes, out.tick)) return false;
    if (!readPod(bytes, out.lastStepSeconds)) return false;
    std::uint8_t paused{};
    if (!readPod(bytes, paused)) return false;
    out.paused = (paused != 0);
    if (!readPod(bytes, out.systemCount)) return false;
    if (!readPod(bytes, out.workerCount)) return false;
    return true;
}

} // namespace

// -----------------------------------------------------------------------------
// Frame encoders / decoders

std::vector<std::byte>
encodeGetEngineSnapshotRequest(std::uint32_t requestId) {
    std::vector<std::byte> out;
    out.reserve(1 + sizeof(std::uint32_t));
    appendPod(out, static_cast<std::uint8_t>(AgentRequestTag::GetEngineSnapshot));
    appendPod(out, requestId);
    return out;
}

std::vector<std::byte>
encodeAuthenticateRequest(std::uint32_t requestId, std::string_view token) {
    std::vector<std::byte> out;
    out.reserve(1 + sizeof(std::uint32_t) + sizeof(std::uint32_t) + token.size());
    appendPod(out, static_cast<std::uint8_t>(AgentRequestTag::Authenticate));
    appendPod(out, requestId);
    appendPod(out, static_cast<std::uint32_t>(token.size()));
    const auto offset = out.size();
    out.resize(offset + token.size());
    if (!token.empty()) {
        std::memcpy(out.data() + offset, token.data(), token.size());
    }
    return out;
}

std::vector<std::byte>
encodeAuthResultResponse(std::uint32_t requestId, bool accepted) {
    std::vector<std::byte> out;
    out.reserve(1 + sizeof(std::uint32_t) + 1);
    appendPod(out, static_cast<std::uint8_t>(AgentResponseTag::AuthResult));
    appendPod(out, requestId);
    appendPod(out, static_cast<std::uint8_t>(accepted ? 1 : 0));
    return out;
}

std::vector<std::byte>
encodeSubmitCommandRequest(std::uint32_t requestId, std::string_view label) {
    std::vector<std::byte> out;
    out.reserve(1 + sizeof(std::uint32_t) + sizeof(std::uint32_t) + label.size());
    appendPod(out, static_cast<std::uint8_t>(AgentRequestTag::SubmitCommand));
    appendPod(out, requestId);
    appendPod(out, static_cast<std::uint32_t>(label.size()));
    const auto offset = out.size();
    out.resize(offset + label.size());
    if (!label.empty()) {
        std::memcpy(out.data() + offset, label.data(), label.size());
    }
    return out;
}

std::vector<std::byte>
encodeCommandResultResponse(std::uint32_t requestId, bool accepted) {
    std::vector<std::byte> out;
    out.reserve(1 + sizeof(std::uint32_t) + 1);
    appendPod(out, static_cast<std::uint8_t>(AgentResponseTag::CommandResult));
    appendPod(out, requestId);
    appendPod(out, static_cast<std::uint8_t>(accepted ? 1 : 0));
    return out;
}

std::vector<std::byte>
encodeEngineSnapshotResponse(std::uint32_t requestId,
                             std::optional<EngineFrameSummary> summary) {
    std::vector<std::byte> out;
    out.reserve(1 + sizeof(std::uint32_t) + 1 + sizeof(EngineFrameSummary));
    appendPod(out, static_cast<std::uint8_t>(AgentResponseTag::EngineSnapshot));
    appendPod(out, requestId);
    if (summary.has_value()) {
        appendPod(out, static_cast<std::uint8_t>(1));
        encodeEngineSnapshotPayload(out, *summary);
    } else {
        appendPod(out, static_cast<std::uint8_t>(0));
    }
    return out;
}

std::optional<DecodedAgentResponse>
decodeAgentResponse(std::span<const std::byte> bytes) {
    DecodedAgentResponse out{};
    std::uint8_t tagByte{};
    if (!readPod(bytes, tagByte)) return std::nullopt;
    out.tag = static_cast<AgentResponseTag>(tagByte);
    if (!readPod(bytes, out.requestId)) return std::nullopt;
    std::uint8_t okByte{};
    if (!readPod(bytes, okByte)) return std::nullopt;
    out.ok = (okByte != 0);

    switch (out.tag) {
        case AgentResponseTag::EngineSnapshot:
            if (out.ok) {
                if (!decodeEngineSnapshotPayload(bytes, out.engineSnapshot)) {
                    return std::nullopt;
                }
            }
            return out;
        case AgentResponseTag::AuthResult:
        case AgentResponseTag::CommandResult:
            // ok byte already consumed; no extra payload.
            return out;
    }
    return std::nullopt;
}

// -----------------------------------------------------------------------------
// StudioAgent

StudioAgent::StudioAgent(network::ITransport& transport,
                        IStudioDataSource& source) noexcept
    : transport_(&transport), source_(&source) {}

void StudioAgent::setAuthToken(std::string token) {
    authToken_ = std::move(token);
    // Changing the token drops every previously-authenticated peer —
    // the studio side must re-authenticate against the new value.
    authenticatedPeers_.clear();
}

bool StudioAgent::isPeerAuthenticated(network::PeerId peer) const noexcept {
    if (authToken_.empty()) return true;
    return authenticatedPeers_.find(peer) != authenticatedPeers_.end();
}

void StudioAgent::setCommandStack(editor::CommandStack* stack) noexcept {
    commandStack_ = stack;
}

bool StudioAgent::registerCommandFactory(std::string label,
                                        CommandFactory factory) {
    auto [it, inserted] = commandFactories_.emplace(std::move(label),
                                                    std::move(factory));
    if (!inserted) {
        it->second = std::move(factory);
    }
    return inserted;
}

bool StudioAgent::dispatchCommand_(std::string_view label) {
    if (commandStack_ == nullptr) return false;
    auto it = commandFactories_.find(std::string(label));
    if (it == commandFactories_.end()) return false;
    auto cmd = it->second();
    if (!cmd) return false;
    commandStack_->execute(std::move(cmd));
    return true;
}

std::size_t StudioAgent::pump() {
    if (transport_ == nullptr || source_ == nullptr) return 0;

    transport_->poll();

    std::array<network::ReceivedPacket, 16> inbox{};
    std::size_t handledThisCall = 0;

    while (true) {
        const auto received = transport_->receive(inbox);
        if (received == 0) break;
        for (std::size_t i = 0; i < received; ++i) {
            const auto& pkt = inbox[i];
            // Production attach gate: drain but don't dispatch when
            // disabled. Counters stay at zero — the agent is invisible.
            if (!attachEnabled_) continue;
            std::span<const std::byte> view{pkt.payload.data(),
                                            pkt.payload.size()};
            handleRequest_(pkt.peer, view);
            ++handledThisCall;
        }
        if (received < inbox.size()) break;
    }

    requestsHandled_ += handledThisCall;
    return handledThisCall;
}

void StudioAgent::handleRequest_(network::PeerId from,
                                 std::span<const std::byte> bytes) {
    if (bytes.empty()) return;

    std::uint8_t tagByte{};
    if (!readPod(bytes, tagByte)) return;
    std::uint32_t requestId{};
    if (!readPod(bytes, requestId)) return;

    const auto tag = static_cast<AgentRequestTag>(tagByte);

    // Authentication itself is the one request the unauthenticated
    // peer is allowed to send. Every other request gates on the auth
    // check first.
    if (tag != AgentRequestTag::Authenticate && !isPeerAuthenticated(from)) {
        // Reply with a kind-appropriate ok=0 so the studio side knows
        // the request was refused (rather than dropped). The
        // shape of the response matches the request to keep the
        // RemoteDataSource decode loop simple.
        std::vector<std::byte> response;
        switch (tag) {
            case AgentRequestTag::GetEngineSnapshot:
                response = encodeEngineSnapshotResponse(requestId, std::nullopt);
                break;
            case AgentRequestTag::SubmitCommand:
                response = encodeCommandResultResponse(requestId, false);
                ++commandsRejected_;
                break;
            case AgentRequestTag::Authenticate:
                return; // unreachable: filtered above.
        }
        const network::PacketView view{response.data(), response.size()};
        if (transport_->send(from, view)) {
            bytesSent_ += response.size();
        }
        return;
    }

    switch (tag) {
        case AgentRequestTag::Authenticate: {
            std::uint32_t tokenLen{};
            if (!readPod(bytes, tokenLen)) return;
            if (bytes.size() < tokenLen) return;
            const std::string_view token{
                reinterpret_cast<const char*>(bytes.data()), tokenLen};
            bool accepted = false;
            if (authToken_.empty()) {
                // Open mode — auth always succeeds.
                accepted = true;
                authenticatedPeers_.insert(from);
            } else if (token == authToken_) {
                accepted = true;
                authenticatedPeers_.insert(from);
            } else {
                // Wrong token: drop any prior auth state for this peer.
                authenticatedPeers_.erase(from);
            }
            auto response = encodeAuthResultResponse(requestId, accepted);
            const network::PacketView view{response.data(), response.size()};
            if (transport_->send(from, view)) {
                bytesSent_ += response.size();
            }
            return;
        }
        case AgentRequestTag::GetEngineSnapshot: {
            auto response = encodeEngineSnapshotResponse(
                requestId, source_->engineSnapshot());
            const network::PacketView view{response.data(), response.size()};
            if (transport_->send(from, view)) {
                bytesSent_ += response.size();
            }
            return;
        }
        case AgentRequestTag::SubmitCommand: {
            std::uint32_t labelLen{};
            if (!readPod(bytes, labelLen)) return;
            if (bytes.size() < labelLen) return;
            const std::string_view label{
                reinterpret_cast<const char*>(bytes.data()), labelLen};
            const bool accepted = dispatchCommand_(label);
            if (accepted) {
                ++commandsApplied_;
            } else {
                ++commandsRejected_;
            }
            auto response = encodeCommandResultResponse(requestId, accepted);
            const network::PacketView view{response.data(), response.size()};
            if (transport_->send(from, view)) {
                bytesSent_ += response.size();
            }
            return;
        }
    }
}

} // namespace threadmaxx::studio
