#pragma once

/// @file rollback.hpp
/// @brief Input / snapshot / event history rings used by client
/// prediction (NW8) and lockstep / rollback netcode patterns. The
/// library never owns simulation state — it just retains enough
/// history for game code to rewind + replay.

#include "ids.hpp"
#include "replication.hpp"

#include <cstdint>
#include <deque>
#include <vector>

namespace threadmaxx::network {

struct RollbackConfig {
    /// @brief Ticks of history to retain. Drops the oldest entry FIFO
    /// once the ring fills.
    std::uint32_t historyTicks{120};

    /// @brief Hard upper bound on a single rewind. The game's reconcile
    /// step refuses to rewind further than this.
    std::uint32_t maxRollbackTicks{12};

    /// @brief Keep an event history alongside snapshots. Disable to
    /// save memory if game code doesn't replay events.
    bool keepEventHistory{true};
};

/// @brief One stored input frame.
struct StoredInput {
    PeerId peer{};
    TickId tick{};
    std::vector<std::byte> bytes{};
};

/// @brief One stored snapshot — the per-entity records as the encoder
/// saw them, plus the engine's commitHash if game code supplies one.
struct StoredSnapshot {
    TickId tick{};
    std::vector<EntityRecord> entities{};
    std::uint64_t commitHash{0};
};

/// @brief One stored event — game-specific opaque blob.
struct StoredEvent {
    TickId tick{};
    std::vector<std::byte> bytes{};
};

class RollbackBuffer {
public:
    explicit RollbackBuffer(RollbackConfig cfg = {}) : cfg_(cfg) {}

    const RollbackConfig& config() const noexcept { return cfg_; }

    // ---- inputs ----
    void pushInput(StoredInput input);
    const std::deque<StoredInput>& inputs() const noexcept { return inputs_; }
    std::vector<StoredInput> inputsForTick(TickId tick) const;
    std::size_t inputCount() const noexcept { return inputs_.size(); }

    // ---- snapshots ----
    void pushSnapshot(StoredSnapshot snap);
    const std::deque<StoredSnapshot>& snapshots() const noexcept {
        return snapshots_;
    }
    const StoredSnapshot* snapshotAt(TickId tick) const noexcept;
    std::size_t snapshotCount() const noexcept { return snapshots_.size(); }

    // ---- events ----
    void pushEvent(StoredEvent event);
    const std::deque<StoredEvent>& events() const noexcept { return events_; }
    std::size_t eventCount() const noexcept { return events_.size(); }

    /// @brief Drop everything older than `cutoff`. Called automatically
    /// by push when ticks past `historyTicks` would otherwise be kept.
    void compactBefore(TickId cutoff);

private:
    RollbackConfig cfg_{};
    std::deque<StoredInput> inputs_{};
    std::deque<StoredSnapshot> snapshots_{};
    std::deque<StoredEvent> events_{};
};

} // namespace threadmaxx::network

namespace threadmaxx::network {

inline std::deque<StoredInput>::iterator
findInputInsert(std::deque<StoredInput>& d, TickId tick) {
    auto it = d.begin();
    while (it != d.end() && it->tick.value < tick.value) ++it;
    return it;
}

} // namespace threadmaxx::network
