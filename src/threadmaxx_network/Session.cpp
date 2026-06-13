/// @file Session.cpp
/// @brief ServerSession + ClientSession.

#include "threadmaxx_network/session.hpp"

#include "threadmaxx_network/diagnostics.hpp"

#include <array>
#include <utility>

namespace threadmaxx::network {

namespace {

void integrateRemoteSequence(std::uint32_t& remoteSeq,
                             std::uint32_t& remoteAckBits,
                             std::uint32_t incoming) noexcept {
    // Treat sequence numbers as a sliding window.
    if (incoming == remoteSeq) return;
    if (incoming > remoteSeq) {
        const std::uint32_t shift = incoming - remoteSeq;
        if (shift >= 32u) {
            remoteAckBits = 0;
        } else {
            remoteAckBits <<= shift;
            remoteAckBits |= (1u << (shift - 1u));
        }
        remoteSeq = incoming;
    } else {
        const std::uint32_t back = remoteSeq - incoming;
        if (back > 0 && back <= 32u) {
            remoteAckBits |= (1u << (back - 1u));
        }
    }
}

} // namespace

ServerSession::ServerSession(ITransport* transport, NetworkConfig cfg)
    : transport_(transport),
      cfg_(cfg),
      sessionCounter_(cfg.serverSeed != 0 ? cfg.serverSeed : 0xA110CABEull) {}

std::size_t ServerSession::connectedPeerCount() const noexcept {
    std::size_t n = 0;
    for (const auto& kv : peers_) if (kv.second.connected) ++n;
    return n;
}

const PeerState* ServerSession::peer(PeerId p) const noexcept {
    auto it = peers_.find(p.value);
    return it != peers_.end() ? &it->second : nullptr;
}

std::vector<PeerSummary> ServerSession::listPeers() const {
    std::vector<PeerSummary> out;
    out.reserve(peers_.size());
    for (const auto& kv : peers_) {
        const auto& ps = kv.second;
        PeerSummary s{};
        s.peer = ps.peer;
        s.session = ps.session;
        s.connected = ps.connected;
        s.remoteSeq = ps.remoteSeq;
        s.remoteAckBits = ps.remoteAckBits;
        s.localSeq = ps.localSeq;
        const auto itQ = inputQueue_.find(ps.peer.value);
        s.pendingInputCount = (itQ != inputQueue_.end())
            ? static_cast<std::uint32_t>(itQ->second.size())
            : 0u;
        out.push_back(s);
    }
    return out;
}

std::size_t ServerSession::pumpReceive() {
    std::array<ReceivedPacket, 32> tmp{};
    std::size_t handled = 0;
    while (true) {
        const auto n = transport_->receive(tmp);
        if (n == 0) break;
        for (std::size_t i = 0; i < n; ++i) {
            const auto& packet = tmp[i];
            BitReader r{std::span<const std::byte>{
                packet.payload.data(), packet.payload.size()}};
            auto header = readPacketHeader(r);
            if (!header) continue;
            ++handled;
            if (header->version != kProtocolVersion) continue;
            switch (header->type) {
                case PacketType::Hello:
                    handleHello_(*header, packet);
                    break;
                case PacketType::Disconnect: {
                    auto it = peers_.find(packet.peer.value);
                    if (it != peers_.end()) peers_.erase(it);
                    inputQueue_.erase(packet.peer.value);
                    break;
                }
                case PacketType::Input: {
                    auto it = peers_.find(packet.peer.value);
                    if (it != peers_.end()) {
                        integrateRemoteSequence(
                            it->second.remoteSeq,
                            it->second.remoteAckBits,
                            header->sequence);
                    }
                    handleInput_(*header, packet, r);
                    break;
                }
                default: {
                    auto it = peers_.find(packet.peer.value);
                    if (it != peers_.end()) {
                        integrateRemoteSequence(
                            it->second.remoteSeq,
                            it->second.remoteAckBits,
                            header->sequence);
                    }
                    break;
                }
            }
        }
    }
    return handled;
}

void ServerSession::handleInput_(const PacketHeader&,
                                 const ReceivedPacket& src,
                                 BitReader& r) {
    auto it = peers_.find(src.peer.value);
    if (it == peers_.end() || !it->second.connected) return;
    auto payload = readInput(r);
    if (!payload) return;
    auto& q = inputQueue_[src.peer.value];
    // De-duplicate by tick.
    for (const auto& existing : q) {
        if (existing.tick == payload->tick) return;
    }
    // Insert sorted by tick to keep replay order deterministic.
    auto pos = q.begin();
    while (pos != q.end() && pos->tick.value < payload->tick.value) ++pos;
    q.insert(pos, *payload);
    if (onInput_) onInput_(src.peer, *payload);
}

std::span<const InputPayload>
ServerSession::inputsFor(PeerId peer) const noexcept {
    auto it = inputQueue_.find(peer.value);
    if (it == inputQueue_.end()) return {};
    return {it->second.data(), it->second.size()};
}

void ServerSession::releaseInputsUpTo(PeerId peer, TickId tick) {
    auto it = inputQueue_.find(peer.value);
    if (it == inputQueue_.end()) return;
    auto& q = it->second;
    auto end = q.begin();
    while (end != q.end() && end->tick.value <= tick.value) ++end;
    q.erase(q.begin(), end);
    // Also send an Ack so the client can drop retransmits.
    sendAck(peer, tick);
}

bool ServerSession::sendAck(PeerId peer, TickId tick) {
    auto it = peers_.find(peer.value);
    if (it == peers_.end()) return false;
    BitWriter w;
    PacketHeader hdr{};
    hdr.type = PacketType::Ack;
    hdr.session = it->second.session;
    hdr.tick = tick;
    hdr.sequence = ++it->second.localSeq;
    hdr.ack = it->second.remoteSeq;
    hdr.ackBits = it->second.remoteAckBits;
    writePacketHeader(w, hdr);
    const auto bytes = w.finish();
    return transport_->send(peer, PacketView{bytes.data(), bytes.size()});
}

void ServerSession::handleHello_(const PacketHeader&,
                                 const ReceivedPacket& src) {
    if (connectedPeerCount() >= cfg_.maxPeers) return;

    auto& ps = peers_[src.peer.value];
    if (ps.connected) return; // already in session
    ps.peer = src.peer;
    ps.session = nextSessionId();
    ps.connected = true;

    // Reply with Welcome.
    BitWriter w;
    PacketHeader hdr{};
    hdr.type = PacketType::Welcome;
    hdr.session = ps.session;
    hdr.sequence = ++ps.localSeq;
    writePacketHeader(w, hdr);
    WelcomePayload wel{};
    wel.session = ps.session;
    wel.assignedPeer = src.peer;
    wel.serverSalt = sessionCounter_;
    writeWelcome(w, wel);
    const auto bytes = w.finish();
    transport_->send(src.peer, PacketView{bytes.data(), bytes.size()});

    if (onConnected_) onConnected_(ps);
}

void ServerSession::disconnect(PeerId p) {
    auto it = peers_.find(p.value);
    if (it == peers_.end()) return;

    BitWriter w;
    PacketHeader hdr{};
    hdr.type = PacketType::Disconnect;
    hdr.session = it->second.session;
    hdr.sequence = ++it->second.localSeq;
    writePacketHeader(w, hdr);
    const auto bytes = w.finish();
    transport_->send(p, PacketView{bytes.data(), bytes.size()});

    peers_.erase(it);
}

ClientSession::ClientSession(ITransport* transport,
                             PeerId serverPeer,
                             NetworkConfig cfg)
    : transport_(transport),
      cfg_(cfg),
      server_(serverPeer),
      self_(transport->localPeer()) {}

bool ClientSession::sendInput(TickId tick,
                              std::span<const std::byte> bytes) {
    if (!connected_) return false;
    InputPayload p{};
    p.tick = tick;
    p.bytes.assign(bytes.begin(), bytes.end());
    // Retain for retransmit. De-duplicate by tick.
    auto pos = pending_.begin();
    while (pos != pending_.end() && pos->tick.value < tick.value) ++pos;
    if (pos != pending_.end() && pos->tick == tick) {
        *pos = p;
    } else {
        pending_.insert(pos, p);
    }
    BitWriter w;
    PacketHeader hdr{};
    hdr.type = PacketType::Input;
    hdr.session = session_;
    hdr.tick = tick;
    hdr.sequence = ++localSeq_;
    hdr.ack = remoteSeq_;
    hdr.ackBits = remoteAckBits_;
    writePacketHeader(w, hdr);
    writeInput(w, p);
    const auto out = w.finish();
    return transport_->send(server_, PacketView{out.data(), out.size()});
}

std::size_t ClientSession::retransmitPending() {
    std::size_t sent = 0;
    for (const auto& p : pending_) {
        BitWriter w;
        PacketHeader hdr{};
        hdr.type = PacketType::Input;
        hdr.session = session_;
        hdr.tick = p.tick;
        hdr.sequence = ++localSeq_;
        hdr.ack = remoteSeq_;
        hdr.ackBits = remoteAckBits_;
        writePacketHeader(w, hdr);
        writeInput(w, p);
        const auto out = w.finish();
        if (transport_->send(server_, PacketView{out.data(), out.size()})) {
            ++sent;
        }
    }
    return sent;
}

bool ClientSession::beginHandshake(std::uint64_t salt) {
    clientSalt_ = salt;
    BitWriter w;
    PacketHeader hdr{};
    hdr.type = PacketType::Hello;
    hdr.sequence = ++localSeq_;
    writePacketHeader(w, hdr);
    HelloPayload h{};
    h.protocolVersion = kProtocolVersion;
    h.clientSalt = salt;
    writeHello(w, h);
    const auto bytes = w.finish();
    return transport_->send(server_, PacketView{bytes.data(), bytes.size()});
}

std::size_t ClientSession::pumpReceive() {
    std::array<ReceivedPacket, 32> tmp{};
    std::size_t handled = 0;
    while (true) {
        const auto n = transport_->receive(tmp);
        if (n == 0) break;
        for (std::size_t i = 0; i < n; ++i) {
            const auto& packet = tmp[i];
            BitReader r{std::span<const std::byte>{
                packet.payload.data(), packet.payload.size()}};
            auto header = readPacketHeader(r);
            if (!header) continue;
            ++handled;
            if (header->version != kProtocolVersion) continue;
            if (packet.peer != server_) continue;
            integrateRemoteSequence(remoteSeq_, remoteAckBits_,
                                    header->sequence);
            switch (header->type) {
                case PacketType::Welcome: {
                    auto wel = readWelcome(r);
                    if (!wel) break;
                    session_ = wel->session;
                    self_    = wel->assignedPeer;
                    connected_ = true;
                    break;
                }
                case PacketType::Ack: {
                    // Ack covers every pending input with tick <= header->tick.
                    if (header->tick.value > lastAcked_.value) {
                        lastAcked_ = header->tick;
                    }
                    auto end = pending_.begin();
                    while (end != pending_.end() &&
                           end->tick.value <= header->tick.value) ++end;
                    pending_.erase(pending_.begin(), end);
                    break;
                }
                case PacketType::Disconnect:
                    connected_ = false;
                    break;
                default:
                    break;
            }
        }
    }
    return handled;
}

} // namespace threadmaxx::network
