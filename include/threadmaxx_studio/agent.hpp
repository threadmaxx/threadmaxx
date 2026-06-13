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
///     AgentRequestTag::SubmitCommand      (0x20)  — payload:
///         [u32 labelLen][utf8 label bytes]
///     AgentResponseTag::CommandResult     (0xA0)  — [u8 ok]
///
/// Future request / response tags layer in for ST32..ST34 (auth,
/// bandwidth metrics, multi-shard discovery). Tag values are stable
/// within a wire version; bumping `kAgentWireVersion` is the break
/// signal.
///
/// **Embedding `network::ServerSession`** is intentionally deferred:
/// the studio RPC pattern is request/response, while ServerSession
/// is shaped for per-tick Input/Snapshot/Delta. ST32 will layer the
/// handshake + auth on top of this transport-only base.

#include "data_source.hpp"

#include <threadmaxx_network/ids.hpp>
#include <threadmaxx_network/interest.hpp>
#include <threadmaxx_network/transport.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace threadmaxx::editor {
class CommandStack;
class IEditCommand;
} // namespace threadmaxx::editor

namespace threadmaxx::studio {

/// @brief Wire-format version. Callers that store recorded bytes
/// across releases (test fixtures, capture-and-replay) should record
/// this alongside the payload so a future decoder can detect a break.
inline constexpr std::uint32_t kAgentWireVersion = 1;

/// @brief Tags for inbound studio→agent RPC requests.
enum class AgentRequestTag : std::uint8_t {
    Authenticate      = 0x01,
    GetEngineSnapshot = 0x10,
    SubmitCommand     = 0x20,
    SetClientFocus    = 0x30,
};

/// @brief Tags for outbound agent→studio RPC responses.
enum class AgentResponseTag : std::uint8_t {
    AuthResult       = 0x81,
    EngineSnapshot   = 0x90,
    CommandResult    = 0xA0,
    FocusAck         = 0xB0,
};

/// @brief Compile-time production-attach toggle. When `NDEBUG` is
/// defined and `THREADMAXX_STUDIO_AGENT_ENABLE_PROD` is NOT, the
/// agent defaults to attach-disabled (every inbound packet is
/// silently dropped). Hosts can override at runtime via
/// `StudioAgent::setAttachEnabled(true)` after confirming the
/// host-side authorization story.
inline constexpr bool kStudioAgentDefaultAttachEnabled =
#if defined(NDEBUG) && !defined(THREADMAXX_STUDIO_AGENT_ENABLE_PROD)
    false;
#else
    true;
#endif

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

/// @brief Encode an Authenticate request. Studio→agent.
[[nodiscard]] std::vector<std::byte>
encodeAuthenticateRequest(std::uint32_t requestId, std::string_view token);

/// @brief Encode an AuthResult response. Agent→studio.
[[nodiscard]] std::vector<std::byte>
encodeAuthResultResponse(std::uint32_t requestId, bool accepted);

/// @brief Encode a SetClientFocus request. Studio→agent. The wire
/// carries the focus position + radius (the peerId field on the wire
/// is always the sending studio's localPeer — the agent overrides it
/// with the actual `from` peer on receipt so a forged peerId can't
/// poison another peer's focus).
[[nodiscard]] std::vector<std::byte>
encodeSetClientFocusRequest(std::uint32_t requestId,
                            const network::ClientFocus& focus);

/// @brief Encode a FocusAck response. Agent→studio.
[[nodiscard]] std::vector<std::byte>
encodeFocusAckResponse(std::uint32_t requestId, bool accepted);

/// @brief Encode a GetEngineSnapshot request. Studio→agent.
[[nodiscard]] std::vector<std::byte>
encodeGetEngineSnapshotRequest(std::uint32_t requestId);

/// @brief Encode an EngineSnapshot response. Agent→studio.
[[nodiscard]] std::vector<std::byte>
encodeEngineSnapshotResponse(std::uint32_t requestId,
                             std::optional<EngineFrameSummary> summary);

/// @brief Encode a SubmitCommand request. Studio→agent.
[[nodiscard]] std::vector<std::byte>
encodeSubmitCommandRequest(std::uint32_t requestId, std::string_view label);

/// @brief Encode a CommandResult response. Agent→studio.
[[nodiscard]] std::vector<std::byte>
encodeCommandResultResponse(std::uint32_t requestId, bool accepted);

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
    /// @brief Factory that produces a fresh `IEditCommand` per
    /// invocation. Registered by label via `registerCommandFactory`;
    /// invoked once per inbound `SubmitCommand(label)` from a remote
    /// studio. Factories may capture host-side handles / state.
    using CommandFactory =
        std::function<std::unique_ptr<editor::IEditCommand>()>;

    /// @brief Bind to a transport and data source. Both must outlive
    /// the agent; the agent does not take ownership of either.
    StudioAgent(network::ITransport& transport,
                IStudioDataSource& source) noexcept;

    /// @brief Runtime toggle for the production attach gate. When
    /// disabled the agent drains the transport but silently discards
    /// every inbound packet — no responses are emitted. Default at
    /// construction is `kStudioAgentDefaultAttachEnabled`.
    void setAttachEnabled(bool enabled) noexcept { attachEnabled_ = enabled; }

    /// @brief Current attach-enabled state.
    [[nodiscard]] bool attachEnabled() const noexcept { return attachEnabled_; }

    /// @brief Configure the per-peer auth token. Empty token = open
    /// (no auth required). Non-empty = peers must send an
    /// `Authenticate(token)` request that exactly matches before any
    /// other request from that peer is accepted. Changing the token
    /// drops every previously-authenticated peer.
    void setAuthToken(std::string token);

    /// @brief Currently-configured auth token. Empty = open.
    [[nodiscard]] std::string_view authToken() const noexcept {
        return authToken_;
    }

    /// @brief True when @p peer has successfully authenticated.
    /// Always true when no token is configured (open mode).
    [[nodiscard]] bool isPeerAuthenticated(network::PeerId peer) const noexcept;

    /// @brief Number of distinct peers currently authenticated.
    [[nodiscard]] std::size_t authenticatedPeerCount() const noexcept {
        return authenticatedPeers_.size();
    }

    /// @brief Latest focus a peer pushed via SetClientFocus, or
    /// `nullptr` if the peer has not pushed one. **ST33 contract**:
    /// the remote IStudioDataSource MUST opt into ClientFocus like any
    /// other peer — no special-cased full-world reads. Future
    /// world-snapshot RPCs consult this map before slicing world
    /// state.
    [[nodiscard]] const network::ClientFocus*
    peerFocus(network::PeerId peer) const noexcept;

    /// @brief Number of peers that have pushed a focus.
    [[nodiscard]] std::size_t peerFocusCount() const noexcept {
        return peerFocuses_.size();
    }

    /// @brief Bind a `CommandStack`. Mutation requests are rejected
    /// (CommandResult ok=0) until a stack is bound. The stack must
    /// outlive the agent. Pass `nullptr` to detach.
    void setCommandStack(editor::CommandStack* stack) noexcept;

    /// @brief Register a label → command factory. Returns true on
    /// fresh insertion, false on overwrite (the previous factory is
    /// replaced). Labels are matched verbatim against the string sent
    /// over the wire.
    bool registerCommandFactory(std::string label, CommandFactory factory);

    /// @brief Number of registered factories. Useful for tests.
    [[nodiscard]] std::size_t commandFactoryCount() const noexcept {
        return commandFactories_.size();
    }

    /// @brief Drain the transport, decode every inbound packet, and
    /// send a response back to its originator. Returns the number of
    /// requests handled this call.
    std::size_t pump();

    /// @brief Cumulative requests handled since construction. Useful
    /// for tests + bandwidth panels (ST33).
    [[nodiscard]] std::size_t requestsHandled() const noexcept {
        return requestsHandled_;
    }

    /// @brief Cumulative commands applied to the `CommandStack`.
    [[nodiscard]] std::size_t commandsApplied() const noexcept {
        return commandsApplied_;
    }

    /// @brief Cumulative commands rejected (no stack bound, unknown
    /// label, factory returned nullptr).
    [[nodiscard]] std::size_t commandsRejected() const noexcept {
        return commandsRejected_;
    }

    /// @brief Cumulative response bytes sent since construction.
    [[nodiscard]] std::size_t bytesSent() const noexcept {
        return bytesSent_;
    }

private:
    void handleRequest_(network::PeerId from,
                        std::span<const std::byte> bytes);
    bool dispatchCommand_(std::string_view label);

    // PeerId is a thin wrapper around uint32_t with operator== but no
    // hash — give the auth set an explicit hasher.
    struct PeerIdHash {
        std::size_t operator()(network::PeerId p) const noexcept {
            return std::hash<std::uint32_t>{}(p.value);
        }
    };

    network::ITransport*  transport_{nullptr};
    IStudioDataSource*    source_{nullptr};
    editor::CommandStack* commandStack_{nullptr};
    std::unordered_map<std::string, CommandFactory> commandFactories_;
    bool                  attachEnabled_{kStudioAgentDefaultAttachEnabled};
    std::string           authToken_;
    std::unordered_set<network::PeerId, PeerIdHash> authenticatedPeers_;
    std::unordered_map<std::uint32_t, network::ClientFocus> peerFocuses_;
    std::size_t           requestsHandled_{0};
    std::size_t           commandsApplied_{0};
    std::size_t           commandsRejected_{0};
    std::size_t           bytesSent_{0};
};

} // namespace threadmaxx::studio
