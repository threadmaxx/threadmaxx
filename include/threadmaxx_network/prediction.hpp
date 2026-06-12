#pragma once

/// @file prediction.hpp
/// @brief Client-side prediction + reconciliation.
///
/// The library is engine-agnostic: it never simulates. Game code
/// supplies two hooks:
///   `SimulateFn(TickId, span<StoredInput>)` — advance one tick from
///     the supplied inputs (modify game-side state).
///   `ApplyConfirmedFn(StoredSnapshot)` — overwrite game-side state
///     with the server-confirmed snapshot before replay.
/// Reconciler walks the rollback buffer, hands the simulator the right
/// frames, and returns whether the prediction was correct.

#include "ids.hpp"
#include "replication.hpp"
#include "rollback.hpp"

#include <cstdint>
#include <functional>
#include <span>

namespace threadmaxx::network {

/// @brief Outcome of one reconcile pass.
enum class ReconcileResult : std::uint8_t {
    /// @brief No confirmation arrived yet; nothing was done.
    Idle,
    /// @brief Confirmation matched the predicted state. No rewind.
    Matched,
    /// @brief Confirmation diverged. Game state has been rewound and
    /// replayed.
    Reconciled,
    /// @brief Server confirmation arrived but is older than the
    /// available history window (`historyTicks` exceeded). Game code
    /// should request a full resync.
    OutOfWindow,
};

/// @brief One snapshot of the game's predicted state at a specific
/// tick — used to compare against server confirmation.
struct PredictedSnapshot {
    TickId tick{};
    std::vector<EntityRecord> entities{};
};

using SimulateFn = std::function<void(TickId,
                                      std::span<const StoredInput>)>;
using ApplyConfirmedFn = std::function<void(const StoredSnapshot&)>;
using CaptureFn = std::function<PredictedSnapshot(TickId)>;

class Reconciler {
public:
    explicit Reconciler(RollbackConfig cfg = {});

    void setSimulate(SimulateFn fn) { simulate_ = std::move(fn); }
    void setApplyConfirmed(ApplyConfirmedFn fn) { applyConfirmed_ = std::move(fn); }
    void setCapture(CaptureFn fn) { capture_ = std::move(fn); }

    /// @brief Called by game code once per tick after the local
    /// simulation has run. Records the input and a snapshot of
    /// predicted state at `tick`.
    void recordTick(TickId tick,
                    std::span<const StoredInput> inputsThisTick);

    /// @brief Called when a server-confirmed snapshot for `tick`
    /// arrives. Compares against the locally-predicted snapshot. On
    /// mismatch, calls `applyConfirmed` then re-simulates every tick
    /// from `tick+1` through `predictedTick()`.
    ReconcileResult onConfirmed(StoredSnapshot confirmed);

    TickId predictedTick() const noexcept { return predicted_; }
    TickId confirmedTick() const noexcept { return confirmed_; }

    /// @brief Most recent result returned by onConfirmed.
    ReconcileResult lastResult() const noexcept { return lastResult_; }

    /// @brief Total reconciles done (across the session). Useful for
    /// HUD diagnostics ("misprediction rate").
    std::uint64_t reconcileCount() const noexcept { return reconciles_; }

    const RollbackBuffer& history() const noexcept { return history_; }

private:
    static bool entitiesEqual(const std::vector<EntityRecord>& a,
                              const std::vector<EntityRecord>& b) noexcept;

    RollbackBuffer history_;
    SimulateFn simulate_{};
    ApplyConfirmedFn applyConfirmed_{};
    CaptureFn capture_{};
    TickId predicted_{};
    TickId confirmed_{};
    ReconcileResult lastResult_{ReconcileResult::Idle};
    std::uint64_t reconciles_{0};
    std::vector<PredictedSnapshot> predictedHistory_{};
};

} // namespace threadmaxx::network
