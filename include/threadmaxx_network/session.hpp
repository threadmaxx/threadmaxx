#pragma once

/// @file session.hpp
/// @brief ServerSession + ClientSession — packet routing, handshake,
/// sequence + ack tracking.
///
/// Both sessions own an `ITransport*` (non-owning). The session
/// drives `transport.poll()` + `transport.receive(...)`, dispatches
/// inbound packets on `PacketType`, and feeds outbound packets
/// through `transport.send`. The simulation loop sits above this:
/// game code calls `session.pumpReceive()` once per tick, then queues
/// outbound state via `sendSnapshot` / `sendInput` / etc.

#include "config.hpp"
#include "packets.hpp"
#include "transport.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

namespace threadmaxx::network {

/// @brief Per-peer connection state on the server side.
struct PeerState {
    PeerId    peer{};
    SessionId session{};
    bool      connected{false};

    /// @brief Last `sequence` we received from this peer.
    std::uint32_t remoteSeq{0};
    /// @brief 32-bit ack bitmap of recently-received sequences below
    /// `remoteSeq`.
    std::uint32_t remoteAckBits{0};
    /// @brief Next `sequence` we'll stamp on outbound packets.
    std::uint32_t localSeq{0};
};

class ServerSession {
public:
    ServerSession(ITransport* transport, NetworkConfig cfg = {});

    /// @brief Drain the transport once and dispatch every packet.
    /// Returns the number of packets handled (any type).
    std::size_t pumpReceive();

    /// @brief Number of currently-connected peers.
    std::size_t connectedPeerCount() const noexcept;

    /// @brief Lookup peer state by id; returns nullptr if unknown.
    const PeerState* peer(PeerId p) const noexcept;

    /// @brief NW11 — enumerate every known peer as a `PeerSummary`.
    /// Walks the peers map; one allocation per call (caller owns the
    /// returned vector). Studio's network panel + remote attach use
    /// this; game code typically prefers the per-peer `peer()`
    /// accessor.
    std::vector<struct PeerSummary> listPeers() const;

    /// @brief Set a callback fired after every successful Hello/Welcome
    /// handshake. The argument carries the newly-connected peer.
    void onPeerConnected(std::function<void(const PeerState&)> cb) {
        onConnected_ = std::move(cb);
    }

    /// @brief Set a callback for inbound input packets from connected
    /// clients. Game code uses this to feed inputs into its
    /// simulation queue.
    void onInput(std::function<void(PeerId, const InputPayload&)> cb) {
        onInput_ = std::move(cb);
    }

    /// @brief Drained input queue for `peer`. Each input arrived
    /// once at most (the server side de-duplicates by (peer, tick)).
    std::span<const InputPayload> inputsFor(PeerId peer) const noexcept;

    /// @brief Drop input packets at or below `tick` for `peer`. Used
    /// once game code has committed those inputs into its simulation
    /// (or for memory bound when the client side has acked).
    void releaseInputsUpTo(PeerId peer, TickId tick);

    /// @brief Server-side disconnect. Posts a Disconnect packet and
    /// clears local state.
    void disconnect(PeerId p);

    /// @brief Send an out-of-band ack covering `peer`'s most recent
    /// tick. Useful when the server has no other downstream traffic
    /// to piggy-back on. Returns true on a successful transport
    /// `send` call.
    bool sendAck(PeerId peer, TickId tick);

    /// @brief Currently-installed config.
    const NetworkConfig& config() const noexcept { return cfg_; }

    /// @brief Allocate a fresh SessionId. Exposed for tests.
    SessionId nextSessionId() noexcept {
        ++sessionCounter_;
        return SessionId{sessionCounter_};
    }

private:
    void handleHello_(const PacketHeader& header,
                      const ReceivedPacket& src);
    void handleInput_(const PacketHeader& header,
                      const ReceivedPacket& src,
                      BitReader& r);

    ITransport* transport_;
    NetworkConfig cfg_;
    std::uint64_t sessionCounter_{0};
    std::unordered_map<std::uint32_t, PeerState> peers_;
    std::unordered_map<std::uint32_t, std::vector<InputPayload>> inputQueue_;
    std::function<void(const PeerState&)> onConnected_;
    std::function<void(PeerId, const InputPayload&)> onInput_;
};

/// @brief Client-side handshake + sequence tracking.
class ClientSession {
public:
    ClientSession(ITransport* transport,
                  PeerId serverPeer,
                  NetworkConfig cfg = {});

    /// @brief Send Hello and wait for Welcome. Returns true once
    /// connected (call `pumpReceive` repeatedly until then).
    bool beginHandshake(std::uint64_t clientSalt = 0xDEADBEEFull);

    /// @brief Drain transport, dispatch inbound packets. Returns the
    /// number of packets handled.
    std::size_t pumpReceive();

    /// @brief Queue an input frame for `tick` and post it to the
    /// server. Returns true on a successful transport `send` call.
    /// The frame is retained in the retransmit window until the
    /// server acks (via an inbound Ack packet or piggy-backed
    /// `ack` field on a Snapshot / Delta).
    bool sendInput(TickId tick, std::span<const std::byte> bytes);

    /// @brief Re-transmit every input frame in the retransmit window
    /// whose tick is greater than `lastAcked()`. Hosts call this
    /// once per tick to defeat packet loss.
    /// @return number of frames re-sent.
    std::size_t retransmitPending();

    /// @brief Number of input frames currently held in the retransmit
    /// buffer.
    std::size_t pendingInputCount() const noexcept {
        return pending_.size();
    }

    /// @brief Highest tick the server has acked back (or 0 if none).
    TickId lastAcked() const noexcept { return lastAcked_; }

    /// @brief True after a successful Welcome.
    bool connected() const noexcept { return connected_; }

    SessionId sessionId() const noexcept { return session_; }
    PeerId    selfPeer()  const noexcept { return self_; }
    PeerId    serverPeer() const noexcept { return server_; }

    /// @brief Last sequence we received from the server.
    std::uint32_t remoteSeq() const noexcept { return remoteSeq_; }
    std::uint32_t remoteAckBits() const noexcept { return remoteAckBits_; }
    std::uint32_t localSeq() const noexcept { return localSeq_; }

    /// @brief Internally-bumped salt used for Hello.
    std::uint64_t clientSalt() const noexcept { return clientSalt_; }

private:
    ITransport* transport_;
    NetworkConfig cfg_;
    PeerId server_;
    PeerId self_{};
    SessionId session_{};
    std::uint64_t clientSalt_{0};
    std::uint32_t remoteSeq_{0};
    std::uint32_t remoteAckBits_{0};
    std::uint32_t localSeq_{0};
    bool connected_{false};

    // Retransmit ring keyed by tick value.
    std::vector<InputPayload> pending_{};
    TickId lastAcked_{};
};

} // namespace threadmaxx::network
