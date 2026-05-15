# Telemetry sinks

@page telemetry Telemetry sinks

Batch 14 (§3.7) ships the *ingestion* side of the telemetry stack:
pluggable sinks the engine streams `FrameSnapshot` records to, a
built-in frame-budget watcher, and a stall watchdog. The
*primitives* (`FrameSnapshot`, `writeJsonLines`,
`ChromeTraceWriter`, per-tick `commitHash`) are covered in
[`tracing.md`](tracing.md) and [`stats_and_profiling.md`](stats_and_profiling.md).

All telemetry features are opt-in. Defaults preserve the prior
behavior bit-for-bit: no sink installed, no watchdog running, no
budget enforcement.

Public header: `<threadmaxx/Telemetry.hpp>`.

## `ITraceSink`

```cpp
class ITraceSink {
public:
    virtual ~ITraceSink() = default;
    virtual void onFrame(const FrameSnapshot& snap) = 0;
    virtual void onShutdown() {}
};
```

`Engine::setTraceSink(ITraceSink*)` installs a sink. The engine
calls `onFrame` once per `Engine::step()`, on the simulation
thread, *after* the frame has been built and published — i.e.
with the same numbers `frameSnapshot()` would return. The
snapshot is borrowed: `snap.systems` points into engine-owned
memory and is only valid for the call's duration. Copy
out-of-call what you need.

`onShutdown` is engine-internal hook for the sink to flush
in-flight I/O / join worker threads. The included `FileTraceSink`
handles teardown in its destructor too, so users typically don't
need to call `onShutdown` manually.

Lifetime: the engine **does not** take ownership of the sink.
Mirror of `setRenderer` / `setLogger` — the sink must outlive the
engine. Per-tick overhead with no sink installed is zero.

## `FileTraceSink`

```cpp
FileTraceSink::Config cfg;
cfg.pathTemplate  = "trace.%N.json";
cfg.rotationBytes = 64u * 1024u * 1024u;   // 64 MiB
FileTraceSink sink(cfg);
engine.setTraceSink(&sink);
```

Each `onFrame` call delegates to an internal `ChromeTraceWriter`
(see [tracing.md](tracing.md)) and writes one `step` record + one
record per system. When the active file exceeds `rotationBytes`,
the sink:

1. Destroys the writer (which writes the closing `]`).
2. Closes the file.
3. Increments the rotation index.
4. Opens the next file (`pathTemplate` with `%N` substituted)
   and constructs a fresh writer (which writes the opening `[`).

Files are standalone valid Chrome trace JSON — load any one in
`chrome://tracing` or Perfetto. Path format:

- `"trace.%N.json"` → `trace.0.json`, `trace.1.json`, ...
- `"/var/log/trace"` → `/var/log/trace.0.json`, ...

All I/O happens on the sim thread inside `onFrame`. Keep
`rotationBytes` reasonable; tiny budgets force frequent rotation
and disk pressure.

Diagnostic accessors:

```cpp
sink.rotationIndex();         // 0-based, current file
sink.bytesWrittenCurrent();   // approximate
```

## `HudTraceSink`

```cpp
HudTraceSink sink;
engine.setTraceSink(&sink);

// Render or HUD thread:
HudTraceSink::LatestTelemetry t;
if (sink.tryGet(t)) {
    drawHud(t.tick, t.lastStepSeconds * 1000.0, t.aliveEntities);
}
```

Seqlock-protected single-writer / multi-reader sink. The
simulation thread is the only writer; any thread can call
`tryGet` to read the most recent published snapshot. The seqlock
ensures torn-write-free reads: a reader catches a `data_` write
in progress (sequence odd), retries up to 16 times, and returns
`false` only if the engine has never published a snapshot yet.

`LatestTelemetry` carries just the headline numbers:

```cpp
struct LatestTelemetry {
    std::uint64_t tick;
    double        lastStepSeconds;
    double        avgStepSeconds;
    double        commitDurationSecs;
    std::uint64_t jobsSubmittedLastStep;
    std::uint64_t commandsCommittedLastStep;
    std::size_t   aliveEntities;
    std::uint64_t commitHash;
    std::uint32_t workerCount;
    std::uint64_t totalJobs;
    std::uint64_t totalCommands;
};
```

A slow reader sees a stale snapshot, never a half-updated one.
That's the documented contract — the sink is intended for a HUD
that polls once per render frame, not a queue.

The sequence counter is `alignas(64)` so the writer's seq bumps
don't share a cache line with the `data_` write.

## `FrameBudgetWatcher`

```cpp
const double targetSeconds = 0.016;   // ~60 FPS budget
engine.registerSystem(std::make_unique<FrameBudgetWatcher>(
    &engine, targetSeconds));

auto sub = engine.events<BudgetExceeded>().subscribeScoped(
    [](const BudgetExceeded& e) {
        std::fprintf(stderr,
            "tick %llu overran budget: %.3f ms > %.3f ms\n",
            (unsigned long long)e.tick,
            e.lastStepSeconds * 1000.0,
            e.targetSeconds * 1000.0);
    });
```

A built-in `ISystem` that reads `engine.stats().lastStepSeconds`
in `postStep` and emits a `BudgetExceeded` event when the
just-finished tick exceeded the target.

```cpp
struct BudgetExceeded {
    std::uint64_t tick;
    double        lastStepSeconds;
    double        targetSeconds;
};
```

Reads / writes are empty, so the watcher lands in any wave
without contention. `postStep` runs in registration order *after*
the last wave's commit, so it observes the just-finished tick;
subscribers drain the event on the *next* tick boundary.

Combine with batch 12's `Engine::setTickBudget` (which actually
skips work when the budget is exceeded) for a complete budget /
report loop.

## Stall watchdog

```cpp
engine.setStallTimeout(0.1);   // 100 ms

auto sub = engine.events<EngineStall>().subscribeScoped(
    [](const EngineStall& e) {
        std::fprintf(stderr,
            "STALL: tick %llu has been running for %.3f s\n",
            (unsigned long long)e.tick, e.durationSeconds);
    });
```

`Engine::setStallTimeout(seconds)` (sim-thread only) installs a
background watchdog thread. Pass `0.0` (the default) to disable;
the engine joins the thread.

How it works:

1. At the top of every `step()` the sim thread publishes the
   step-start clock + active tick number through relaxed atomics
   (`watchdogStepStartNs_`, `watchdogActiveTick_`).
2. The watchdog wakes every `0.25 * timeout` (clamped to a 10ms
   floor) and computes `now - stepStart`.
3. If that exceeds the timeout AND no `EngineStall` has been
   emitted for the active tick yet (CAS-guarded latch), the
   watchdog emits one through `events<EngineStall>()`.
4. After the sim thread finishes the stalled tick, it clears the
   "already announced" latch so the next stalled tick can fire.

The watchdog uses the engine's typed event channel directly. This
is safe specifically because batch 13c made `EventChannel<T>::emit`
lock-free MPSC — emitting from a non-sim thread doesn't require
any extra synchronization. The event drains on the sim thread at
the usual tick boundary; subscribers receive it on the next tick.

```cpp
struct EngineStall {
    std::uint64_t tick;
    double        durationSeconds;
};
```

Per-tick overhead when disabled (the default) is zero. The
watchdog thread costs one sleep+atomic-read per poll cycle when
enabled.

## Putting it together

A typical "ship telemetry from a real game" setup combines:

- `FileTraceSink` writing Chrome traces for post-mortem analysis.
- `HudTraceSink` feeding the in-game profiler overlay.
- `FrameBudgetWatcher` + `Engine::setTickBudget` for budget
  enforcement (the watcher reports, the budget actually skips
  skippable systems).
- `Engine::setStallTimeout` for catastrophic-hang detection.

All four compose; install whichever you need.

## What this batch does NOT do

- It does not aggregate snapshots across multiple engines. Each
  engine gets at most one sink.
- It does not perform compression / network IO. `FileTraceSink`
  writes raw JSON to a local path; if you need over-the-wire
  shipping, wrap a sink yourself.
- It does not promise the sink survives `Engine::shutdown` — the
  engine doesn't take ownership and doesn't propagate
  `onShutdown` to user-installed sinks. The included sinks'
  destructors handle their own cleanup; for a custom sink, flush
  in the destructor.
