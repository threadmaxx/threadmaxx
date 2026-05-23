#pragma once

#include "Stats.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace threadmaxx {

/// ADAPTIVE_TUNING.md T4 — one `(systemName, preferredGrain)` entry in a
/// @ref TuningPatch. The engine resolves @c systemName against the
/// registered system list at patch-apply time (case-sensitive byte
/// comparison against @ref ISystem::name); unknown names are silently
/// ignored (logged at @c Warn level via @ref ILogger).
///
/// The override replaces the system's @ref ISystem::preferredGrain
/// return value for subsequent ticks until a later patch overrides it
/// again. The previous value is not restored when the policy is
/// detached — call @ref Engine::setTuningPolicy with `nullptr` and
/// then either re-register the affected systems or apply a patch
/// that explicitly resets the grain to the original value.
struct SystemGrainOverride {
    /// System name, matched against @ref ISystem::name. Owned-string so
    /// the patch survives the lifetime of any borrowed source.
    std::string   systemName;

    /// New preferred grain. Same semantics as
    /// @ref ISystem::preferredGrain: a non-zero value pins the per-system
    /// default chunk size when a `parallelFor` call passes `grain == 0`;
    /// caller-supplied `grain != 0` is unaffected. `0` resets the system
    /// to the engine's automatic 4-chunks-per-worker heuristic.
    std::uint32_t preferredGrain = 0;
};

/// ADAPTIVE_TUNING.md T4 — a bundle of per-system overrides produced by
/// an @ref ITuningPolicy::propose call and applied by the engine at the
/// next tick boundary (before any @ref ISystem::preStep hook runs).
///
/// Mid-wave application is forbidden by the engine — the patch is
/// staged inside the engine's internal state and visible to the next
/// tick's `update`, `preStep`, and `postStep` calls.
///
/// T5 will extend this struct with `SkipBiasOverride` (skippable-system
/// pressure modulation); T6 will add the scripted-replay handle so the
/// same patch stream can be replayed against a networked client.
struct TuningPatch {
    /// Grain overrides applied in vector order. If two entries name the
    /// same system, the last one wins.
    std::vector<SystemGrainOverride> grainOverrides;
};

/// ADAPTIVE_TUNING.md T4 — policy interface for the adaptive runtime
/// tuner. The engine calls @ref observe once per @ref Engine::step
/// AFTER the commit phase and BEFORE the next tick begins, then
/// @ref propose once per step; if the latter returns a patch, the
/// engine stages it and applies it at the next tick boundary before
/// any @ref ISystem::preStep hook runs.
///
/// @par Determinism contract
///      The same input stream + same scripted policy stream produces
///      bit-identical `EngineStats::commitHash` streams. A policy is
///      free to read from @ref EngineStats, @ref SystemStats, and
///      @ref JobSystemStats; reading from any other side channel
///      (wall-clock, thread-local RNG, environment) breaks
///      determinism. T6 will provide the scripted-replay mechanism
///      that bypasses @ref propose entirely.
///
/// @par Lifetime
///      The engine never owns the policy. @ref Engine::setTuningPolicy
///      borrows the pointer; the host owns lifetime. Pass `nullptr`
///      to detach (the default state).
///
/// @par Thread safety
///      Both methods are invoked on the simulation thread inside
///      @ref Engine::step. Worker jobs never see the policy. Policies
///      that maintain internal state may use plain (non-atomic)
///      members.
class ITuningPolicy {
public:
    virtual ~ITuningPolicy() = default;

    /// Engine hand-off of the just-completed step's instrumentation.
    /// Invoked once per @ref Engine::step, AFTER the commit phase
    /// (final stats published, render-frame submitted, event channels
    /// drained) and BEFORE the next tick begins. The spans into
    /// engine-owned storage are valid only for the duration of the
    /// call — copy any retained fields.
    ///
    /// @param engine  Snapshot of @ref EngineStats for the just-finished tick.
    /// @param systems Per-system stats in registration order.
    /// @param jobs    Worker-pool aggregate stats.
    virtual void observe(const EngineStats&            engine,
                         std::span<const SystemStats>  systems,
                         const JobSystemStats&         jobs) = 0;

    /// Optional patch to apply before the next tick. Return `std::nullopt`
    /// (or default-constructed) to leave engine knobs untouched. The
    /// patch is staged when this method returns and applied at the
    /// next tick boundary before @ref ISystem::preStep — the engine
    /// never applies a patch mid-wave.
    virtual std::optional<TuningPatch> propose() = 0;
};

} // namespace threadmaxx
