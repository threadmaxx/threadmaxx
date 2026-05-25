#pragma once

#include <cstdint>

namespace threadmaxx {

/// Engine configuration. Plain struct of knobs; defaults are sensible
/// for an interactive 60 Hz game.
struct Config {
    /// Number of worker threads. `0` = pick a reasonable default based
    /// on hardware concurrency (`max(1, hardware_concurrency - 1)`),
    /// leaving the simulation thread its own core.
    std::uint32_t workerCount = 0;

    /// Fixed simulation step in seconds. 60 Hz by default.
    double fixedStepSeconds = 1.0 / 60.0;

    /// Maximum number of fixed steps that `run()` will execute per
    /// outer iteration when catching up to wall-clock. Prevents the
    /// spiral of death if one frame takes longer than the step.
    std::uint32_t maxStepsPerIteration = 8;

    /// When true, the engine guarantees the same world state for the
    /// same inputs across runs and machines: jobs are committed strictly
    /// in submission order and workers never observe shared mutable
    /// state. Today this is mostly already true; the flag is a
    /// declaration of intent.
    bool deterministic = false;

    /// If true, `run()` sleeps to match wall-clock. If false, `run()`
    /// iterates as fast as possible (useful for tests and offline
    /// simulation).
    bool sleepToPace = true;

    /// Initial entity capacity hint. Storage grows past this on demand.
    std::uint32_t initialEntityCapacity = 1024;

    /// §3.6 batch 13 — When `true` (the default), the commit phase
    /// runs on the sim thread in submission order — the deterministic
    /// reference path. When `false`, batch 13b's sharded commit groups
    /// value-only commands (`SetTransform` / `SetVelocity` /
    /// `SetAcceleration` / `SetUserData`) by destination chunk and
    /// commits them on worker threads; migrate-possible commands stay
    /// serial.
    ///
    /// Both paths produce bit-for-bit identical state — guarded by
    /// the per-tick `EngineStats::commitHash` (§3.6 batch 13a). On
    /// the workloads measured so far in `bench/commit_bench` the
    /// classifier overhead exceeds the parallelism win, so the
    /// default stays `true`; flip it to `false` only after profiling
    /// flags commit as the bottleneck. If a divergence is ever
    /// discovered in production, flipping this back to `true` is the
    /// documented immediate fallback.
    bool singleThreadedCommit = true;

    /// §3.6 batch 13a — Opt-in production diagnostic. When set to
    /// `N > 0`, the engine logs `EngineStats::commitHash` via
    /// `ILogger` at `LogLevel::Info` every `N` ticks. Default `0`
    /// (off, zero cost).
    ///
    /// Use it to catch divergence in shipped builds: two clients run
    /// with the same seed but produce diverging hashes — the first
    /// tick where the logs disagree points at the offending tick. In
    /// CI the same coverage is achieved by `tests/commit_hash_test`,
    /// so production usage of this knob is for incident response,
    /// not normal builds.
    std::uint32_t logCommitHashEvery = 0;

    /// §3.6 batch 30 — Determinism-contract opt-out. When `false`
    /// (the v1.3 default), `EngineStats::commitHash` is the
    /// per-archetype state rollup: at end of step the engine
    /// recomputes a FNV-1a-64 fingerprint per dirty archetype chunk
    /// and combines them (sorted by mask) into the published hash.
    /// Cost is O(dirty chunk bytes) per tick instead of O(commands)
    /// per tick, which lets the commit phase scale with state size
    /// rather than command count.
    ///
    /// When `true`, the v1.x byte-per-command FNV-1a-64 mix is
    /// preserved exactly. Use it ONLY if you have recorded reference
    /// hashes from a v1.x build (replay, lockstep, network diff) and
    /// can't re-record them yet — `doc/migration_v1_2_to_v1_3.md`
    /// documents the transition. The flag is slated for removal one
    /// MINOR cycle after v1.3 ships per the threadmaxx deprecation
    /// policy.
    ///
    /// The two paths produce different hash values for the same
    /// inputs by construction — they're hashing different things.
    /// Both are deterministic across runs and machines for the same
    /// command stream.
    bool legacyCommitHash = false;

    /// SHARDED_OPTIMISATION.md S6 — Per-buffer run-length threshold at
    /// which the commit phase switches a same-(srcArch, dstMask) run
    /// of `CmdAddTag` / `CmdRemoveTag` / `CmdSetHealth` /
    /// `CmdSetFaction` / `CmdSetBoundingVolume` from the per-cmd
    /// `setMaskAndMigrate` path to the batched
    /// `setMaskAndMigrateBatch` path. Default `16` was chosen so the
    /// batch's collect-handles + sort-srcRows overhead is amortised by
    /// the migrate-loop savings. Set to `std::numeric_limits<uint32_t>::
    /// max()` to fully disable batching (used by the A/B bench at
    /// `bench/migration_bench.cpp` to baseline the per-cmd path).
    /// Lowering below `4` is not useful — the per-cmd path's branch
    /// predictor catches up.
    ///
    /// Hash determinism: bit-for-bit identical to the per-cmd path
    /// for the same command stream. Verified by
    /// `tests/migration_batch_test.cpp` (test 6).
    std::uint32_t batchMigrateThreshold = 16;

    /// SHARDED_OPTIMISATION.md S8 — opts the sharded commit path into
    /// record-time per-chunk routing. When `true` (default), the
    /// engine installs a chunk-locator hook on every fresh
    /// `CommandBuffer` it hands to a worker; the buffer populates
    /// per-chunk index buckets at record time, and the commit's
    /// Pass A walks only the migrating-index list (not the full
    /// command stream). When `false`, sharded commit falls back to
    /// the pre-S8 Pass A scan over every command.
    ///
    /// Ignored when `singleThreadedCommit == true` (the serial path
    /// pays no record-time overhead either way).
    ///
    /// Hash determinism: identical commitHash stream with the flag on
    /// or off, verified by `tests/command_buffer_routing_test.cpp`.
    bool recordTimeRouting = true;

    /// SHARDED_OPTIMISATION.md S9 — sim-thread-inline largest bin.
    /// When `true` (default), the sharded commit's Pass C identifies
    /// the single largest large-bin and runs it inline on the sim
    /// thread, submitting only `largeBins − 1` worker jobs. For
    /// balanced workloads (where `largeBins == workerCount`), this
    /// turns "sim waits for workers" into "sim is a peer of workers"
    /// and drops the latch wait to near zero. Set `false` (or
    /// `THREADMAXX_NO_INLINE_LARGEST=1` env in benches) to revert to
    /// the pre-S9 lane (every large bin → worker; sim only handles
    /// small bins then waits).
    ///
    /// Ignored when `singleThreadedCommit == true`; ignored when no
    /// bin qualifies for the job lane (`largeBins == 0`).
    ///
    /// Hash determinism: identical commitHash regardless of the flag.
    /// Bins target disjoint chunks; `finalizeCommitHash` sorts by
    /// `mask.bits()` before folding so execution order is irrelevant.
    bool inlineLargestBin = true;

    /// SHARDED_OPTIMISATION.md S10 — row-split the largest bin.
    /// **Default is `false` and the knob is opt-in.** When `true`,
    /// Pass C row-partitions the single largest large-bin into
    /// `min(workerCount + 1, largestBinSize / kMinBinForJob)` sub-
    /// bins; sub-bin 0 runs inline on the sim thread and the rest go
    /// to workers. Eligibility additionally requires `largeBins == 1`
    /// (the only case where the split adds parallelism — with
    /// multiple large bins, the others remain the critical path).
    ///
    /// **Why off by default**: empirically (3-run averages, S10 bench
    /// with new `setTransform/SingleArch` workload), enabling the
    /// split regresses the canonical case by +207%:
    ///   sharded split-off: 2 217 µs commit (S9 sim-inline path)
    ///   sharded split-on:  6 794 µs commit (Pass C +233%)
    /// Root cause: each cmd carries a `std::visit` + `locate()` cost
    /// at classification time (~25–30 ns/cmd). The actual apply cost
    /// (write Transform via `mut*()`) is only ~13.6 ns/cmd, so the
    /// pre-classification pass doubles the per-cmd memory traffic on
    /// the slot table and command variant — strictly slower than the
    /// S9 sim-inline path that just applies in place. A viable S10
    /// would require record-time row-bucketing (analogous to S8's
    /// chunkBuckets but with row info), which is out of scope for
    /// this batch.
    ///
    /// The implementation, the test (`tests/pass_c_split_test.cpp`),
    /// and this knob are preserved so a future revisit with a
    /// cheaper classifier can re-evaluate without re-implementing
    /// the partitioner. `THREADMAXX_NO_SPLIT_LARGEST=1` env in
    /// benches force-disables (no-op when default is already off).
    ///
    /// Ignored when `singleThreadedCommit == true`; ignored when
    /// `inlineLargestBin == false`; ignored when the largest bin is
    /// below `2 * kMinBinForJob`; ignored when `largeBins != 1`.
    ///
    /// Determinism: each cmd is routed to the sub-bin whose row range
    /// covers its target entity's current row (`loc.row /
    /// rowsPerBin`). Sub-bins target disjoint rows of the same
    /// archetype chunk, so their writes never overlap. Within a sub-
    /// bin, cmds remain in submission order. `finalizeCommitHash`
    /// sorts chunks by `mask.bits()` before folding so the rollup is
    /// independent of sub-bin execution order.
    bool splitLargestBin = false;

    /// SHARDED_OPTIMISATION.md S11 — `JobLatch::wait()` spin budget.
    /// When non-zero, the waiter hot-loops on an atomic "done" flag
    /// for up to `jobLatchSpinIters` iterations (each is a single
    /// `pause` / `yield` instruction, ~2-10 ns on modern hardware)
    /// before falling back to the mutex+CV blocking path. The spin
    /// skips the `cv_.wait()` kernel sleep + wakeup IPI (~5-15 µs
    /// on Linux) when workers finish within the spin budget; the
    /// mutex acquire is still taken on the win path to keep the
    /// destructor synchronized with the worker's final unlock
    /// (otherwise a stack-allocated latch's mutex/CV dtors race
    /// the worker's `count_down` release).
    ///
    /// Default `4096` ≈ 10-40 µs spin budget — short enough that
    /// burning a sim-thread core for the full window is in noise on
    /// any commit-bound workload, conservative enough that worker
    /// runs longer than the budget pay the normal mutex+CV cost.
    /// Empirically (3-run averages, default-on vs `=0`) the win is
    /// `setTransform/MultiArch` commit_us −22% (Pass C wait
    /// 641 µs → 125 µs); `setTransform/Churn` commit_us −9%;
    /// no detectable regression on no-wait workloads (Churn,
    /// SingleArch, ManyTinyBins).
    ///
    /// Set to `0` to disable (legacy mutex+CV-only path; useful for
    /// CPU-conservative builds where dead-CPU spin time is
    /// undesirable). `THREADMAXX_NO_LATCH_SPIN=1` env in benches
    /// force-disables.
    ///
    /// Determinism: identical commitHash with the knob at any
    /// value — the spin only changes the wait/wakeup path, never
    /// observable state. TSAN-clean because the spin path always
    /// re-acquires the mutex before returning, preserving the
    /// happens-before chain through the worker's `lock_guard` in
    /// `count_down`; the cv_.wait fallback path is unchanged.
    std::uint32_t jobLatchSpinIters = 4096;

    /// SHARDED_OPTIMISATION.md S16 — workload-aware auto fallthrough.
    /// When `true`, `commitBuffersSharded` adds a fourth pre-condition
    /// to its early-out check: if the fraction of non-value-only
    /// commands (`globalCount / totalCommands`) meets or exceeds
    /// `workloadAwareGlobalPercent / 100`, the call falls through to
    /// the per-buffer serial commit path. Default `false` preserves
    /// pre-S16 behavior; flip it to `true` to let the engine pick
    /// single-vs-sharded per call based on cheap counters that are
    /// already maintained for free.
    ///
    /// Motivation. The S12 outcome identified that on RPG-mix-shaped
    /// workloads (≈50% global), Pass B time is dominated by the
    /// serial global-lane apply, not by the bucket-walk demotion that
    /// sharding parallelizes. The bucket-walk wins exist (`S13`
    /// landed −37 … −46% on every value-only workload's Pass B) but
    /// don't recover the global-lane cost. A static fallthrough on
    /// `globalCount / totalCommands` lets users opt into sharded mode
    /// without manually classifying every workload — the engine picks
    /// sharded only when its parallelism actually applies.
    ///
    /// Ignored when `singleThreadedCommit == true` (no sharded path
    /// runs). Affects only the per-call mode decision; commitHash
    /// stream is identical to a run with the flag off plus matching
    /// manual `singleThreadedCommit` selection.
    bool workloadAwareCommit = false;

    /// SHARDED_OPTIMISATION.md S16 — global-lane percentage threshold
    /// for the workload-aware fallthrough. When `workloadAwareCommit
    /// == true` AND `globalCount * 100 >= totalCommands *
    /// workloadAwareGlobalPercent`, the call falls through to the
    /// serial commit path. Default `30` was picked from the S16 bench
    /// matrix: RPG-mix (≈50% global) trips it; setTransform variants
    /// (≈0% global) sail through to sharded. Raise to be less
    /// aggressive about falling through; lower to require more
    /// value-only purity before sharding kicks in.
    ///
    /// Ignored when `workloadAwareCommit == false`.
    std::uint32_t workloadAwareGlobalPercent = 30;
};

} // namespace threadmaxx
