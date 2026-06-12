#pragma once

/// @file udp_transport.hpp
/// @brief POSIX UDP transport.
///
/// Available on POSIX platforms when
/// `THREADMAXX_NETWORK_HAS_UDP=1` is exported (the CMake build
/// detects a working `<sys/socket.h>` at configure time). Windows
/// (Winsock 2) variant is v1.x.
///
/// Wire protocol layered on top is identical to LoopbackTransport's
/// — the packet stream defined in `packets.hpp` is transport-agnostic.

#include "ids.hpp"
#include "transport.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace threadmaxx::network {

struct UdpEndpoint {
    std::string address; // dotted IPv4 (v1.0); v1.x adds v6
    std::uint16_t port{0};
};

/// @brief Map from a remote `(ip, port)` to a stable `PeerId`. The
/// server consults this; the client uses a single registered server
/// endpoint.
struct UdpPeerTable {
    PeerId resolve(const UdpEndpoint& ep);
    const UdpEndpoint* lookup(PeerId peer) const noexcept;
    PeerId nextPeer() noexcept { return PeerId{++next_}; }

    struct Entry {
        PeerId peer;
        UdpEndpoint endpoint;
    };
    std::vector<Entry> entries;
    std::uint32_t next_{0};
};

class UdpTransport final : public ITransport {
public:
    /// @brief Construct + bind. `bindAddress="0.0.0.0"` listens on all
    /// interfaces. `port=0` lets the OS pick. Returns a non-default
    /// transport whose `bound()` reports true on success.
    static std::unique_ptr<UdpTransport> bind(std::string_view bindAddress,
                                              std::uint16_t port);

    ~UdpTransport() override;

    UdpTransport(const UdpTransport&) = delete;
    UdpTransport& operator=(const UdpTransport&) = delete;

    PeerId localPeer() const noexcept override { return self_; }
    bool send(PeerId peer, PacketView packet) override;
    std::size_t receive(std::span<ReceivedPacket> out) override;
    void poll() override {}
    void shutdown() override;

    /// @brief Register a known remote endpoint and return its PeerId.
    /// Idempotent.
    PeerId registerPeer(const UdpEndpoint& ep);

    /// @brief Currently-bound address (after `bind` resolved port 0).
    std::uint16_t boundPort() const noexcept { return port_; }
    bool bound() const noexcept { return fd_ >= 0; }

private:
    UdpTransport(int fd, std::uint16_t port);

    int fd_{-1};
    std::uint16_t port_{0};
    PeerId self_{};
    UdpPeerTable peers_{};
};

} // namespace threadmaxx::network
