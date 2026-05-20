# threadmaxx benchmarks

Opt-in microbenchmarks and §3.9 workload-realistic benches. Not
registered with CTest — they print timings to stdout and exit, which
is useful for ad-hoc profiling but noisy in CI.

## Build

```
cmake -S . -B build -DTHREADMAXX_BUILD_BENCHMARKS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

All targets land under `build/bench/`.

## Inventory

| Binary                  | Source batch | Purpose                                            |
|-------------------------|--------------|----------------------------------------------------|
| `commit_bench`          | 13c          | Single-threaded vs sharded commit, broad sweep     |
| `event_channel_bench`   | 13c          | Lock-free `EventChannel::emit` throughput          |
| `hierarchy_bench`       | 15b          | `HierarchySystem` resolution cost                  |
| `cull_bench`            | 15b          | `cullByFrustum` items × cameras matrix             |
| `foreach_bench`         | 15b          | Original `forEachWith` / `Cached` / `Chunk` sweep  |
| `resource_handle_bench` | 15b          | Refcount churn under contention                    |
| `pack_instances_bench`  | 15b          | `InstanceBufferLayout::packInstances` throughput   |
| `job_stealing_bench`    | 15b          | Worker steal-ratio sweep                           |
| `chunk_iter_bench`      | **16**       | Iteration paths on canonical workloads + raw walk  |
| `commit_path_bench`     | **16**       | Per-variant commit cost on Churn workload          |
| `migration_bench`       | **16**       | Per-archetype-pair migration cost                  |
| `grain_sweep`           | **16**       | `preferredGrain` sweep across canonical workloads  |
| `rpg_stress_bench`      | **26**       | Full-tick RPG-shaped decomposition at 10k/50k/100k |

The batch-16 benches are the **§3.9 gate** — every later batch in §3.9
must clear a numeric bar on at least one of these to land. The
batch-26 `rpg_stress_bench` is the **Phase 8 gate** (`OPTIMIZATION_PATH.md`)
— every Phase 8 batch ships with a before/after row from it.

## Canonical workloads (`scene_workloads.hpp`)

Three deterministic seeds shared by every §3.9 bench:

- **AI-only** — `kAiCount` (= 1,024) entities; Transform + Velocity +
  BoundingVolume, half also carry Health. RPG-shaped variety with a
  small population — exercises the inner-loop overhead more than raw
  throughput.

- **Render+AI** — `kRenderCount` (= 20,000) entities; ~50% renderable
  (`RenderTag`), ~50% movers (`Velocity`), ~5% `StaticTag`. The
  rpg_demo shape scaled up. The "headline" workload — what most
  later perf comparisons in §3.9 use.

- **Churn** — `kChurnCount` (= 100,000) entities; spawn/destroy and
  mask-flip workloads run against this. Drives the commit-phase /
  migration paths.

- **RpgStress** (`RpgStressWorkload`) — batch-26 addition. Mirrors
  `examples/rpg_demo` with `--stress` at the engine level: five
  archetype shapes (player, sword, terrain, NPCs, pickups), all
  built-in components only (no Vulkan / GLFW / user components
  dependency). Scales from 10k → 100k+ NPCs via the workload's
  `npcCount` / `pickupCount` public fields. Used by
  `rpg_stress_bench` to capture full-tick decomposition (step /
  update / commit / engBRF / other) at scale.

All seeded with `std::mt19937` so the entity layout is byte-
identical across runs.

## Common output format (`common.hpp`)

Every batch-16 bench emits CSV to stdout (and optionally to the
file passed as `argv[1]`):

```
label,workload,entities,workers,grain,mean_ns,stddev_ns,p50_ns,p95_ns,p99_ns,throughput,steal_pct,note
```

- `label` — system or operation being measured.
- `workload` — `AI` / `Render+AI` / `Churn` / `Churn/sharded` /
  `density-32k` / `scene-50pct` / …
- `mean_ns` / `stddev_ns` / `p50_ns` / `p95_ns` / `p99_ns` —
  per-tick wall-clock in **nanoseconds**, computed over the
  measurement window. Percentiles are nearest-index on the sorted
  sample vector.
- `throughput` — items/sec (entities/sec for iteration benches,
  cmds/sec for commit benches, migrations/sec for migration_bench).
- `steal_pct` — only populated by `grain_sweep`; the
  `stolenJobs / totalJobs` ratio observed during the window.
- `note` — free-form. Carries the headline derived metric like
  `ns_per_entity=68.6` or `ns_per_cmd=125.6`.

To compare two runs, redirect each to its own file and diff with
`csvtool` / `python -m pandas`:

```
build/bench/chunk_iter_bench /tmp/before.csv
# … apply optimization, rebuild …
build/bench/chunk_iter_bench /tmp/after.csv
diff -u /tmp/before.csv /tmp/after.csv
```

## What each batch-16 bench answers

### `chunk_iter_bench`

> Does `forEachChunk` actually win over `forEachWith` at the scales
> our games run at? At what entity count does the win start to show?
> How much of the cost is bookkeeping vs. real work?

Compares four iteration paths against the AI-only and Render+AI
workloads:

- `forEachWith<T...>` — per-entity, mask-test in the hot path.
- `forEachWithCached<T...>` — `MaskCache` rebuild in `preStep`.
- `forEachChunk<T...>` — chunk-span callback (lowest scheduler
  indirection).
- `rawMaskedWalk` — manual `world.transforms()` /
  `world.velocities()` / `componentMasks()` walk with inline mask
  test. Not a public path users should adopt — it bypasses
  `parallelFor` entirely — but useful as the "no scheduler / no
  callback indirection" lower bound.

The §3.9.2 batch 17 target: the gap between `forEachChunk` and
`rawMaskedWalk` is the headroom batch 17 attacks.

### `commit_path_bench`

> Which command variant is most expensive per cmd, and how does
> the cost change between `singleThreadedCommit = true` (default)
> and `false` (sharded)?

Four variants on the Churn workload: `setTransform` /
`setVelocity` (value-only, fast path), `addRemoveTag` (migrates),
`spawnDestroy` (heavy). The §3.9.3 batch 18 target: arena-backed
command storage + tagged-union payload should reduce the cost of
the value-only variants.

### `migration_bench`

> What is the per-entity cost when N entities migrate between two
> archetypes in the same tick? How does it scale with the number
> of migrations?

Two sweeps:

- **density-32k** — fixed 32,000-entity scene, varies migrations
  per tick from 16 → 32,000.
- **scene-50pct** — 50% migration density, varies the scene size
  from 1,024 → 100,000.

The §3.9.4 batch 19 target: when many entities flow between the
same `(src_arch, dst_arch)` pair, the per-migration cost should
drop with the batched path.

### `grain_sweep`

> For each canonical workload, what `preferredGrain` setting
> minimizes p99 step time AND keeps the steal ratio low?

Sweeps `ISystem::preferredGrain()` across {8, 16, 32, 64, 128,
256, 512}, with `parallelFor(grain=0)` so the engine picks up the
hint. Output drives per-system tuning recommendations for §3.9.2
batch 17 (and any future system that wants to declare a
data-driven grain).

## Adding a new bench

1. Drop a `.cpp` under `bench/` that includes `common.hpp` and
   (optionally) `scene_workloads.hpp`.
2. Use `LatencyHistogram` + `CsvWriter` so output is comparable
   to the existing benches.
3. Add the target to `bench/CMakeLists.txt`'s
   `THREADMAXX_BENCHMARKS` list.

Keep `kWarmup` ≥ 8 and `kIters` ≥ 64 unless the operation is so
expensive that takes minutes per run. `Stopwatch` reads
`std::chrono::steady_clock`; resolution is ~ns on Linux.

## Shipping bar for §3.9.x batches

Per `FUTURE_WORK.md` §3.9.7, a batch in §3.9.2 / §3.9.3 / §3.9.4
**lands** only when:

- Its PR body cites a before/after CSV diff from the relevant
  batch-16 bench.
- Mean and p99 both move in the right direction on at least one
  canonical workload.
- The full ctest suite still passes byte-for-byte (commit-hash
  golden tests catch determinism regressions).

If the numbers don't move, the change doesn't land — the
optimization notes are explicit that *"if a proposed
optimization does not improve fewer branches / allocations /
lookups / locks / redundant traversals / more contiguous access
/ more predictable job sizes, it probably is not worth the
added complexity."*

## Shipping bar for Phase 8 batches (`OPTIMIZATION_PATH.md`)

Phase 8 inherits the §3.9 rules and adds the **`rpg_stress_bench`
diff** as the cross-cutting gate. Every Phase 8 PR body cites a
before/after row from `rpg_stress_bench` AT 100k entities at
minimum. If the touched path is also covered by a more focused
bench (chunk_iter / commit_path / migration / grain_sweep), the
PR cites that one too. The diff protocol:

```
cmake --build build --target rpg_stress_bench -j
./build/bench/rpg_stress_bench /tmp/before.csv
# ... apply optimization ...
./build/bench/rpg_stress_bench /tmp/after.csv
diff -u /tmp/before.csv /tmp/after.csv
```

Phase rows emitted: `step` (total), `update` (sum of system
update durations), `commit` (single-threaded commit phase),
`engBRF` (engine's render-frame build), `other` (residual).
Sweep across 10k / 50k / 100k NPCs by default; override via
`./rpg_stress_bench out.csv <npc1> <npc2> ...` for ad-hoc
sweeps.

The CSV columns are the same as every other bench; the `note`
field carries `ns_per_entity` so a glance at the diff shows
the per-entity cost change at each scale. A regression at one
scale + a win at another is acceptable when the Pareto front
moves the right way overall; the PR body must explain the
trade-off.
