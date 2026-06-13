/// @file StudioAgent.cpp
/// @brief ST29 — StudioAgent implementation. See `agent.hpp` for the
/// wire-format layout.

#include <threadmaxx_studio/agent.hpp>

#include <array>
#include <cstring>

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
    }
    return std::nullopt;
}

// -----------------------------------------------------------------------------
// StudioAgent

StudioAgent::StudioAgent(network::ITransport& transport,
                        IStudioDataSource& source) noexcept
    : transport_(&transport), source_(&source) {}

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

    switch (static_cast<AgentRequestTag>(tagByte)) {
        case AgentRequestTag::GetEngineSnapshot: {
            auto response = encodeEngineSnapshotResponse(
                requestId, source_->engineSnapshot());
            const network::PacketView view{response.data(), response.size()};
            if (transport_->send(from, view)) {
                bytesSent_ += response.size();
            }
            return;
        }
    }
}

} // namespace threadmaxx::studio
