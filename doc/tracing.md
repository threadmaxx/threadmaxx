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

## Adapting to Chrome trace / Tracy

The library deliberately ships only the JSON Lines format. Convert to
the trace format your tool wants on the downstream side:

- **Chrome trace** — emit `{"ph":"X","name":"movement","ts":...,"dur":...}`
  per system per tick from the `systems[].update_s` field.
- **Tracy** — call `ZoneScopedN(...)` around system update / commit
  spans yourself; use the JSON Lines log as an offline backup.

The snapshot is cheap (`std::span` + a handful of POD fields), so
calling `frameSnapshot()` every tick is fine. The serializer does no
allocation beyond the stream's buffer.

## Lifetime caveat

`FrameSnapshot::systems` points into engine-owned memory. It is
invalidated by `registerSystem`, `registerSystemAt`, and `shutdown` —
copy the elements out if you need to retain them across calls. The
typical pattern is to spool the JSON record immediately and discard
the snapshot.
