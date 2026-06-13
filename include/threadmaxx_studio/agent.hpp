#pragma once

/// @file agent.hpp
/// @brief ST29 — `StudioAgent`: the game-side endpoint of the M7
/// out-of-process attach pair.
///
/// `StudioAgent` bridges a `network::ITransport` (loopback for tests,
/// UDP for production) and an in-process `IStudioDataSource`. The
/// studio side runs `RemoteDataSource` (ST30) on the other end of the
/// transport: panels see the same `IStudioDataSource` shape regardless
/// of attach mode.
///
/// **Wire format** (host-endian, framed per-packet — no cross-machine
/// portability promised; the agent + studio must be the same
/// architecture, matching the existing `WorldSnapshot` /
/// `editor::RemoteBackend` convention):
///
///     request  := [u8 tag][u32 requestId][payload]
///     response := [u8 tag][u32 requestId][u8 ok][payload]
///
///     AgentRequestTag::GetEngineSnapshot  (0x10)  — no payload
///     AgentResponseTag::EngineSnapshot    (0x90)  — when ok=1:
///         [u64 tick][f64 lastStepSeconds][u8 paused]
///         [u32 systemCount][u32 workerCount]
///
/// Future request / response tags layer in for ST30..ST34 (world
/// snapshots, command tunneling, bandwidth metrics, multi-shard
/// discovery). Tag values are stable within a wire version; bumping
/// `kAgentWireVersion` is the break signal.
///
/// **Embedding `network::ServerSession`** is intentionally deferred:
/// the studio RPC pattern is request/response, while ServerSession
/// is shaped for per-tick Input/Snapshot/Delta. ST32 will layer the
/// handshake + auth on top of this transport-only base.

#include "data_source.hpp"

#include <threadmaxx_network/ids.hpp>
#include <threadmaxx_network/transport.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace threadmaxx::studio {

/// @brief Wire-format version. Callers that store recorded bytes
/// across releases (test fixtures, capture-and-replay) should record
/// this alongside the payload so a future decoder can detect a break.
inline constexpr std::uint32_t kAgentWireVersion = 1;

/// @brief Tags for inbound studio→agent RPC requests.
enum class AgentRequestTag : std::uint8_t {
    GetEngineSnapshot = 0x10,
};

/// @brief Tags for outbound agent→studio RPC responses.
enum class AgentResponseTag : std::uint8_t {
    EngineSnapshot = 0x90,
};

/// @brief Decoded view of a single response frame. Filled from
/// `decodeAgentResponse`. Used by `RemoteDataSource` in ST30 and by
/// the ST29 round-trip test.
struct DecodedAgentResponse {
    AgentResponseTag tag{AgentResponseTag::EngineSnapshot};
    std::uint32_t    requestId{0};
    bool             ok{false};
    /// @brief Populated when `tag == EngineSnapshot && ok`.
    EngineFrameSummary engineSnapshot{};
};

/// @brief Encode a GetEngineSnapshot request. Studio→agent.
[[nodiscard]] std::vector<std::byte>
encodeGetEngineSnapshotRequest(std::uint32_t requestId);

/// @brief Encode an EngineSnapshot response. Agent→studio.
[[nodiscard]] std::vector<std::byte>
encodeEngineSnapshotResponse(std::uint32_t requestId,
                             std::optional<EngineFrameSummary> summary);

/// @brief Decode a single response frame.
/// Returns `nullopt` on truncation or unknown tag.
[[nodiscard]] std::optional<DecodedAgentResponse>
decodeAgentResponse(std::span<const std::byte> bytes);

/// @brief Game-side RPC endpoint. Drains a transport, dispatches
/// inbound requests against a bound `IStudioDataSource`, and ships
/// responses back to the originating peer.
///
/// @thread_safety Single-threaded. The host calls `pump()` from the
/// same thread that owns the data source (typically the sim thread,
/// after every `step()`).
class StudioAgent {
public:
    /// @brief Bind to a transport and data source. Both must outlive
    /// the agent; the agent does not take ownership of either.
    StudioAgent(network::ITransport& transport,
                IStudioDataSource& source) noexcept;

    /// @brief Drain the transport, decode every inbound packet, and
    /// send a response back to its originator. Returns the number of
    /// requests handled this call.
    std::size_t pump();

    /// @brief Cumulative requests handled since construction. Useful
    /// for tests + bandwidth panels (ST33).
    [[nodiscard]] std::size_t requestsHandled() const noexcept {
        return requestsHandled_;
    }

    /// @brief Cumulative response bytes sent since construction.
    [[nodiscard]] std::size_t bytesSent() const noexcept {
        return bytesSent_;
    }

private:
    void handleRequest_(network::PeerId from,
                        std::span<const std::byte> bytes);

    network::ITransport* transport_{nullptr};
    IStudioDataSource*   source_{nullptr};
    std::size_t          requestsHandled_{0};
    std::size_t          bytesSent_{0};
};

} // namespace threadmaxx::studio
