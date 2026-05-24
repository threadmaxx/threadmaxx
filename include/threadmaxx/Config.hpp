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
};

} // namespace threadmaxx
