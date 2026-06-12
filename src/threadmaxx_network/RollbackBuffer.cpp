/// @file RollbackBuffer.cpp

#include "threadmaxx_network/rollback.hpp"

namespace threadmaxx::network {

namespace {

void enforceCapacity(std::deque<StoredInput>& d, std::uint32_t maxLen) {
    while (d.size() > maxLen) d.pop_front();
}
void enforceCapacity(std::deque<StoredSnapshot>& d, std::uint32_t maxLen) {
    while (d.size() > maxLen) d.pop_front();
}
void enforceCapacity(std::deque<StoredEvent>& d, std::uint32_t maxLen) {
    while (d.size() > maxLen) d.pop_front();
}

} // namespace

void RollbackBuffer::pushInput(StoredInput input) {
    auto pos = findInputInsert(inputs_, input.tick);
    inputs_.insert(pos, std::move(input));
    enforceCapacity(inputs_, cfg_.historyTicks);
}

std::vector<StoredInput>
RollbackBuffer::inputsForTick(TickId tick) const {
    std::vector<StoredInput> out;
    for (const auto& in : inputs_) {
        if (in.tick == tick) out.push_back(in);
    }
    return out;
}

void RollbackBuffer::pushSnapshot(StoredSnapshot snap) {
    auto pos = snapshots_.begin();
    while (pos != snapshots_.end() && pos->tick.value < snap.tick.value) ++pos;
    if (pos != snapshots_.end() && pos->tick == snap.tick) {
        *pos = std::move(snap);
    } else {
        snapshots_.insert(pos, std::move(snap));
    }
    enforceCapacity(snapshots_, cfg_.historyTicks);
}

const StoredSnapshot*
RollbackBuffer::snapshotAt(TickId tick) const noexcept {
    for (const auto& s : snapshots_) {
        if (s.tick == tick) return &s;
    }
    return nullptr;
}

void RollbackBuffer::pushEvent(StoredEvent event) {
    if (!cfg_.keepEventHistory) return;
    auto pos = events_.begin();
    while (pos != events_.end() && pos->tick.value < event.tick.value) ++pos;
    events_.insert(pos, std::move(event));
    enforceCapacity(events_, cfg_.historyTicks);
}

void RollbackBuffer::compactBefore(TickId cutoff) {
    while (!inputs_.empty() && inputs_.front().tick.value < cutoff.value)
        inputs_.pop_front();
    while (!snapshots_.empty() && snapshots_.front().tick.value < cutoff.value)
        snapshots_.pop_front();
    while (!events_.empty() && events_.front().tick.value < cutoff.value)
        events_.pop_front();
}

} // namespace threadmaxx::network
