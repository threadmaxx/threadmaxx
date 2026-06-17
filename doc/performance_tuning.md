# Performance Tuning

@page performance_tuning Performance Tuning

This doc is a tuning reference for projects whose `step()` time has
become a budget concern. It collects the public knobs the engine
exposes — grain hints, commit-path toggles, tick budgets — plus the
benchmark workflow and the telemetry sinks used to spot regressions.
It does not duplicate the per-knob detail in
[Systems & scheduling](systems_and_scheduling.md),
[Configuration & lifecycle](configuration.md), or
[Telemetry sinks](telemetry.md); read those first if you haven't.

The numbers cited come from `bench/rpg_stress_bench` on a dev
workstation at 4 workers — the canonical Phase 8 baseline. Your
machine and workload will differ; what matters is the shape of the
result, not the exact ns/step.

## When to tune

A good first step is to read `EngineStats::lastStepSeconds` and
`SystemStats::lastUpdateSeconds` in your HUD. If the step time fits
your frame budget with margin, do not tune — every tuning knob below
trades simplicity for win-at-scale, and small scenes pay the
simplicity cost without harvesting the win.

The Phase 8 baseline (`bench/rpg_stress_bench` at 100k entities)
showed:

| metric          | v1.1 baseline | v1.2 shipped |
|-----------------|--------------:|-------------:|
| `step` mean     | 15.6 ms       | 4.41 ms      |
| `commit` mean   | 9.95 ms       | 2.14 ms      |
| `update` mean   | 5.90 ms       | 2.43 ms      |

The bulk of the v1.2 win is automatic — no game-side changes needed
— from two engine-side optimizations: `forEachChunk` sub-job
dispatch (§3.4 batch 28) and per-archetype hash rollup (§3.6 batch
30). Your project picks up both by upgrading.

The knobs below are for the remaining headroom past the v1.2
defaults.

## The major knobs

### 1. `ISystem::preferredGrain()`

The default grain heuristic in `pickGrain()` is
`max(64, ceil(N / (workers * 4)))`. Override
`ISystem::preferredGrain()` to substitute a different floor:

```cpp
class MovementSystem : public ISystem {
    std::uint32_t preferredGrain() const noexcept override {
        return 256;   // ~256-entity chunks for this system's work
    }
};
```

`forEachChunk` reads `SystemContext::workerCount()` and applies an
~8× fanout over `preferredGrain()` to size its sub-job budget. The
hint feeds both the chunk-walking and the legacy parallel-vector
paths.

**When to bump it up:** the system's body is short (10s of ns per
entity), the worker pool is starved by job-submit overhead, or
`SystemStats::peakQueueDepth` is much smaller than `workerCount`.
**When to bump it down:** the body is long, individual entities
have variable cost (steal-rebalance helps), or one chunk dominates
the entity count (and you want more, smaller sub-jobs to feed
work-stealing).

`bench/grain_sweep` sweeps this knob and reports `stolenJobs /
totalJobs` alongside p50/p95/p99 — use it before picking a number.

### 2. `Config::singleThreadedCommit`

Default `true`. Setting `false` enables the sharded commit path
(§3.6.3 batch 13c): the sim thread classifies commands into
value-only-fast-path vs. migrating vs. global-lane, then dispatches
one job per archetype chunk for the fast-path bucket.

**The honest current state:** on every measured workload,
`singleThreadedCommit = true` wins. Sharded is the documented
immediate fallback for profile-confirmed contention, not a default.
It exists in case your workload shape exposes commit-thread
contention the synthetic benches haven't.

If you experiment with `false`, compare `EngineStats::commitDurationSeconds`
under both paths over a representative tick range. Don't rely on
single-tick measurements — the classifier overhead has variance that
needs many ticks to average out.

### 3. `Config::legacyCommitHash` — v1.2 contract opt-out

Default `false` (the v1.2 state-rollup path). Set to `true` to
restore v1.1 byte-mix semantics. See
[Migration v1 → v1.2](migration_v1_to_v1_2.md) and
[Migration v1.2 → v1.3](migration_v1_2_to_v1_3.md) for the contract
detail.

This is a **transitional** knob, not a tuning knob: the byte-mix
path is slower on every realistic workload (the C2 budget the v1.2
default attacks). Use it only if you have recorded reference hashes
you can't re-record yet.

### 4. `Engine::setTickBudget` + `SkipPolicy`

Default budget is unlimited (no skipping). `setTickBudget(seconds)`
caps the wall-clock per-tick spend; `setSkipPolicy(SkipPolicy::Budget)`
makes the engine skip `ISystem::skippable()` systems in subsequent
waves once the budget is exceeded.

```cpp
engine.setTickBudget(0.012);                   // 12 ms target
engine.setSkipPolicy(SkipPolicy::Budget);
```

Mark systems as skippable when they are cosmetic, eventually-consistent,
or have known fallback behavior:

```cpp
class HudUpdateSystem : public ISystem {
    bool skippable() const noexcept override { return true; }
};
```

The pattern keeps the frame budget firm by letting non-load-bearing
systems drop frames instead of stretching the loop. Subscribers to
`EventChannel<SystemSkipped>` see exactly which systems dropped and
when — wire that into your telemetry sink to track drop rates.

`SystemContext::shouldYield()` exposes the over-budget flag to
cooperative cancellation inside long `parallelFor` bodies. Poll it
once per inner loop iteration; cost is one atomic load.

For deterministic replay (lockstep / networked games), record the
authoritative server's `SystemSkipped` events and replay them on the
client with `SkipPolicy::Scripted` plus `pushScriptedSkip(tick, name)`.

### 5. `ISystem::preferredWorkerCap()` — per-system fan-out ceiling

`preferredWorkerCap()` returning `N > 0` caps the number of parallel
sub-jobs the engine dispatches for this system's `parallelFor` calls.
The cap is a ceiling, not a floor: small workloads still pick fewer
sub-jobs from the normal grain heuristic. Applies AFTER
`preferredGrain` / caller-supplied `grain` — if either would produce
more sub-jobs than the cap, grain is rounded up so the resulting
sub-job count is at most `preferredWorkerCap()`.

**When to use it.** D12-style profiling shows a system's per-sub-job
work is small enough that `JobLatch` + cv-wakeup overhead dominates
beyond a known worker count. The canonical example was rpg_demo's
`CubeRenderSystem` at 71 workers: ~6 µs/sub-job over a 71-way fan-out
where the apply itself was sub-µs. Capping at 8 workers landed at the
D12 worker-sweep sweet spot.

Default `0` is uncapped (fan out across the full pool). Like
`preferredGrain()` the cap is read once per `update` invocation and
pinned during the wave.

### 6. Sharded-commit micro-knobs (S6 — S16)

These are off-the-hot-path tuning knobs introduced in the
`SHARDED_OPTIMISATION.md` sprint. They only do anything when
`Config::singleThreadedCommit = false`. The defaults are tuned
conservatively from the bench matrix; flip them only when a profile
points specifically at the path each one controls.

- **`batchMigrateThreshold` (S6, default `16`).** Per-buffer
  run-length at which contiguous same-`(srcArch, dstMask)` mask-change
  runs dispatch through `setMaskAndMigrateBatch`. Lowering below `4`
  is not useful — the per-cmd path's branch predictor catches up.
  Setting to `numeric_limits<uint32_t>::max()` fully disables
  batching (the A/B baseline used by `bench/migration_bench`).
- **`recordTimeRouting` (S8, default `true`).** Sharded-on only.
  Toggles record-time per-chunk bucketing in the `CommandBuffer`.
  Off, Pass A scans every command; on, it walks only the
  migrating-index list. Effectively never want to turn this off in
  production — it's the gate that makes sharded commit faster than
  the serial path at all on routing-active workloads.
- **`inlineLargestBin` (S9, default `true`).** Sharded-on only.
  Pass C runs the single largest large-bin inline on sim. On
  balanced workloads turns "sim waits for workers" into
  "sim is a peer of workers"; latch wait drops to near zero.
- **`splitLargestBin` (S10, default `false`, parked).** Off-by-
  default row-partition of the largest bin into sub-bins. Default
  off because the per-cmd classify cost (~25-30 ns/cmd) exceeds
  the apply cost (~13.6 ns/cmd) on every measured workload. The
  partitioner + `tests/pass_c_split_test.cpp` are preserved as the
  fixed point for a future record-time row-bucketing revisit.
- **`jobLatchSpinIters` (S11, default `4096`).** `JobLatch::wait()`
  spins on the atomic "done" flag for up to this many iterations
  before falling back to mutex+CV. Empirically `-22%` on
  `setTransform/MultiArch` commit_us (Pass C wait
  641 µs → 125 µs). Set to `0` for the legacy mutex+CV path on
  CPU-conservative builds.
- **`workloadAwareCommit` (S16, default `false`, opt-in).** Adds a
  fourth pre-condition to `commitBuffersSharded`'s early-out: when
  the global-command fraction meets `workloadAwareGlobalPercent`,
  the call falls through to the per-buffer serial path. Lets the
  engine pick single-vs-sharded per call without manual workload
  classification. The default `workloadAwareGlobalPercent = 30`
  trips on RPG-mix-shaped workloads (≈50% global) and stays out of
  the way for `setTransform`-shaped ones (≈0% global).

Determinism is preserved across every combination. The published
`EngineStats::commitHash` is identical regardless of which knob is on
or off — finalizeCommitHash sorts by `mask.bits()` before folding the
per-archetype rollups, so lane execution order is irrelevant.

### 7. `Config::workerCount`

Default `0` = `max(1, hardware_concurrency - 1)`. The "minus one"
leaves the sim thread its own core; on systems where the sim thread
is pinned (e.g. a console with hot-path L2 affinity) you may want
to set this explicitly.

`bench/job_stealing_bench` characterizes the relationship between
worker count and the steal ratio across grain sizes. Use it before
moving away from the default if you suspect the pool is over- or
under-sized.

## Diagnosing a step-time regression

The recommended workflow:

1. **Capture a baseline FrameSnapshot stream.** Install a
   `FileTraceSink` and run the regressing scene for a few hundred
   ticks. The default sink emits one Chrome trace event per system
   per tick (see [Tracing](tracing.md)).

2. **Open the resulting JSON in `chrome://tracing`** (or
   Perfetto). The per-system bars show where the time is going:
   `update` vs `commit` vs `buildRenderFrame`.

3. **Suspect commit?** Check `EngineStats::commandsCommittedLastStep`.
   If commands are spiking, the regression is upstream of the
   commit path — find the system over-recording.

4. **Suspect a specific system's `update`?** Check that system's
   `SystemStats::waitSeconds` vs. `lastUpdateSeconds`. If
   `waitSeconds` dominates (≥ 80% of `lastUpdateSeconds`), the
   system is wait-bound — likely one big chunk holding up the
   parallel fan-out. Raise `preferredGrain()` or split the chunk
   by adding a dense component that segregates the hot rows.

5. **Suspect commit-hash cost?** If your build uses
   `legacyCommitHash = true`, this is the prime suspect at high
   entity counts — that's the C2 cost the v1.2 default eliminates.
   The migration guide describes the trade-off; `bench/rpg_stress_bench`
   on both flag values shows the magnitude on your hardware.

6. **Suspect wave dispatch?** Don't. `bench/wave_dispatch_bench`
   shows wave overhead is below 0.04% of a 16 ms step even at 32
   waves. If your profile says otherwise, the system inside the
   wave is doing more than its `update()` body advertises.

## The benchmark harness

`bench/` is opt-in via `-DTHREADMAXX_BUILD_BENCHMARKS=ON` and runs
outside CTest (noisy in CI by design). Every bench writes CSV with
the `BenchRow` columns (label, workload, entities, workers, grain,
mean_ns, stddev_ns, p50/95/99_ns, throughput, steal_pct, note).

### Phase 8 lineup

| binary                 | purpose                                          |
|------------------------|--------------------------------------------------|
| `rpg_stress_bench`     | The Phase 8 gate. 100k-entity rpg-shaped tick.   |
| `rpg_stress_probe`     | Per-system breakdown / ablation / commit-volume.  |
| `chunk_iter_bench`     | `forEachWith` / Cached / Chunk on canonical sets.|
| `commit_path_bench`    | Per-variant commit cost.                          |
| `migration_bench`      | Per-archetype-pair migration cost.                |
| `grain_sweep`          | `preferredGrain` sweep across workloads.          |
| `job_stealing_bench`   | Worker × entity × grain steal-ratio matrix.       |
| `wave_dispatch_bench`  | Permanent diagnostic for wave-dispatch overhead.  |

### Diff protocol

```
cmake --build build --target rpg_stress_bench -j
./build/bench/rpg_stress_bench /tmp/before.csv
# ... apply optimization ...
./build/bench/rpg_stress_bench /tmp/after.csv
diff /tmp/before.csv /tmp/after.csv
```

PRs that touch performance cite specific CSV rows. The convention
(documented in `bench/README.md`) is "before / after / Δ" in a
markdown table on the PR body.

## Telemetry sinks for production

For production projects, attach a `FileTraceSink` to a rolling file
budget and review captures from real users. The `HudTraceSink`
seqlock pattern lets a render thread display headline numbers
(step_s, commit_s, jobs, commands, aliveEntities, commitHash)
without disturbing the sim thread.

```cpp
threadmaxx::FileTraceSink fileSink("/tmp/trace.%N.json",
                                   /*rotationBytes=*/ 64 * 1024 * 1024);
fileSink.setAsync(true);    // off-sim-thread writer
engine.setTraceSink(&fileSink);
```

The async writer is opt-in (§3.9.5 batch 20) — `onFrame` deep-copies
the borrowed `FrameSnapshot` span into owned storage, enqueues, and
returns. The internal writer thread drains. Joined on
`setAsync(false)`, `onShutdown`, and `~FileTraceSink`.

For stall-detection, attach the stall watchdog:

```cpp
engine.setStallTimeout(0.250);   // 250 ms tick = stall
```

The watchdog runs on a separate thread and emits `EngineStall`
events via the lock-free MPSC channel (§3.6.3 batch 13c).
Subscribe with `engine.events<EngineStall>().subscribeScoped(...)`
to log them out-of-band.

## Don't chase what isn't there

Three failure modes that have come up in practice:

1. **Optimizing wave-dispatch overhead.** `wave_dispatch_bench`
   shows the ceiling is ~150-200 ns/wave reduction (`OPTIMIZATION_PATH.md
   §3.7`). At 32 waves: 5-6 µs/tick saved on a 16 ms budget.
   Below noise.
2. **Switching to sharded commit on small scenes.** The classifier
   overhead exceeds the parallelism win until command counts are
   well above 5k/tick.
3. **Raising `preferredGrain()` to "reduce overhead".** Raising the
   grain past the entity count makes the system serial. The
   default heuristic already targets the lowest sensible floor; the
   knob is for workloads where you know the body's cost shape.

## Pointers

- [Systems & scheduling](systems_and_scheduling.md) — per-system
  reads/writes, grain hint, skippable, scripted skip queue.
- [Configuration & lifecycle](configuration.md) — every `Config`
  field with its default and meaning.
- [Stats & profiling](stats_and_profiling.md) — what every
  `EngineStats` and `SystemStats` field measures.
- [Telemetry sinks](telemetry.md) — `ITraceSink`, `FileTraceSink`,
  `HudTraceSink`, `FrameBudgetWatcher`, `setStallTimeout`.
- [Tracing](tracing.md) — JSON Lines + Chrome Trace event format.
- [Migration v1 → v1.2](migration_v1_to_v1_2.md) — the v1.2 hash
  contract change and the `legacyCommitHash` opt-out.
- [Migration v1.2 → v1.3](migration_v1_2_to_v1_3.md) — the per-
  archetype-rollup hash contract that `legacyCommitHash = false`
  selects by default.
- `OPTIMIZATION_PATH.md` (top-level) — every Phase 8 batch's
  as-shipped numbers; `bench/profile_report.md` (batch 27) names
  the C1/C2/C3 inefficiencies the v1.2 batches attacked.
- `SHARDED_OPTIMISATION.md` (top-level) — the S6/S8/S9/S10/S11/S16
  batches with their bench tables and the diagnosis behind each
  default.
- `ADAPTIVE_TUNING.md` (top-level) — the Phase-T tuner and
  `preferredWorkerCap()` rationale.
