# threadmaxx Sharded Commit Path Optimization Plan

## Goal

Make the sharded commit path faster than the serial path on the workloads that matter, while preserving the current determinism contract.

The roadmap already records the current problem clearly: the sharded commit path is implemented as a three-pass classifier + parallel apply, but benchmark data showed the classifier overhead currently exceeds the parallelism win across the tested workload shapes, so `singleThreadedCommit = true` remains the default. :contentReference[oaicite:0]{index=0} :contentReference[oaicite:1]{index=1}

## Non-negotiables

- Keep `commitHash` identical to the serial path.
- Keep `WorldSnapshot` identical to the serial path.
- Keep `singleThreadedCommit` as the safe fallback.
- Do not introduce any change that makes replay or deterministic debugging harder.
- Do not ship a “parallel” change that only moves overhead around.

## Phase 0 — Measure first

Before changing behavior, instrument the sharded path so we know where the time goes.

Add timing around these segments:

1. classifier pass A: migrating-entity set build
2. classifier pass B: command scan, hashing, serial apply, binning
3. classifier pass C: bin execution and latch wait
4. allocations inside commit
5. time spent waiting for worker jobs
6. bytes moved / number of commands per bin
7. number of migrating entities per tick
8. number of destination chunks touched per tick

Also capture:
- command mix by type
- archetype-pair churn
- chunk size distribution
- number of non-value-only commands vs value-only commands
- number of entities that force the serial lane

## Phase 1 — Build a proper benchmark matrix

Benchmark at least these scenarios:

- small / medium / large entity counts
- low / medium / high archetype churn
- mostly value-only mutations
- mostly mask-changing mutations
- spawn/destroy heavy frames
- many tiny chunks vs a few large chunks
- mixed command streams similar to the RPG demo

Compare:
- serial commit
- current sharded commit
- sharded commit with experimental changes

Use the existing hash checks as a hard gate:
- per-tick `commitHash`
- final `WorldSnapshot`

## Phase 2 — Attack the current hot spots in order

### 2.1 Reduce classifier overhead

The current sharded path pays for a full classification pass before it can do useful work. That is the first thing to minimize.

Actions:
- keep the migrating-entity set as compact as possible
- avoid extra scans of the same command stream
- reduce branches in the command classifier
- make the classifier data-oriented, not variant-heavy

Possible implementation ideas:
- compact command records into a small POD form before classification
- store frequently used flags in a precomputed per-command header
- avoid repeated `std::visit` cost if command kinds can be predecoded once

### 2.2 Make the per-chunk bins cheaper

The roadmap’s sharded path already routes value-only commands into per-destination-chunk bins. Those bins need to be as cheap as possible. :contentReference[oaicite:2]{index=2}

Actions:
- pre-reserve bin storage
- avoid repeated reallocation
- keep per-chunk bins contiguous and cache-friendly
- align bins to reduce false sharing
- prefer linear append over pointer chasing

Good candidate changes:
- `small_vector`-style local storage for tiny bins
- thread-local arena storage for command records
- one compact bin header per destination chunk

### 2.3 Shrink command representation overhead

If the current command record is variant-heavy, it is likely too expensive for the sharded path.

Actions:
- keep the public `CommandBuffer` API unchanged
- internally move toward a compact record layout
- separate “command kind” from payload bytes
- store only the fields needed for classification in the hot path
- avoid repeated dynamic dispatch during commit

This is a strong candidate because the sharded path does more metadata work than the serial path, so the representation itself needs to be cheap.

### 2.4 Reduce synchronization overhead

The sharded path currently has a parallel apply step plus a latch wait. That is fine, but it must only pay for synchronization when the parallel work is actually large enough.

Actions:
- avoid spawning worker jobs for trivially small bins
- keep a serial fast path for tiny bins
- batch worker submissions when the destination chunks are sparse
- reduce cross-thread coordination when the work per bin is tiny

### 2.5 Keep the hash path cheap

The roadmap says the sharded path hashes chunk-local commands at queue time and global commands at apply time to preserve the exact same hash sequence as the serial path. :contentReference[oaicite:3]{index=3}

Do not change the hash contract.
Instead:
- make the hash helper inline and branch-light
- avoid recomputing fields that are already known
- keep value-only and global command hash paths separate but minimal
- measure hash cost independently from apply cost

## Phase 3 — Consider structural improvements only if profiling proves they matter

These are promising, but they should stay behind profiler evidence.

### 3.1 Per-chunk command buffers

The roadmap already identifies this as a later candidate: record-time routing into per-chunk buckets could skip the classifier pass, but it requires redesigning the `CommandBuffer` recording API, so it was deferred until profiling proves the classifier is the bottleneck. :contentReference[oaicite:4]{index=4}

Use this only if:
- classifier overhead is still the dominant cost after Phase 2
- the workload has enough command volume to justify the API change

### 3.2 Read-only world snapshot reuse during commit

The roadmap also points at a wave-scoped read-only snapshot as a future candidate that could let workers reuse chunk pointers more aggressively. :contentReference[oaicite:5]{index=5}

This is useful if the sharded path still spends time re-looking-up storage during commit-time apply.

### 3.3 Better batching by archetype pair

If many entities move between the same source and destination archetypes, batch those moves together more aggressively.

Possible approach:
- group migrations by `(source archetype, destination archetype)`
- apply contiguous copies per group
- preserve deterministic ordering within each group

This is only worth it if archetype churn is a real workload shape.

## Phase 4 — Add an adaptive cutoff, not more complexity by default

If the sharded path remains slower for small or medium workloads, add a runtime cutoff so the engine can choose the better path per tick.

Policy idea:
- serial commit for small command counts or low churn
- sharded commit only when command volume / migration volume exceeds a threshold
- threshold based on measured commit history, not a hardcoded guess

Keep the cutoff:
- deterministic
- logged
- easy to override
- easy to disable

## Phase 5 — Verify with strict gates

A change only counts if all of these pass:

- `commitHash` matches serial for every tick
- final `WorldSnapshot` matches serial
- no regression in small-world workloads
- no regression in spawn/destroy-heavy workloads
- no regression in mixed RPG-like workloads
- no increase in tail latency for the commit phase
- no extra allocations in the hot commit path

## Suggested implementation order

1. instrument the sharded path
2. build benchmarks for real workload shapes
3. compact the command record path
4. reduce bin allocations and false sharing
5. trim classifier branches
6. make tiny bins fall back to serial fast paths
7. profile again
8. only then consider per-chunk command buffers
9. only then consider broader structural changes

## Files likely to touch

- `EngineImpl.cpp` or equivalent commit implementation
- `CommandBuffer.hpp` / command record internals
- `EntityStorage.hpp` / sharded commit helpers if needed
- benchmark files under `bench/`
- commit determinism tests

## Summary

The sharded commit path should not be made “more parallel” first.
It should be made **less expensive to classify, less expensive to batch, and less expensive to synchronize**.

The roadmap already says the current sharded design loses on tested workloads, so the right strategy is:
- measure precisely,
- cut avoidable overhead,
- keep determinism intact,
- and only add deeper structural changes if profiling proves the win. :contentReference[oaicite:6]{index=6}
