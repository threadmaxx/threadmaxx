#pragma once

/// @file diagnostics.hpp
/// @brief Desync detection via per-tick `commitHash`.
///
/// Server and clients periodically exchange a commit hash for each
/// recently-simulated tick. When they disagree, the tracker fires a
/// `DesyncReport` listing the diverging tick and both hashes — game
/// code reacts by requesting a full snapshot resync, kicking the
/// client, or whatever the project requires.
///
/// The library is agnostic to where the hash comes from. Game code
/// supplies one via `SyncTracker::record(tick, hash)` after each
/// tick; the engine's `Engine::stats().commitHash` is the obvious
/// source (see DESIGN_NOTES §8.2).

#include "ids.hpp"

#include <cstdint>
#include <deque>
#include <functional>
#include <optional>

namespace threadmaxx::network {

struct DesyncReport {
    TickId tick{};
    std::uint64_t localHash{0};
    std::uint64_t remoteHash{0};
};

using DesyncCallback = std::function<void(const DesyncReport&)>;

class SyncTracker {
public:
    /// @brief Maximum history retained (matches RollbackConfig's depth
    /// by convention).
    explicit SyncTracker(std::uint32_t historyTicks = 120) noexcept
        : historyTicks_(historyTicks) {}

    void onDesync(DesyncCallback cb) { onDesync_ = std::move(cb); }

    /// @brief Record the local commit hash for a tick. The library
    /// keeps the most recent `historyTicks` entries.
    void recordLocal(TickId tick, std::uint64_t hash);

    /// @brief Record a remote (peer) commit hash for a tick. If a
    /// local hash for the same tick is already known and differs,
    /// fires the desync callback.
    void recordRemote(TickId tick, std::uint64_t hash);

    /// @brief Lookup the local hash for `tick`. Empty when not recorded
    /// or evicted.
    std::optional<std::uint64_t> localHash(TickId tick) const noexcept;

    /// @brief Number of distinct ticks we've recorded a desync for.
    std::uint64_t desyncCount() const noexcept { return desyncCount_; }

    std::size_t historyCount() const noexcept { return history_.size(); }

private:
    struct Entry {
        TickId tick{};
        std::uint64_t hash{0};
    };
    std::uint32_t historyTicks_;
    std::deque<Entry> history_{};
    DesyncCallback onDesync_{};
    std::uint64_t desyncCount_{0};
};

} // namespace threadmaxx::network
