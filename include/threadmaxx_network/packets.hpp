#pragma once

/// @file packets.hpp
/// @brief Packet envelope: type tag + 24-byte fixed header + payload.
///
/// Every packet on the wire is:
///   [ u8 version ]
///   [ u8 type    ]
///   [ u64 session ]
///   [ u32 tick   ]
///   [ u32 sequence ]
///   [ u32 ack     ]
///   [ u32 ackBits ] // bitmap of 32 most-recent acks before `ack`
///   [ payload bytes... ]
///
/// Total header size = 30 bytes. Bytes are little-endian. The
/// payload's interpretation is dispatched on `PacketType`.

#include "bitstream.hpp"
#include "ids.hpp"

#include <cstdint>
#include <optional>
#include <span>

namespace threadmaxx::network {

enum class PacketType : std::uint8_t {
    Hello         = 0,
    Welcome       = 1,
    Input         = 2,
    Snapshot      = 3,
    Delta         = 4,
    Ack           = 5,
    ResyncRequest = 6,
    ResyncReply   = 7,
    Ping          = 8,
    Pong          = 9,
    DesyncReport  = 10,
    Disconnect    = 11,
};

inline constexpr std::uint8_t kProtocolVersion = 1u;

struct PacketHeader {
    std::uint8_t  version{kProtocolVersion};
    PacketType    type{PacketType::Hello};
    SessionId     session{};
    TickId        tick{};
    std::uint32_t sequence{0};
    std::uint32_t ack{0};
    std::uint32_t ackBits{0};
};

inline constexpr std::size_t kPacketHeaderBytes = 30;

inline void writePacketHeader(BitWriter& w, const PacketHeader& h) {
    w.alignToByte();
    w.writeBits(h.version, 8);
    w.writeBits(static_cast<std::uint64_t>(h.type), 8);
    w.writeBits(h.session.value, 64);
    w.writeBits(h.tick.value, 32);
    w.writeBits(h.sequence, 32);
    w.writeBits(h.ack, 32);
    w.writeBits(h.ackBits, 32);
}

inline std::optional<PacketHeader> readPacketHeader(BitReader& r) {
    PacketHeader h{};
    h.version       = static_cast<std::uint8_t>(r.readBits(8));
    h.type          = static_cast<PacketType>(r.readBits(8));
    h.session.value = r.readBits(64);
    h.tick.value    = static_cast<std::uint32_t>(r.readBits(32));
    h.sequence      = static_cast<std::uint32_t>(r.readBits(32));
    h.ack           = static_cast<std::uint32_t>(r.readBits(32));
    h.ackBits       = static_cast<std::uint32_t>(r.readBits(32));
    if (r.exhausted()) return std::nullopt;
    return h;
}

/// @brief Hello payload — sent by a client to begin a session.
struct HelloPayload {
    std::uint32_t protocolVersion{kProtocolVersion};
    std::uint64_t clientSalt{0};
};

inline void writeHello(BitWriter& w, const HelloPayload& p) {
    w.writeBits(p.protocolVersion, 32);
    w.writeBits(p.clientSalt, 64);
}

inline std::optional<HelloPayload> readHello(BitReader& r) {
    HelloPayload p{};
    p.protocolVersion = static_cast<std::uint32_t>(r.readBits(32));
    p.clientSalt      = r.readBits(64);
    if (r.exhausted()) return std::nullopt;
    return p;
}

/// @brief Welcome payload — sent by the server back to the client to
/// accept the session.
struct WelcomePayload {
    SessionId session{};
    PeerId    assignedPeer{};
    std::uint64_t serverSalt{0};
};

inline void writeWelcome(BitWriter& w, const WelcomePayload& p) {
    w.writeBits(p.session.value, 64);
    w.writeBits(p.assignedPeer.value, 32);
    w.writeBits(p.serverSalt, 64);
}

inline std::optional<WelcomePayload> readWelcome(BitReader& r) {
    WelcomePayload p{};
    p.session.value       = r.readBits(64);
    p.assignedPeer.value  = static_cast<std::uint32_t>(r.readBits(32));
    p.serverSalt          = r.readBits(64);
    if (r.exhausted()) return std::nullopt;
    return p;
}

} // namespace threadmaxx::network
