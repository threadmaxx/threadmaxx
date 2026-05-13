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
