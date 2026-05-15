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

    /// §3.6 batch 13 — When `true`, the commit phase runs on the sim
    /// thread in submission order (the historical deterministic
    /// reference path). When `false`, batch 13b's sharded commit
    /// groups commands by destination chunk and commits them on
    /// helper threads.
    ///
    /// Today (batch 13a) only the single-threaded path is implemented;
    /// the flag is the public toggle that batch 13b's sharded path
    /// will key off. Documented as a deterministic fallback knob:
    /// if a divergence is ever discovered in production, flipping
    /// this back to `true` restores the reference behavior.
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
};

} // namespace threadmaxx
