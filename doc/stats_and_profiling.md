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

## `JobSystemStats`

```cpp
struct JobSystemStats {
    std::uint64_t totalJobs;     // jobs ever submitted to the worker pool
    std::uint64_t ownPops;       // jobs a worker pulled from its own queue
    std::uint64_t stolenJobs;    // jobs a worker stole from a sibling
    std::uint32_t workerCount;   // resolved Config::workerCount
};
```

Read with `engine.jobSystemStats()`. Lifetime totals — never reset.

- `ownPops + stolenJobs ≈ totalJobs` (subject to in-flight jobs at
  read time).
- A high `stolenJobs / totalJobs` ratio indicates load imbalance —
  workers ran out of own-queue work and had to steal. Either chunk
  grain is too coarse (one chunk monopolized a worker) or total work
  is too thin to fan out cleanly.

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

- Per-job duration histograms (would let us see chunk variance).
- Memory: peak live entities, capacity vs. used in dense arrays.

`EngineStats` + per-`SystemStats` + `JobSystemStats` cover the
"what's slow, where's the imbalance, how big is the commit" question.

## Trace-style profiling

There is no built-in Tracy / Chrome-trace integration yet. The structs
above are designed to be cheap enough to read every tick and emit to
whatever sink you want (CSV, JSON lines, an in-memory ring buffer for a
debug overlay). If you wire it up to a trace consumer, share the patch.
