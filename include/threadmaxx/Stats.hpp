#pragma once

#include <cstddef>
#include <cstdint>

namespace threadmaxx {

/// Snapshot of engine instrumentation, refreshed at the end of each
/// `Engine::step()`. Cheap to copy; read via `Engine::stats()`.
///
/// All `*LastStep` fields describe the most recent step; the EWMA uses
/// a fixed `1/16` weight (~16-step horizon).
struct EngineStats {
    /// Tick this snapshot describes (the tick that just finished). 0
    /// before the first `step()` runs.
    std::uint64_t tick = 0;

    /// Wall-clock duration of the most recent `step()`, in seconds.
    /// Does NOT include `IRenderer::submitFrame()` time.
    double lastStepSeconds = 0.0;

    /// Exponentially-weighted moving average of step duration. Decay
    /// factor is fixed at `1/16`.
    double avgStepSeconds = 0.0;

    /// Wall-clock seconds spent in the commit phase across all waves
    /// during the most recent step. Subtract from `lastStepSeconds` to
    /// see how much of the step was actual wave execution.
    double commitDurationSeconds = 0.0;

    /// Jobs handed to JobSystem during the most recent `step()`. Each
    /// `parallelFor` chunk counts as one; `single()` does not submit.
    std::uint64_t jobsSubmittedLastStep = 0;

    /// Commands committed during the most recent `step()`, summed
    /// across all systems' command buffers.
    std::uint64_t commandsCommittedLastStep = 0;

    /// Live entity count after the most recent commit.
    std::size_t aliveEntities = 0;

    /// Lifetime totals since `initialize()`.
    std::uint64_t totalTicks = 0;
    std::uint64_t totalJobsSubmitted = 0;
    std::uint64_t totalCommandsCommitted = 0;
};

/// Aggregate worker-pool counters. Read via `Engine::jobSystemStats()`.
/// All fields are lifetime totals since the engine was constructed.
///
/// Use these to tune `parallelFor` grain: a high `stolenJobs /
/// totalJobs` ratio means workers were starving and stealing a lot —
/// either the grain is too coarse (one big chunk monopolized a worker)
/// or there isn't enough total work to keep the pool busy.
struct JobSystemStats {
    /// Number of jobs ever submitted to the worker pool. Mirrors
    /// `EngineStats::totalJobsSubmitted` for engine-driven work but
    /// also counts jobs submitted via `JobSystem` directly.
    std::uint64_t totalJobs = 0;

    /// Jobs a worker popped from its own queue. The common-case happy
    /// path.
    std::uint64_t ownPops = 0;

    /// Jobs a worker stole from a sibling's queue. Indicates load
    /// imbalance recovery.
    std::uint64_t stolenJobs = 0;

    /// Number of worker threads (mirrors `Config::workerCount` after
    /// the default has been resolved).
    std::uint32_t workerCount = 0;
};

/// Per-system snapshot. One entry per registered system in
/// registration order; read via `Engine::systemStats()`.
///
/// @warning The returned span is engine-owned and invalidated by
///          `registerSystem()` and `shutdown()`; copy if you need to
///          retain.
struct SystemStats {
    /// The system's `ISystem::name()` return value. The engine does
    /// not copy it — keep the lifetime stable (string literals are
    /// the convention).
    const char* name = nullptr;

    /// Wall-clock duration of the system's `update()` call in the
    /// most recent step. Measured on whichever thread executed the
    /// system.
    double lastUpdateSeconds = 0.0;

    /// EWMA of `lastUpdateSeconds`; same `1/16` decay as
    /// @ref EngineStats::avgStepSeconds.
    double avgUpdateSeconds = 0.0;

    /// Jobs the system handed to JobSystem during the most recent
    /// step. `single()` does not submit and is not counted.
    std::uint64_t jobsSubmittedLastStep = 0;

    /// Commands committed from this system's buffers during the most
    /// recent step.
    std::uint64_t commandsCommittedLastStep = 0;

    /// Lifetime totals since the system was registered.
    std::uint64_t totalJobsSubmitted = 0;
    std::uint64_t totalCommandsCommitted = 0;
};

} // namespace threadmaxx
