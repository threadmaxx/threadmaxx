// SHARDED_OPTIMISATION.md S0 — per-step breakdown of the sharded commit
// path's Pass A / Pass B / Pass C wall-clock and counters.
//
// Filled every `step()` regardless of `Config::singleThreadedCommit`.
// When the sharded path early-outs to serial (e.g. `totalCommands <
// kShardedMinCommands`), `fallbackCalls` is incremented and `nsTotal`
// records the per-buffer serial-commit cost; Pass A/B/C wall-clock
// fields remain zero for that call. When the sharded path runs in
// full, every field is populated.
//
// Per-call totals accumulate across all `commitBuffersSharded` calls in
// a single `step()` (one call per system with a non-empty buffer set).
// The bench at `bench/commit_pass_breakdown.cpp` reads the accumulated
// breakdown after each step.
//
// Overhead: 4–6 `steady_clock::now()` calls per `commitBuffersSharded`
// call (~30–60 ns total), well below per-step measurement noise.
//
// This is the single artifact every post-S0 batch in
// `SHARDED_OPTIMISATION.md` reads to decide its next move.

#pragma once

#include <cstdint>

namespace threadmaxx {

/// Per-step accumulated breakdown of `EngineImpl::commitBuffersSharded`.
/// Reset to zero at the top of every `step()`; readable via
/// `Engine::lastCommitBreakdown()` after the step returns.
struct CommitBreakdown {
    /// Total commands seen across every `commitBuffersSharded` call this
    /// step (includes calls that early-outed to serial).
    std::uint64_t totalCommands = 0;
    /// Subset of `totalCommands` flagged value-only by their buffer's
    /// `valueOnlyCount` tally.
    std::uint64_t totalValueOnly = 0;
    /// Number of entity indices marked in `shardMigratingBitmap_` during
    /// Pass A (deduplicated; one entry per touched index).
    std::uint64_t migratingCount = 0;
    /// Pass B commands that went through `applyCommandImpl` on the sim
    /// thread (the global lane: spawn / destroy / mask-change / value-
    /// only on a migrating entity / stale handle).
    std::uint64_t globalLaneApplied = 0;
    /// Pass B value-only commands that routed to per-chunk bins for
    /// Pass C parallel apply.
    std::uint64_t binnedValueOnly = 0;
    /// `storage.archetypes().chunks().size()` at the start of the most
    /// recent sharded call this step.
    std::uint64_t chunkCount = 0;
    /// Non-empty Pass C bins (across all calls; one entry per call).
    std::uint64_t activeBins = 0;
    /// SHARDED_OPTIMISATION.md S5 — bins applied serially on the sim
    /// thread instead of being dispatched as Pass C jobs. Driven by the
    /// `kMinBinForJob` threshold: a bin with fewer than that many
    /// commands pays more in latch + steal + wake than it earns from a
    /// worker, so it executes inline (often in parallel with workers
    /// already chewing through the large bins). `activeBins -
    /// inlineBinCount` is the count that took the worker dispatch path.
    std::uint64_t inlineBinCount = 0;

    /// Wall-clock ns spent in Pass A (migrating-entity bitmap build).
    std::uint64_t nsPassA = 0;
    /// Wall-clock ns spent in Pass B (classify + global lane apply +
    /// bin value-only).
    std::uint64_t nsPassB = 0;
    /// Wall-clock ns spent in Pass C (parallel bin apply, including the
    /// final `JobLatch::wait`). Equals zero when `activeBins == 0`.
    std::uint64_t nsPassC = 0;
    /// Subset of `nsPassC` spent in `JobLatch::wait` (waiting for
    /// workers to drain the bins).
    std::uint64_t nsLatchWait = 0;
    /// Wall-clock ns spent in the entire sharded path (per-call sum;
    /// includes serial-fallback time when `fallbackCalls > 0`).
    std::uint64_t nsTotal = 0;

    /// Number of calls this step that ran the sharded path in full.
    std::uint32_t shardedCalls = 0;
    /// Number of calls this step that early-outed to the per-buffer
    /// serial commit fallback (small batch, no value-only, or single
    /// archetype).
    std::uint32_t fallbackCalls = 0;
};

} // namespace threadmaxx
