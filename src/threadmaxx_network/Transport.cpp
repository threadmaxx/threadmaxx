/// @file Transport.cpp
/// @brief LoopbackHub + LoopbackTransport implementation.

#include "threadmaxx_network/transport.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <utility>

namespace threadmaxx::network {

namespace {

std::uint64_t nowNanos() noexcept {
    using clock = std::chrono::steady_clock;
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            clock::now().time_since_epoch()).count());
}

} // namespace

PeerId LoopbackHub::allocPeer() {
    std::lock_guard<std::mutex> lk(mtx_);
    return PeerId{nextPeer_++};
}

void LoopbackHub::registerInbox(PeerId peer) {
    std::lock_guard<std::mutex> lk(mtx_);
    inboxes_.emplace(peer.value, Inbox{});
}

void LoopbackHub::unregisterInbox(PeerId peer) {
    std::lock_guard<std::mutex> lk(mtx_);
    inboxes_.erase(peer.value);
}

bool LoopbackHub::send(PeerId from, PeerId to,
                       std::span<const std::byte> bytes,
                       TransportProfile& profile,
                       std::mt19937_64& rng,
                       std::uint64_t nowNs) {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = inboxes_.find(to.value);
    if (it == inboxes_.end()) return false;

    // Build the canonical received packet up front.
    ReceivedPacket rp{};
    rp.peer = from;
    rp.payload.assign(bytes.begin(), bytes.end());
    rp.receiveTimeNs = nowNs;

    // Loss check.
    if (profile.lossRate > 0.0) {
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        if (dist(rng) < profile.lossRate) {
            return true; // accepted for delivery, but dropped
        }
    }

    // Duplication: enqueue an extra copy on the delayed lane.
    if (profile.duplicationRate > 0.0) {
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        if (dist(rng) < profile.duplicationRate) {
            it->second.delayed.push_back(rp);
        }
    }

    // Reorder: push to the delayed lane (delivered on next drain).
    if (profile.reorderRate > 0.0) {
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        if (dist(rng) < profile.reorderRate) {
            it->second.delayed.push_back(std::move(rp));
            return true;
        }
    }

    it->second.ready.push_back(std::move(rp));
    return true;
}

std::size_t LoopbackHub::drain(PeerId peer,
                               std::span<ReceivedPacket> out) {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = inboxes_.find(peer.value);
    if (it == inboxes_.end()) return 0;
    auto& inbox = it->second;

    // Promote delayed entries into the ready queue (this is what
    // causes them to "arrive later" than originally-ordered ones).
    while (!inbox.delayed.empty()) {
        inbox.ready.push_back(std::move(inbox.delayed.front()));
        inbox.delayed.pop_front();
    }

    std::size_t written = 0;
    while (written < out.size() && !inbox.ready.empty()) {
        out[written++] = std::move(inbox.ready.front());
        inbox.ready.pop_front();
    }
    return written;
}

std::size_t LoopbackHub::inboxSize(PeerId peer) const {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = inboxes_.find(peer.value);
    if (it == inboxes_.end()) return 0;
    return it->second.ready.size() + it->second.delayed.size();
}

LoopbackTransport::LoopbackTransport(std::shared_ptr<LoopbackHub> hub,
                                     TransportProfile profile)
    : hub_(std::move(hub)),
      self_(hub_->allocPeer()),
      profile_(profile),
      rng_(profile.rngSeed != 0 ? profile.rngSeed : 0xCAFEBABEull) {
    hub_->registerInbox(self_);
}

LoopbackTransport::~LoopbackTransport() {
    if (!shutdown_) shutdown();
}

bool LoopbackTransport::send(PeerId peer, PacketView packet) {
    if (shutdown_) return false;
    std::span<const std::byte> bytes{packet.data, packet.size};
    return hub_->send(self_, peer, bytes, profile_, rng_, nowNanos());
}

std::size_t LoopbackTransport::receive(std::span<ReceivedPacket> out) {
    if (shutdown_) return 0;
    return hub_->drain(self_, out);
}

void LoopbackTransport::poll() {
    // Loopback: send is synchronous queuing, drain is the consumer.
    // No async backlog to pump.
}

void LoopbackTransport::shutdown() {
    if (shutdown_) return;
    shutdown_ = true;
    if (hub_) hub_->unregisterInbox(self_);
}

} // namespace threadmaxx::network
