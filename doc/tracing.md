# Frame snapshots and tracing

@page tracing Frame snapshots and tracing

`Engine::frameSnapshot()` returns a bundled record of the per-tick
instrumentation:

```cpp
struct FrameSnapshot {
    EngineStats                  engine;
    std::span<const SystemStats> systems;
    JobSystemStats               jobs;
};
```

The fields are the same values `Engine::stats()`, `Engine::systemStats()`
and `Engine::jobSystemStats()` would return individually; bundling them
keeps the caller's record internally consistent (no risk of one stat
lagging another by a step).

## JSON Lines emitter

`writeJsonLines(std::ostream&, const FrameSnapshot&)` from
`<threadmaxx/Trace.hpp>` serializes one snapshot as a single newline-
terminated JSON object:

```cpp
std::ofstream out("frames.jsonl");
// Inside a postStep hook or right after engine.step():
threadmaxx::writeJsonLines(out, engine.frameSnapshot());
```

Field names match the stats structs, so a downstream parser can map
them mechanically:

```json
{"tick":120,"step_s":0.00031,"avg_step_s":0.00029,"commit_s":0.00008,
 "jobs":6,"commands":48,"alive":12,
 "systems":[{"name":"movement","update_s":0.00009,"avg_update_s":0.00008,
             "jobs":2,"commands":12}],
 "job_pool":{"total_jobs":1234,"own_pops":1130,"steals":104,"workers":4}}
```

## Chrome Trace Event Format

`<threadmaxx/Trace.hpp>` also provides a streaming Chrome trace writer
that lands directly in `chrome://tracing` / Perfetto:

```cpp
std::ofstream trace("trace.json");
threadmaxx::ChromeTraceWriter w(trace);
for (int i = 0; i < 600; ++i) {
    engine.step();
    w.emit(engine.frameSnapshot());
}
// w's destructor writes the closing ']'
```

What lands in the file:

- One `{"ph":"X","name":"step","tid":0,...}` record per tick frames
  the whole wave; `dur` is `lastStepSeconds` in microseconds.
- One record per registered system per tick, on a per-system row
  (`tid` is a stable hash of the system's name). `dur` is the system's
  `lastUpdateSeconds`.
- A monotonic fake timeline (`ts`) is generated; the engine snapshot
  doesn't carry a wall-clock anchor, so timestamps are placed
  back-to-back by measured duration. Good for "what's slow"; not
  meaningful for "what was happening at 2:14 PM".

`ChromeTraceWriter` is move-only and one-shot — construct a fresh one
per output file. The output is a valid JSON array; it can be loaded
mid-write (e.g. for live profiling) only after the destructor runs.

## Streaming via `ITraceSink`

The two-call pattern (`engine.step()` then `writer.emit(...)` in
user code) works fine for ad-hoc profiling but doesn't compose
with engine internals — a system that wants to record its own
diagnostics around the step boundary has no hook. Batch 14
ships `Engine::setTraceSink(ITraceSink*)` for that: the engine
calls `onFrame(snap)` on the sim thread after every step and
before the renderer is invoked.

Two built-in sinks ship with the engine: `FileTraceSink` (rolling
Chrome-trace JSON with automatic file rotation) and
`HudTraceSink` (seqlock-protected latest snapshot for HUDs).
There's also a built-in `FrameBudgetWatcher` `ISystem` that
emits `BudgetExceeded` events when a tick overruns a target, and
an `Engine::setStallTimeout(seconds)` watchdog that emits
`EngineStall` events from a dedicated thread.

See [`telemetry.md`](telemetry.md) for the full surface.

## Adapting to Tracy

There's no built-in Tracy integration. The typical pattern is to call
`ZoneScopedN(...)` around system update / commit spans in user code
and use `writeJsonLines` as an offline backup.

The snapshot is cheap (`std::span` + a handful of POD fields), so
calling `frameSnapshot()` every tick is fine. The serializers do no
allocation beyond the stream's buffer.

## Lifetime caveat

`FrameSnapshot::systems` points into engine-owned memory. It is
invalidated by `registerSystem`, `registerSystemAt`, and `shutdown` —
copy the elements out if you need to retain them across calls. The
typical pattern is to spool the JSON record immediately and discard
the snapshot.
