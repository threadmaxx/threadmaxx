# Stats & Profiling

@page stats_and_profiling Stats & Profiling

The engine exposes two snapshot structs refreshed at the end of every
`step()`. Both are cheap to copy and contain only POD fields.

## `EngineStats`

```cpp
struct EngineStats {
    std::uint64_t tick;
    double        lastStepSeconds;
    double        avgStepSeconds;           // EWMA, 1/16 decay (~16-step horizon)
    double        commitDurationSeconds;    // wall-clock spent in commit, summed across waves
    std::uint64_t jobsSubmittedLastStep;
    std::uint64_t commandsCommittedLastStep;
    std::size_t   aliveEntities;
    std::uint64_t totalTicks;
    std::uint64_t totalJobsSubmitted;
    std::uint64_t totalCommandsCommitted;
    std::uint64_t commitHash;               // §3.6 batch 13a — FNV-1a-64 over this tick's commits
};
```

Read it with `engine.stats()`.

- `lastStepSeconds` is the wall-clock duration of the most recent `step()`,
  measured from before any system update to after the last commit. It does
  *not* include `submitFrame()` time (that happens after).
- `avgStepSeconds` is the EWMA with a fixed `1/16` weight. It converges
  about three time constants in 50 ticks — fast enough to react to load
  changes, slow enough that one expensive tick doesn't spike the average.
- `commitDurationSeconds` accumulates wall-clock spent applying command
  buffers (across pre/post hooks and all waves). Subtract from
  `lastStepSeconds` to see how much of the tick was actual wave
  execution.
- `jobsSubmittedLastStep` counts each `parallelFor` chunk as one job.
  `single()` does not submit and is not counted.
- `aliveEntities` is the live count after the most recent commit.
- `commitHash` is a running FNV-1a-64 over every applied mutation
  (spawn / destroy / setX / addTag / removeTag / user-component
  blob) in the commit phase. Reset to the offset basis
  (`0xcbf29ce484222325`) at step start; same inputs → same hash,
  across runs and machines. Use it as a deterministic per-tick
  checksum: two engine runs (or two clients in a networked game)
  with the same seed produce the same hash sequence — the first
  mismatch points at the offending tick. A paused tick commits
  nothing and leaves the field at the basis value.

## Catching divergence in production — `Config::logCommitHashEvery`

Setting `Config::logCommitHashEvery = N` (default `0` = off) makes
the engine log `commitHash` via `ILogger` at `LogLevel::Info` every
N ticks. The line format is `commitHash tick=<T> hash=0x<16 hex>`.

Use it to spot non-determinism in shipped builds where attaching a
trace sink isn't practical: two clients with the same input log
their hashes, the first diverging tick locates the bug. Zero cost
when off (`if (N > 0 && totalTicks % N == 0)`).

The same field is emitted by `writeJsonLines` (as
`"commit_hash":"0x…"`) and by `ChromeTraceWriter`'s `step` event's
`args.commit_hash` — pick whichever sink is cheapest to wire into
the build.

## `JobSystemStats`

```cpp
struct JobSystemStats {
    std::uint64_t totalJobs;     // jobs ever submitted to the worker pool
    std::uint64_t ownPops;       // jobs a worker pulled from its own queue
    std::uint64_t stolenJobs;    // jobs a worker stole from a sibling
    std::uint32_t workerCount;   // resolved Config::workerCount
    std::array<std::uint64_t, kJobDurationHistogramBins>
                  jobDurationHistogram;  // lifetime per-job-duration histogram
};
```

Read with `engine.jobSystemStats()`. Lifetime totals — never reset.

- `ownPops + stolenJobs ≈ totalJobs` (subject to in-flight jobs at
  read time).
- A high `stolenJobs / totalJobs` ratio indicates load imbalance —
  workers ran out of own-queue work and had to steal. Either chunk
  grain is too coarse (one chunk monopolized a worker) or total work
  is too thin to fan out cleanly.
- `jobDurationHistogram` is 16 log2-microsecond bins: bin `i` covers
  `[2^i µs, 2^(i+1) µs)` for `i < 15`, and bin 15 catches everything
  `≥ 32 ms`. A healthy wave clusters in a narrow band; a long tail in
  bins 12+ means a few jobs are dominating the tick.

## `SystemStats`

```cpp
struct SystemStats {
    const char*   name;
    double        lastUpdateSeconds;
    double        avgUpdateSeconds;        // same 1/16 EWMA as EngineStats
    std::uint64_t jobsSubmittedLastStep;
    std::uint64_t commandsCommittedLastStep;
    std::uint64_t totalJobsSubmitted;
    std::uint64_t totalCommandsCommitted;
    double        waitSeconds;             // time spent in parallelFor's latch wait
    std::uint32_t peakQueueDepth;          // max JobSystem outstanding seen by this system
};
```

Read it with `engine.systemStats()`. Returns a `std::span<const
SystemStats>` with one entry per registered system in **registration
order**.

- `name` is the pointer returned by `ISystem::name()`. The engine does
  not copy it — keep your `name()` return value's lifetime stable (string
  literals are the convention).
- `lastUpdateSeconds` measures only the system's `update()` call,
  including the time it spends waiting on its `parallelFor` jobs.
- `jobsSubmittedLastStep` / `commandsCommittedLastStep` attribute jobs
  and commits to the system that issued them.
- `waitSeconds` is how much of `lastUpdateSeconds` the system's thread
  sat blocked in `parallelFor`'s latch wait. `lastUpdateSeconds -
  waitSeconds` is a rough estimate of how much the calling thread did
  itself (orchestration / `single()` work). Always
  `≤ lastUpdateSeconds`.
- `peakQueueDepth` is the highest value of the JobSystem's
  outstanding-jobs counter observed immediately after each
  `parallelFor` submit during this system. Compare to
  `JobSystemStats::workerCount`: a peak ≪ workers means the wave is
  starving the pool; ≫ workers means the wave is queue-bound.

The span is engine-owned and invalidated by `registerSystem()` and
`shutdown()`. Copy if you need to retain.

## Building a HUD

```cpp
const auto eng = engine.stats();
std::printf("tick %llu  step=%.3f ms (avg %.3f)  ent=%zu  jobs=%llu  cmds=%llu\n",
            (unsigned long long)eng.tick,
            eng.lastStepSeconds * 1000.0,
            eng.avgStepSeconds  * 1000.0,
            eng.aliveEntities,
            (unsigned long long)eng.jobsSubmittedLastStep,
            (unsigned long long)eng.commandsCommittedLastStep);

for (const auto& s : engine.systemStats()) {
    std::printf("  %-20s  %.3f ms (avg %.3f)  jobs=%llu  cmds=%llu\n",
                s.name,
                s.lastUpdateSeconds * 1000.0,
                s.avgUpdateSeconds  * 1000.0,
                (unsigned long long)s.jobsSubmittedLastStep,
                (unsigned long long)s.commandsCommittedLastStep);
}
```

This gives you a one-screen overview of where the tick is going. The
boids example uses something close to this format for its `[frame]` log
lines.

## What the numbers tell you

- **`SystemStats::lastUpdateSeconds` dominated by one system** — that's
  your hot spot. If the system has a `parallelFor`, lowering grain or
  parallelizing the inner loop may help; if it uses `single()`, see
  whether the body can move to a parallel path.
- **`jobsSubmittedLastStep` very high with small `lastUpdateSeconds`** —
  the work is too fine-grained. Increase `grain` so each job has more
  to do; the per-job overhead is the limit.
- **`commandsCommittedLastStep` very high** — commit phase is bottlenecking
  the tick. Consider whether your systems can write fewer, larger
  commands (e.g. one `setTransform` per entity instead of two).
- **`avgStepSeconds` close to `fixedStepSeconds`** — you're approaching
  the budget for the current step rate. Either reduce work or lower the
  step rate.

## What's not measured (yet)

These would be useful additions and are tracked in
[FUTURE_WORK.md](../FUTURE_WORK.md):

- Memory: peak live entities, capacity vs. used in dense arrays.
- Per-system per-wave wall-clock attribution (today: per-system update
  duration only).

`EngineStats` + per-`SystemStats` + `JobSystemStats` cover the
"what's slow, where's the imbalance, how big is the commit" question.

## Trace-style profiling

Two serializers live in `<threadmaxx/Trace.hpp>` on top of
`engine.frameSnapshot()`:

- `writeJsonLines(os, snapshot)` — one JSON object per tick, newline-
  terminated. Best for offline ingest, replay, custom dashboards.
- `ChromeTraceWriter w(os); w.emit(snapshot);` — a streaming Chrome
  Trace Event Format writer. Construct on a stream once, call `emit`
  every tick; the destructor closes the JSON array. The resulting
  file loads directly in `chrome://tracing` / Perfetto. One `"step"`
  record per tick frames the wave; one record per system shows
  per-system update duration on a stable per-system row.

See [Tracing](tracing.md) for the full surface and examples.
