/// @file UdpTransport.cpp
/// @brief POSIX UDP socket transport.

#include "threadmaxx_network/udp_transport.hpp"

#if defined(__unix__) || defined(__APPLE__)

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <string>

namespace threadmaxx::network {

namespace {

bool sameEndpoint(const UdpEndpoint& a, const UdpEndpoint& b) noexcept {
    return a.port == b.port && a.address == b.address;
}

} // namespace

PeerId UdpPeerTable::resolve(const UdpEndpoint& ep) {
    for (const auto& e : entries) {
        if (sameEndpoint(e.endpoint, ep)) return e.peer;
    }
    Entry e{};
    e.peer = nextPeer();
    e.endpoint = ep;
    entries.push_back(e);
    return e.peer;
}

const UdpEndpoint* UdpPeerTable::lookup(PeerId peer) const noexcept {
    for (const auto& e : entries) {
        if (e.peer == peer) return &e.endpoint;
    }
    return nullptr;
}

UdpTransport::UdpTransport(int fd, std::uint16_t port)
    : fd_(fd), port_(port) {
    // peers_ is value-initialized; allocate `self_` after construction
    // to avoid reading from an uninitialized field in the mem-init list.
    self_ = peers_.nextPeer();
}

UdpTransport::~UdpTransport() {
    if (fd_ >= 0) ::close(fd_);
}

std::unique_ptr<UdpTransport>
UdpTransport::bind(std::string_view bindAddress, std::uint16_t port) {
    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return nullptr;
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags >= 0) ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (bindAddress.empty() || bindAddress == "0.0.0.0") {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else {
        std::string nul{bindAddress};
        if (::inet_pton(AF_INET, nul.c_str(), &addr.sin_addr) != 1) {
            ::close(fd);
            return nullptr;
        }
    }
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(fd);
        return nullptr;
    }
    sockaddr_in resolved{};
    socklen_t resolvedLen = sizeof(resolved);
    ::getsockname(fd, reinterpret_cast<sockaddr*>(&resolved), &resolvedLen);
    const std::uint16_t boundPort = ntohs(resolved.sin_port);
    return std::unique_ptr<UdpTransport>(new UdpTransport(fd, boundPort));
}

PeerId UdpTransport::registerPeer(const UdpEndpoint& ep) {
    return peers_.resolve(ep);
}

bool UdpTransport::send(PeerId peer, PacketView packet) {
    if (fd_ < 0) return false;
    const UdpEndpoint* ep = peers_.lookup(peer);
    if (!ep) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(ep->port);
    if (::inet_pton(AF_INET, ep->address.c_str(), &addr.sin_addr) != 1) {
        return false;
    }
    const ssize_t n = ::sendto(fd_, packet.data, packet.size, 0,
                               reinterpret_cast<sockaddr*>(&addr),
                               sizeof(addr));
    return n == static_cast<ssize_t>(packet.size);
}

std::size_t UdpTransport::receive(std::span<ReceivedPacket> out) {
    if (fd_ < 0) return 0;
    std::size_t written = 0;
    while (written < out.size()) {
        sockaddr_in addr{};
        socklen_t addrLen = sizeof(addr);
        std::byte buf[2048];
        const ssize_t n = ::recvfrom(fd_, buf, sizeof(buf), 0,
                                     reinterpret_cast<sockaddr*>(&addr),
                                     &addrLen);
        if (n <= 0) break;
        UdpEndpoint ep{};
        char dotted[INET_ADDRSTRLEN] = {};
        ::inet_ntop(AF_INET, &addr.sin_addr, dotted, sizeof(dotted));
        ep.address = dotted;
        ep.port = ntohs(addr.sin_port);
        const PeerId from = peers_.resolve(ep);
        ReceivedPacket& rp = out[written++];
        rp.peer = from;
        rp.payload.assign(buf, buf + n);
        rp.receiveTimeNs = 0;
    }
    return written;
}

void UdpTransport::shutdown() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

} // namespace threadmaxx::network

#else

namespace threadmaxx::network {

// Non-POSIX builds compile this TU empty.
PeerId UdpPeerTable::resolve(const UdpEndpoint&) { return PeerId{}; }
const UdpEndpoint* UdpPeerTable::lookup(PeerId) const noexcept { return nullptr; }

std::unique_ptr<UdpTransport>
UdpTransport::bind(std::string_view, std::uint16_t) { return nullptr; }

UdpTransport::UdpTransport(int, std::uint16_t) {}
UdpTransport::~UdpTransport() = default;
PeerId UdpTransport::registerPeer(const UdpEndpoint&) { return PeerId{}; }
bool UdpTransport::send(PeerId, PacketView) { return false; }
std::size_t UdpTransport::receive(std::span<ReceivedPacket>) { return 0; }
void UdpTransport::shutdown() {}

} // namespace threadmaxx::network

#endif
