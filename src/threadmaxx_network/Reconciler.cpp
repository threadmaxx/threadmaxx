/// @file Reconciler.cpp

#include "threadmaxx_network/prediction.hpp"

#include <algorithm>
#include <utility>

namespace threadmaxx::network {

Reconciler::Reconciler(RollbackConfig cfg) : history_(cfg) {}

bool Reconciler::entitiesEqual(const std::vector<EntityRecord>& a,
                               const std::vector<EntityRecord>& b) noexcept {
    if (a.size() != b.size()) return false;
    // Compare by id; assume each side is unsorted but unique.
    for (const auto& ra : a) {
        bool found = false;
        for (const auto& rb : b) {
            if (rb.id != ra.id) continue;
            if (rb.data != ra.data) return false;
            found = true;
            break;
        }
        if (!found) return false;
    }
    return true;
}

void Reconciler::recordTick(TickId tick,
                            std::span<const StoredInput> inputsThisTick) {
    for (const auto& in : inputsThisTick) {
        StoredInput copy = in;
        copy.tick = tick;
        history_.pushInput(std::move(copy));
    }
    if (capture_) {
        PredictedSnapshot ps = capture_(tick);
        ps.tick = tick;
        // Insert sorted; replace any existing entry at the same tick.
        auto pos = predictedHistory_.begin();
        while (pos != predictedHistory_.end() && pos->tick.value < tick.value) ++pos;
        if (pos != predictedHistory_.end() && pos->tick == tick) {
            *pos = std::move(ps);
        } else {
            predictedHistory_.insert(pos, std::move(ps));
        }
        // Cap to historyTicks.
        while (predictedHistory_.size() > history_.config().historyTicks)
            predictedHistory_.erase(predictedHistory_.begin());
    }
    predicted_ = tick;
}

ReconcileResult Reconciler::onConfirmed(StoredSnapshot confirmed) {
    confirmed_ = confirmed.tick;

    // Find the matching predicted snapshot.
    const PredictedSnapshot* pred = nullptr;
    for (const auto& p : predictedHistory_) {
        if (p.tick == confirmed.tick) { pred = &p; break; }
    }
    if (!pred) {
        // We've evicted that tick's prediction from history → out of window.
        lastResult_ = ReconcileResult::OutOfWindow;
        return lastResult_;
    }

    if (entitiesEqual(pred->entities, confirmed.entities)) {
        lastResult_ = ReconcileResult::Matched;
        return lastResult_;
    }

    // Mismatch: rewind + replay.
    ++reconciles_;
    if (applyConfirmed_) applyConfirmed_(confirmed);

    if (simulate_) {
        const TickId start{confirmed.tick.value + 1};
        for (std::uint32_t t = start.value; t <= predicted_.value; ++t) {
            auto inputs = history_.inputsForTick(TickId{t});
            simulate_(TickId{t},
                std::span<const StoredInput>{inputs.data(), inputs.size()});
        }
    }
    // Update predicted snapshots for the replayed range too.
    if (capture_) {
        for (std::uint32_t t = confirmed.tick.value + 1;
             t <= predicted_.value; ++t) {
            PredictedSnapshot fresh = capture_(TickId{t});
            fresh.tick = TickId{t};
            for (auto& p : predictedHistory_) {
                if (p.tick.value == t) { p = std::move(fresh); break; }
            }
        }
    }

    lastResult_ = ReconcileResult::Reconciled;
    return lastResult_;
}

} // namespace threadmaxx::network
