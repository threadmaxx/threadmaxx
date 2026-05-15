#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace threadmaxx {

/// Number of bins in the per-job-duration histogram exposed on
/// @ref JobSystemStats. The bins are log2-spaced in microseconds:
///
///   bin i covers durations in `[2^i, 2^(i+1))` µs for `i < 15`,
///   and bin 15 catches anything `≥ 32 ms`.
///
/// So bin 0 = `[1µs, 2µs)`, bin 9 = `[512µs, 1024µs)`, bin 14 =
/// `[16ms, 32ms)`, bin 15 = `[32ms, ∞)`. Jobs that complete in under
/// 1 µs fall into bin 0.
inline constexpr std::size_t kJobDurationHistogramBins = 16;

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

    /// §3.6 batch 13a — Running FNV-1a-64 over every applied
    /// mutation in this step's commit phase. Updated by
    /// `EngineImpl::commitBuffer` as each command lands. Reset to the
    /// FNV-1a-64 offset basis (`0xcbf29ce484222325`) at step start, so
    /// a step with zero commits leaves this at the basis value.
    ///
    /// Same inputs → same hash, across runs and machines. The hash is
    /// the runtime safety net for the batch 13b sharded commit path:
    /// it converts any sharding bug from a silent state divergence
    /// into a loud first-tick mismatch. Compare client-vs-server in
    /// networked games to detect drift early; tests use it as a
    /// stronger-than-`WorldSnapshot` per-tick checksum.
    std::uint64_t commitHash = 0xcbf29ce484222325ull;
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

    /// Lifetime per-job-duration histogram. Bucket layout is described
    /// at @ref kJobDurationHistogramBins. Useful for spotting "one job
    /// is dominating the wave" — a healthy distribution clusters in a
    /// narrow band; a long tail in bins 12+ means a few jobs are eating
    /// the budget.
    std::array<std::uint64_t, kJobDurationHistogramBins>
        jobDurationHistogram = {};
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

    /// Seconds the system's thread spent inside `parallelFor`'s
    /// completion wait during the most recent step. Subtracted from
    /// @ref lastUpdateSeconds it gives a rough estimate of how much of
    /// `update()` was the calling thread *itself* doing work (vs.
    /// orchestrating workers). Always `<= lastUpdateSeconds`.
    double waitSeconds = 0.0;

    /// Peak number of in-flight jobs visible in the worker pool during
    /// this system's `update()`, sampled immediately after each
    /// `parallelFor` submit. Useful for spotting wave congestion: a
    /// peak ≪ `JobSystemStats::workerCount` means the wave is starving
    /// the pool; ≫ workers means the wave is queue-bound.
    std::uint32_t peakQueueDepth = 0;
};

} // namespace threadmaxx
