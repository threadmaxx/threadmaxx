#pragma once

/// @file delta.hpp
/// @brief Entity-level delta encoding on top of `SnapshotEncoder`.
///
/// v1.0 model: the encoder holds a baseline `Snapshot` (the last
/// snapshot the receiver acked). When `produceDelta` runs against a
/// current set of `EntityRecord`s:
///
/// - entities whose bytes match the baseline → omitted,
/// - entities that differ → included in full,
/// - entities present in current but not in baseline → "spawn" entry,
/// - entities present in baseline but not in current → "despawn" entry.
///
/// Per-field bit packing (v1.x) cuts further; v1.0 stops at "send only
/// the entities that changed", which is already a huge win at MMORPG
/// scales (typically 1-2% of entities mutate per tick).

#include "bitstream.hpp"
#include "ids.hpp"
#include "packets.hpp"
#include "replication.hpp"

#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace threadmaxx::network {

/// @brief Encodes Delta packets relative to an acked baseline.
class DeltaEncoder {
public:
    /// @brief Replace the baseline. Called by the server when a client
    /// acks a baseline snapshot.
    void setBaseline(TickId baselineTick,
                     std::vector<EntityRecord> baseline);

    /// @brief Erase the current baseline. The next `produceDelta` call
    /// will emit a full-snapshot fallback.
    void clearBaseline() noexcept {
        baselineTick_ = TickId{};
        baseline_.clear();
        baselineIndex_.clear();
    }

    /// @brief Is a baseline currently installed?
    bool hasBaseline() const noexcept { return !baselineIndex_.empty() || baselineTick_.value != 0; }

    TickId baselineTick() const noexcept { return baselineTick_; }

    /// @brief Produce one delta-packet payload representing `current`
    /// relative to the baseline. The payload is one self-contained
    /// packet (header + body). `current` is taken by const-reference.
    /// `nextSequence` is bumped.
    std::vector<std::byte>
    produceDelta(SessionId session, TickId tick,
                 std::span<const EntityRecord> current,
                 std::uint32_t& nextSequence);

    /// @brief Body counter exposed for tests: how many full / changed /
    /// despawn entries were emitted in the most recent produceDelta.
    std::uint32_t lastChangedCount() const noexcept { return lastChanged_; }
    std::uint32_t lastDespawnCount() const noexcept { return lastDespawn_; }

private:
    TickId baselineTick_{};
    std::vector<EntityRecord> baseline_{};
    std::unordered_map<std::uint64_t, std::size_t> baselineIndex_{};
    std::uint32_t lastChanged_{0};
    std::uint32_t lastDespawn_{0};
};

/// @brief Decodes Delta packets and applies them on top of a baseline.
class DeltaDecoder {
public:
    void setBaseline(TickId baselineTick,
                     std::vector<EntityRecord> baseline);

    void clearBaseline() noexcept {
        baselineTick_ = TickId{};
        baseline_.clear();
        index_.clear();
    }

    /// @brief Feed one delta-packet payload. On success, dispatches
    /// the resulting per-entity decoded records through `reader`.
    bool feed(std::span<const std::byte> payload, const EntityReader& reader);

    /// @brief Most recent tick a delta successfully applied to.
    TickId lastAppliedTick() const noexcept { return lastTick_; }

    /// @brief Current per-entity baseline.
    std::span<const EntityRecord> currentBaseline() const noexcept {
        return {baseline_.data(), baseline_.size()};
    }

private:
    TickId baselineTick_{};
    std::vector<EntityRecord> baseline_{};
    std::unordered_map<std::uint64_t, std::size_t> index_{};
    TickId lastTick_{};
};

} // namespace threadmaxx::network
