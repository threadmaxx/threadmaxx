/// @file SyncTracker.cpp

#include "threadmaxx_network/diagnostics.hpp"

namespace threadmaxx::network {

void SyncTracker::recordLocal(TickId tick, std::uint64_t hash) {
    // Insert in sorted order; replace existing tick.
    auto pos = history_.begin();
    while (pos != history_.end() && pos->tick.value < tick.value) ++pos;
    if (pos != history_.end() && pos->tick == tick) {
        pos->hash = hash;
    } else {
        history_.insert(pos, {tick, hash});
    }
    while (history_.size() > historyTicks_) history_.pop_front();
}

void SyncTracker::recordRemote(TickId tick, std::uint64_t hash) {
    auto local = localHash(tick);
    if (!local) return;
    if (*local == hash) return;
    ++desyncCount_;
    if (onDesync_) onDesync_(DesyncReport{tick, *local, hash});
}

std::optional<std::uint64_t>
SyncTracker::localHash(TickId tick) const noexcept {
    for (const auto& e : history_) {
        if (e.tick == tick) return e.hash;
    }
    return std::nullopt;
}

} // namespace threadmaxx::network
