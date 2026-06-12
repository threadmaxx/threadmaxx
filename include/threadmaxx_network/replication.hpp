#pragma once

/// @file replication.hpp
/// @brief Snapshot encoding + delta dirty tracking.
///
/// The library never sees engine state directly — game code provides
/// `EntityWriter` (encode one entity) and `EntityReader` (decode one)
/// functors, plus a stable enumeration of currently-replicated entity
/// ids. The library handles fragmentation, sequencing, and
/// reassembly.

#include "bitstream.hpp"
#include "ids.hpp"
#include "packets.hpp"

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace threadmaxx::network {

/// @brief Maximum payload byte count per snapshot fragment, exclusive
/// of the 30-byte PacketHeader. Picked so 1500-byte ethernet MTU
/// leaves comfortable room for UDP/IP headers.
inline constexpr std::size_t kSnapshotFragmentBytes = 1200;

/// @brief Per-entity wire record. The component blob is opaque.
struct EntityRecord {
    NetEntityId id{};
    std::vector<std::byte> data{};
};

/// @brief Game-side functor: encode one entity into bytes. Called once
/// per replicated entity per snapshot tick.
using EntityWriter = std::function<void(NetEntityId, BitWriter&)>;

/// @brief Game-side functor: decode one entity record received from
/// the server.
using EntityReader = std::function<void(NetEntityId, BitReader&)>;

/// @brief Replication registry — game-side knowledge of which
/// component types replicate, what their codec id is, and which
/// fields of each are included.
///
/// The library uses this for two things in v1.0:
/// 1. tagging the wire schema (so server/client can fail loudly when
///    they disagree),
/// 2. providing a stable bitmask space for per-field dirty tracking
///    (consumed by NW6 deltas).
class ReplicationRegistry {
public:
    /// @brief Register a component type by name + max-field-count.
    /// Returns its codec id. Re-registering a name returns the same
    /// id (idempotent).
    std::uint32_t registerComponent(std::string_view name,
                                    std::uint32_t fieldCount);

    /// @brief Lookup an id by name. 0 = unknown.
    std::uint32_t codecId(std::string_view name) const noexcept;

    /// @brief Number of fields a previously-registered component has.
    std::uint32_t fieldCount(std::uint32_t codecId) const noexcept;

    /// @brief 64-bit schema hash. Server + client compare hashes to
    /// detect schema drift before any replication runs.
    std::uint64_t schemaHash() const noexcept;

    std::size_t componentCount() const noexcept { return entries_.size(); }

private:
    struct Entry {
        std::string name;
        std::uint32_t fieldCount;
    };
    std::vector<Entry> entries_;
    std::unordered_map<std::string, std::uint32_t> byName_;
    mutable std::uint64_t cachedHash_{0};
};

/// @brief Snapshot encoding state. Lives on the server.
class SnapshotEncoder {
public:
    /// @brief Begin a new snapshot for `tick`. Picks a fresh
    /// snapshot id (incremented monotonically).
    void begin(TickId tick);

    /// @brief Record one entity using the caller-supplied writer.
    void addEntity(NetEntityId id, const EntityWriter& writer);

    /// @brief Finalize and split into Snapshot packets. Returns the
    /// per-fragment payloads (header + payload bytes). Each fragment
    /// is at most `kSnapshotFragmentBytes + kPacketHeaderBytes` bytes
    /// in length.
    std::vector<std::vector<std::byte>> finishFragments(SessionId session,
                                                        std::uint32_t& nextSequence);

    std::uint32_t snapshotId() const noexcept { return snapshotId_; }
    TickId tick() const noexcept { return tick_; }
    std::size_t entityCount() const noexcept { return body_.entities.size(); }

private:
    struct Body {
        TickId tick{};
        std::uint32_t snapshotId{0};
        std::vector<EntityRecord> entities{};
    };
    Body body_{};
    std::uint32_t snapshotId_{0};
    TickId tick_{};
    std::uint32_t nextSnapshotId_{1};
};

/// @brief Snapshot decoding state. Lives on the client.
///
/// The client receives fragments out of order; reassembly is keyed by
/// `(snapshotId, fragmentIdx)`. Once every fragment for a snapshot
/// arrives, the decoder calls back into game code via `EntityReader`
/// for every entity in the assembled payload.
class SnapshotDecoder {
public:
    /// @brief Submit one inbound Snapshot packet. Returns true if the
    /// packet completed a snapshot (and `lastCompletedTick()` is now
    /// fresh).
    bool feed(std::span<const std::byte> payload, const EntityReader& reader);

    /// @brief Most recently fully-reassembled tick.
    TickId lastCompletedTick() const noexcept { return lastTick_; }

    /// @brief Number of partial snapshots currently in flight.
    std::size_t pendingSnapshotCount() const noexcept {
        return partials_.size();
    }

private:
    struct Partial {
        std::uint32_t totalFragments{0};
        std::uint32_t collected{0};
        std::vector<std::vector<std::byte>> fragments;
        TickId tick{};
    };
    std::unordered_map<std::uint32_t, Partial> partials_;
    TickId lastTick_{};
};

} // namespace threadmaxx::network
