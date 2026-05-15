# threadmaxx Core: Future Performance & Optimization Ideas

This note condenses the performance ideas discussed for `threadmaxx` core, with a bias toward what is most likely to matter for a 3D RPG workload.

The main theme is simple:

- keep the core deterministic,
- keep the public API stable,
- optimize the hot paths that already exist,
- avoid adding more parallelism until profiling says it pays off,
- push feature-specific heavy lifting into sibling libraries when that is a cleaner fit.

## 1. Current direction looks right

The roadmap already has the core architectural wins in place:

- fixed-step simulation
- worker pool / work stealing
- chunked archetype storage
- chunk-oriented iteration
- wave-scoped read-only views
- task-graph scheduling
- lock-free event emission
- commit-hash determinism checks
- optional sharded commit path, with the single-threaded path still the safe default

That means the next gains are probably not from another large concurrency rewrite. The better path is to reduce the amount of work done per tick, per chunk, and per command.

## 2. Highest-value ideas

### 2.1 Make chunk iteration even cheaper

The best place to keep improving is the chunk traversal path.

Focus areas:

- reuse wave-scoped chunk views aggressively
- reduce repeated archetype / chunk lookups inside hot loops
- keep `forEachChunk` as the main path for data-parallel systems
- make `forEachWith` pay as little extra overhead as possible when it still needs to exist

Why this matters:

- this is the hot path for movement, AI, culling, and simulation systems
- the storage refactor only pays off if iteration stays simple and cache-friendly

Practical follow-up ideas:

- benchmark `forEachChunk` vs `forEachWith` vs cached queries on realistic scene shapes
- reduce per-chunk indirection in the query API
- consider more internal caching around archetype masks and chunk lists if profiling shows repeated lookup overhead

### 2.2 Keep the commit path simple unless profiling proves otherwise

The roadmap already explored sharded commit, measured it, and found that the classifier overhead currently outweighs the win on tested workloads. That strongly suggests the default should remain the single-threaded commit path for now.

Good next steps:

- reduce allocations and variant overhead in command processing
- keep commit hashing cheap
- keep command application branch-light
- avoid introducing more commit-side parallel splitting until a real game workload proves it is worth it

Potential improvements:

- linearize command recording internally to reduce vector/variant overhead
- keep migration and mutation fast on the single-threaded path
- consider per-chunk command buffering only if profiling shows commit classification is a real bottleneck

### 2.3 Continue reducing contention on shared runtime data

The roadmap already removed some major contention points, but there is still room to shave overhead in places that are touched every tick.

Good places to watch:

- event channels
- resource loader stats and reload paths
- telemetry ingestion
- trace sinks
- debug overlays and diagnostics

Possible improvements:

- keep lock-free or low-contention designs where they are already working
- avoid turning rare control-path operations into frequent hot-path synchronization
- use atomics only where they replace a real bottleneck, not by default

### 2.4 Make job granularity tunable and measurable

The scheduling layer now has task tags, preferred grain hints, and a wave-aware DAG. That opens the door for fine tuning rather than structural changes.

Ideas worth keeping:

- treat `preferredGrain` as a real tuning knob
- benchmark per-system grain sizes on representative scenes
- watch for large chunks that create imbalance or small chunks that create too much overhead
- keep recursive splitting or work sharing as a profiling-driven optimization, not as a blanket rule

This is the right kind of optimization to revisit if a few big chunks dominate frame time or if worker utilization becomes uneven.

## 3. Good candidate optimizations, but only if profiling says they matter

### 3.1 Bulk command buffer compression

If command recording shows up as a cost, make the command buffer representation more compact.

Possible shape:

- linear arena-backed command storage
- compact tags + payloads instead of heavier variant machinery
- fewer allocations during high-churn frames

This is attractive for spawn-heavy or destroy-heavy scenes.

### 3.2 Batched migrations by archetype pair

If many entities change archetype in the same tick, it may be worth batching those moves by source/destination pair.

Potential upside:

- fewer tiny move operations
- better cache locality during migration
- more contiguous copies

This should stay behind profiling until the current commit model shows a clear migration bottleneck.

### 3.3 Read-only view reuse across a wave

The current wave-scoped `WorldView` is already useful. If a future workload shows repeated chunk-pointer reuse within a wave, that pattern could be extended further.

Keep in mind:

- the more aggressively you cache views, the more careful you need to be about invalidation
- the benefit is strongest when queries are repeated many times inside the same wave

### 3.4 Predictive prefetching

Prefetching can help memory-bound systems, but it is extremely workload-specific.

Use only when:

- access patterns are stable enough to predict
- the system is demonstrably memory-latent, not compute-bound
- the target architecture benefits from it

This is a good experiment for AI vision, spatial queries, or steady movement loops.

### 3.5 Faster snapshot / serialization paths

World snapshots are more of a gameplay and tooling concern than a raw simulation speed concern, but they can still affect frame pacing.

Ideas:

- async snapshot building
- background serialization
- copy-on-write or delta-based save preparation

Useful for stutter-free saves and editor workflows.

## 4. Optimizations that are better as sibling libraries

Some ideas are good, but they fit better outside core.

### 4.1 SIMD batch kernels

This is the biggest one.

The core should stay layout-oriented. SIMD math helpers are better as a sibling library that operates on chunk spans.

Why:

- avoids turning core math into an ISA-specific abstraction layer
- keeps the engine flexible
- lets game code opt in only where it needs vectorized kernels

### 4.2 Network, physics, animation, audio, navmesh, editor tooling, migration

These are all useful, but they are already better treated as sibling libraries or tools.

The core should expose the hooks they need, not own their runtime policy.

## 5. Lower-priority / niche ideas

These may be worth exploring later, but they are not the first things to optimize.

### 5.1 NUMA awareness

Potentially useful on large workstation or server-class machines.

Probably not worth the complexity for a general-purpose engine until there is a clear target deployment that needs it.

### 5.2 Alternate steal policies

Different steal strategies can help specific job shapes, but they are subtle and easy to overfit.

Only worth touching if a benchmark shows a real tail-latency issue.

### 5.3 User-defined component packing

This may improve locality for some systems, but it adds storage and migration complexity very quickly.

Probably too invasive for core unless a very strong use case appears.

### 5.4 Epoch-based reclamation for event nodes

Useful if allocator churn becomes visible again.

Probably a later internal optimization, not an early design change.

## 6. Recommended order of attack

If the goal is to make core faster without destabilizing it, I would prioritize work in this order:

1. reduce chunk iteration overhead
2. reduce command recording / commit overhead
3. keep event and telemetry paths low-contention
4. keep grain sizing measurable and tunable
5. add wave-local caching only where profiling proves it matters
6. experiment with prefetching and batching only after real benchmarks justify it
7. keep SIMD and other specialized math in sibling libraries

## 7. Practical rule of thumb

For `threadmaxx`, the best performance work is usually:

- fewer branches
- fewer allocations
- fewer lookups
- fewer locks
- fewer redundant traversals
- more contiguous memory access
- more predictable job sizes

If a proposed optimization does not improve one of those, it probably is not worth the added complexity.

## 8. Short version

The core is already in a good architectural place.

The best future wins are likely to come from:

- cheaper chunk traversal
- cheaper command handling
- fewer contention points
- better grain tuning
- occasional cache-friendly batching

The things to be cautious about are:

- adding more commit-side parallelism too early
- turning the core into a SIMD/math framework
- adding storage modes that complicate migration
- optimizing for niche hardware before profiling a real workload

The roadmap is already pointing in the right direction. The next step is mostly measurement, tightening, and selective tuning rather than major redesign.

