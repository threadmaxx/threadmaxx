#pragma once

/// @file transport.hpp
/// @brief Transport interface + the loopback in-process backend that
/// every network test runs against.
///
/// The protocol layer above never talks to sockets directly — it
/// posts `PacketView`s through `ITransport` and pulls `ReceivedPacket`s
/// out. UDP / QUIC / loopback all implement the same shape.

#include "ids.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <random>
#include <span>
#include <unordered_map>
#include <vector>

namespace threadmaxx::network {

/// @brief Borrowed view of a packet payload. The transport may copy
/// it before returning from `send`, but doesn't have to retain the
/// buffer after the call.
struct PacketView {
    const std::byte* data{nullptr};
    std::size_t size{0};
};

/// @brief One received packet, with peer-identity + arrival time.
struct ReceivedPacket {
    PeerId peer{};
    std::vector<std::byte> payload{};
    std::uint64_t receiveTimeNs{0};
};

/// @brief Engine-side transport contract. Implementations may buffer
/// internally; `receive` drains the local queue.
class ITransport {
public:
    virtual ~ITransport() = default;

    /// @brief Identify the local peer (e.g. for routing in loopback).
    virtual PeerId localPeer() const noexcept = 0;

    /// @brief Send `packet.size` bytes to `peer`. Returns true if
    /// the transport accepted the bytes for delivery (it may still
    /// drop them downstream — e.g. a lossy loopback profile).
    virtual bool send(PeerId peer, PacketView packet) = 0;

    /// @brief Drain up to `out.size()` packets into `out`. Returns the
    /// count actually written.
    virtual std::size_t receive(std::span<ReceivedPacket> out) = 0;

    /// @brief Pump any internal queues (e.g. socket polls). Loopback
    /// is a no-op since `send` queues directly into the hub.
    virtual void poll() = 0;

    virtual void shutdown() = 0;
};

/// @brief Configurable behaviour of `LoopbackTransport`: lets tests
/// simulate lossy / reordered / duplicated networks deterministically.
struct TransportProfile {
    /// @brief Drop probability per send call. Range [0, 1].
    double lossRate{0.0};

    /// @brief Probability that a packet is delayed until the next
    /// `receive` call. Reordering kicks in when ≥ 2 packets are in
    /// the delay buffer at receive time.
    double reorderRate{0.0};

    /// @brief Probability that a packet is also delivered a second
    /// time on the next receive call (independent of reorder).
    double duplicationRate{0.0};

    /// @brief Deterministic seed. `0` keeps the profile non-random
    /// (lossy actions never fire when all probabilities are zero;
    /// any non-zero seed makes the RNG reproducible).
    std::uint64_t rngSeed{0};
};

/// @brief Shared in-process hub. Every `LoopbackTransport` registers
/// with a `LoopbackHub` on construction; `send` routes packets through
/// the hub's per-peer inbox.
///
/// Designed for tests: deterministic when the profile's seed is fixed,
/// supports many peers in one process.
///
/// @thread_safety The hub is safe to use from multiple threads. Tests
/// that don't need concurrency may ignore the mutex.
class LoopbackHub {
public:
    LoopbackHub() = default;

    /// @brief Issue a fresh PeerId. Always non-zero.
    PeerId allocPeer();

    /// @brief Register an inbox for `peer`. Called by LoopbackTransport.
    void registerInbox(PeerId peer);

    /// @brief Drop the inbox for `peer`. Future sends to `peer`
    /// return false.
    void unregisterInbox(PeerId peer);

    /// @brief Queue `bytes` for delivery to `peer`. The profile may
    /// drop, delay (reorder), or duplicate. `from` is recorded in
    /// the delivered ReceivedPacket. Returns true iff the packet was
    /// accepted for delivery (even if it'll be dropped per profile).
    bool send(PeerId from, PeerId to,
              std::span<const std::byte> bytes,
              TransportProfile& profile,
              std::mt19937_64& rng,
              std::uint64_t nowNs);

    /// @brief Drain the inbox for `peer`. Returns number of packets
    /// written into `out`.
    std::size_t drain(PeerId peer,
                      std::span<ReceivedPacket> out);

    /// @brief How many packets are waiting for `peer`.
    std::size_t inboxSize(PeerId peer) const;

private:
    struct Inbox {
        std::deque<ReceivedPacket> ready;
        std::deque<ReceivedPacket> delayed; // reorder buffer
    };

    mutable std::mutex mtx_;
    std::uint32_t nextPeer_{1};
    std::unordered_map<std::uint32_t, Inbox> inboxes_;
};

/// @brief In-process loopback transport. Uses a shared `LoopbackHub`
/// to route packets between peers. Optional `TransportProfile`
/// simulates loss / reorder / duplication.
class LoopbackTransport final : public ITransport {
public:
    LoopbackTransport(std::shared_ptr<LoopbackHub> hub,
                      TransportProfile profile = {});
    ~LoopbackTransport() override;

    PeerId localPeer() const noexcept override { return self_; }

    bool send(PeerId peer, PacketView packet) override;
    std::size_t receive(std::span<ReceivedPacket> out) override;
    void poll() override;
    void shutdown() override;

    /// @brief Adjust the simulated network profile at runtime.
    void setProfile(TransportProfile profile) noexcept {
        profile_ = profile;
        rng_.seed(profile_.rngSeed != 0 ? profile_.rngSeed : 0xCAFEBABEull);
    }

    const TransportProfile& profile() const noexcept { return profile_; }

private:
    std::shared_ptr<LoopbackHub> hub_;
    PeerId self_;
    TransportProfile profile_;
    std::mt19937_64 rng_;
    bool shutdown_{false};
};

} // namespace threadmaxx::network
