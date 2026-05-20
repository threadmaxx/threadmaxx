# threadmaxx — Optimization Path

Forward-looking, measurement-driven plan for the next sweep of core
library optimization. Distilled from `FUTURE_WORK.md` (the batch
history, batches 1–25) and `threadmaxx_core_future_optimization_notes.md`
(the governing principles). Picks up where the v1.1 line closed.

This document is the source of truth for **what comes next**;
`FUTURE_WORK.md` is the archive of what shipped.

Last refreshed: 2026-05-20

---

## 1. Where we stand

- **v1.0.0** sealed the core library (batch 25, 2026-05-18) —
  fixed-step sim, archetype storage, wave scheduler, sharded commit,
  task-graph DAG, telemetry sinks, hot reload, serialization,
  108/108 ctest on both `build/` and `build-werror/`.
- **v1.1.0** shipped the Vulkan skinning pipeline (batch 9b.4.a/b/c,
  2026-05-18) — `opaque_skinned` pipeline, per-frame bone-matrix
  ring, procedural 2-bone capsule demo. Real-asset (glTF) import
  remains a focused sibling-library batch.
- **rpg_demo `--stress` optimization (2026-05-20)** drove the
  renderer from 134 ms/tick → 4 ms/tick at 100k entities (33×). The
  fix was demo-side (AOI radius + debug-line packing), not engine-
  side; the engine framework was never the bottleneck in that
  workload.

**Closed phases** (per `FUTURE_WORK.md` §6):
- Phase 1 — job model stabilization ✅
- Phase 2 — fine-grained system slicing ✅
- Phase 3 — frame task graph ✅
- Phase 4 — cancellation and budgets ✅
- Phase 5 — storage contention reduction ✅
- Phase 6 — measurement primitives + ingestion ✅
- Phase 7 — measurement-driven tightening (§3.9, batches 16–20) ✅

**Open items** (deferred behind profile data):
- §3.6.4 — per-chunk record-time command buffers
- §3.6.4 — cross-wave `WorldView` snapshot pointer cache
- §3.9.6 — SIMD, NUMA, alternate steal policies, user-defined
  component packing, epoch reclamation for event nodes
- §3.11.7b.5 — glTF skinning importer (sibling-library batch)

## 2. Governing principles

Lifted verbatim from `threadmaxx_core_future_optimization_notes.md` §7
and made load-bearing here. A proposed batch in §3 that violates one
of these does not ship.

1. **Fewer branches, allocations, lookups, locks, redundant
   traversals.** If a change does not improve at least one, it
   probably is not worth the added complexity.
2. **More contiguous access. More predictable job sizes.** Cache
   locality and tail-latency stability over peak microbenchmark
   throughput.
3. **Profile before optimizing.** Every §3 batch ships with a
   before/after number from a §4 bench in its PR body. No number,
   no land.
4. **Determinism is non-negotiable.** Every batch must preserve
   `commit_hash_test.cpp` and `sharded_commit_test.cpp`
   byte-for-byte. Hash drift = automatic reject.
5. **No public API breakage.** All additions are additive (new
   methods, new types, opt-in feature flags). Deprecation cycle
   per `version.hpp` rules.
6. **Sibling library, not core.** SIMD math, physics, audio, glTF
   import, navmesh — none belong in the core library. The engine
   ships the layout (chunked storage, render-frame builder); the
   consumer ships the algorithms.
7. **Each batch is independently shippable.** A 1-day batch
   should not depend on a 2-week batch landing first.

## 3. Phase 8 — Workload-driven library tightening (v1.2+)

The §3.9 batches optimized against synthetic micro-benchmarks. Now
that two real-game consumers (`examples/vulkan_renderer/`,
`examples/rpg_demo/`) ship and stress workloads run at 100k+
entities, the next phase is grounded in **real-workload profile
evidence**, not synthetic gates alone.

### 3.1 Phase 8 sequencing

```
B26 (gate) → B27 (profile sweep) → {B28, B29, B30, B31} (parallel)
                                     ↓
                                  B32 (hygiene)
                                     ↓
                                  B33 (docs polish)
                                     ↓
                                  v1.2 release
```

**B26 is the gate.** No subsequent batch ships without a
before/after row produced by B26 infrastructure or one of the
existing `bench/` binaries.

**B27 is the diagnostic.** It identifies the top 3 inefficiencies
in the new workloads but does **not** optimize anything. Its output
is the input to B28–B31.

**B28–B31** are candidate optimizations. Each one ships only if
B27 evidence (or pre-existing batch-16 bench data) justifies the
add. Any of them may drop out of the plan if measurement says they
don't matter.

### 3.2 Batch 26 — Stress-scale benchmark harness (gate)  ✅ landed 2026-05-20

**Goal:** make the rpg_demo `--stress` workload reproducible inside
the `bench/` infrastructure so subsequent batches can diff against
it without needing GLFW + Vulkan + a real screen.

**Scope:**

- New `bench/scene_workloads.hpp` entries:
    - `RpgStressWorkload` — mirrors `rpg_demo --stress`: ~10k NPCs
      with Faction + BoundingVolume + NpcState (user component) +
      AnimState (user component), ~50k pickups with Pickup user
      component, ~10k static terrain entities. Five archetype
      shapes. Total ~70k entities; configurable to ≥100k.
    - `RpgStressTickHarness` — drives a registered system mix
      (movement integration, parallel chunk-walking accumulator,
      a serial `ctx.single` "brain" body for the NPC slice) and
      reports per-phase wall-clock decomposition (update / commit
      / wave-rebuild / pre-postStep) using `EngineStats` +
      `SystemStats`.
- New `bench/rpg_stress_bench.cpp` — runs `RpgStressTickHarness`
  for 300 ticks, prints CSV rows with `phase, ns_per_tick,
  ns_per_entity` columns. Output schema fixed so B27/B28/B29
  PRs can paste their CSVs into PR bodies as evidence.
- Extend `bench/grain_sweep.cpp` to the 100k-entity scale (today
  it tops out at 20k Render+AI).

**No engine changes.** Pure addition under `bench/`.

**Test gate:** none — benchmarks are not registered with ctest by
design (per `bench/README.md`'s "noisy in CI" note). The
correctness invariant is that the harness uses the same
`commit_hash_test`-pinned APIs, so accidental engine breakage
surfaces through the existing 108-test gate.

**Acceptance:**
- The harness runs end-to-end on `-DTHREADMAXX_BUILD_BENCHMARKS=ON`.
- The CSV output shows per-phase decomposition for at least three
  entity-count rows (e.g. 10k / 50k / 100k).
- `bench/README.md` documents the new bench + workload + how to
  diff two runs.

**Effort:** ~3 hours.

**Why this is the gate:** the rpg_demo session showed that
microbenchmarks alone don't catch the real workload shape — a 4ms
microbench cost can dominate a 17ms frame, or be invisible. The
B26 harness produces the canonical "what does a 100k-entity tick
look like" CSV that every B27+ batch must move the right way.

**As-shipped 2026-05-20:**

- `bench/scene_workloads.hpp` — new `RpgStressWorkload` mirroring
  `rpg_demo --stress` at the engine level. 5 archetype shapes
  (player, sword, terrain, NPCs, pickups) — built-in components
  only, no Vulkan / GLFW / user-component dependency.
  Configurable via `npcCount` / `pickupCount` members.
- `bench/rpg_stress_bench.cpp` — new binary. Registers three
  systems mirroring the demo system mix (MovementSystem parallel
  chunk-walking, BrainSystem `ctx.single` serial, RenderPrepSystem
  parallel chunk-walking accumulator). Captures `EngineStats`-
  derived per-phase histograms over 128 iterations after 8 warmup
  ticks. Sweeps 10k / 50k / 100k NPC scales by default; CLI args
  override.
- `bench/CMakeLists.txt` — registers `rpg_stress_bench` in
  `THREADMAXX_BENCHMARKS`.
- `bench/README.md` — documents `RpgStressWorkload`,
  `rpg_stress_bench`, and the Phase 8 shipping bar with the
  diff-protocol example.

**Baseline numbers (dev workstation, 4 workers, 128 iters):**

| scale (NPCs+pickup) | step    | update  | commit  | engBRF  |
|---------------------|---------|---------|---------|---------|
| 10k + 5k = 15k      | 1.91 ms | 1.08 ms | 0.97 ms | 0.7 µs  |
| 50k + 5k = 55k      | 7.07 ms | 2.35 ms | 4.84 ms | 0.8 µs  |
| 100k + 5k = 105k    | 15.6 ms | 5.90 ms | 9.95 ms | 0.8 µs  |

**Headline finding:** at 100k entities, `commit` (9.95 ms) >
`update` (5.90 ms). The commit-phase cost dominance was predicted
by §3.10.1 batch 21 ("the hash itself — ~70 ns/cmd of FNV-1a
byte-by-byte over the 48-byte CmdSetTransform — is on the serial
path and is fixed by the determinism contract") but had no
workload-realistic benchmark proving it at scale. B26 now does.
**This sharpens B27's diagnostic target:** the top inefficiency
at 100k entities is the FNV-1a hash cost on the single-threaded
commit path. Candidate optimizations (B27 will weigh each):
incremental hash skipping for value-only writes, hash-batching
across the variant stream, or alternate determinism contracts
(e.g. per-archetype hash rollups). The current B26 baseline is
the gate every candidate must clear.

**Verification:**
- `bench/rpg_stress_bench` builds clean on both `build/` and
  `build-werror/` with `-DTHREADMAXX_BUILD_BENCHMARKS=ON`.
- Full ctest **108/108** on both trees (no behavior change —
  the new workload + bench are additive under `bench/`).

### 3.3 Batch 27 — Hot-path profile sweep (diagnostic)

**Goal:** identify the top inefficiencies in the B26 workload
without optimizing yet. The output is the input to B28–B31.

**Scope:**

- Run B26's `rpg_stress_bench` with `perf record` (or
  equivalent platform profiler). Capture flame graphs for the
  10k / 50k / 100k rows.
- For each entity-count row, attribute time to:
    - `update` (system update bodies) — chunk-walk overhead vs.
      callable-body work
    - `commit` (single-threaded path) — variant dispatch vs.
      `applyCommandImpl` vs. hash mix
    - `wave-rebuild` — `rebuildWaves` invocation cost +
      `WorldView` rebuild cost between waves
    - `pre/postStep` — serial commit churn
    - `engBRF` (engine's render-frame build) — chunk iteration +
      `RenderInstance` packing
    - `events` — typed channel drain cost
- Output: a `bench/profile_report.md` (a one-shot doc, not a
  CSV) describing the top 3 inefficiencies + 3 candidate
  optimizations with proposed bench gates for each.

**No code changes.** Pure measurement + reporting.

**Acceptance:**
- The report names the top 3 inefficiencies by ns/tick at 100k
  entities.
- Each candidate optimization names: (a) the hypothesis, (b) the
  bench that would gate it, (c) the expected delta.
- The report is reviewed against principle #1 (must reduce
  branches / allocations / lookups / locks / redundant
  traversals).

**Effort:** ~4 hours (mostly profiler tooling + write-up).

**Why this is the diagnostic:** every batch in §3.9 shipped with
a specific hypothesis ("variant size dominates command-buffer
memory"). Without a fresh profile, B28+ would be guessing. The
B27 report is the **named target** for each later batch.

### 3.4 Batch 28 — WorldView lifetime reuse (deferred from §3.6.4)

**Conditional batch.** Ships **only if** B27 shows
`WorldView` rebuild + `rebuildWaves` overhead > 2% of step time
at 100k entities.

**Hypothesis:** `WorldView` is rebuilt between every wave (today,
~4–6 waves/tick in the RPG-stress workload). At 100k entities + 5
archetypes, each rebuild is ~10µs × 6 = 60µs/tick. If consistent
across ticks, that's ~0.36% of a 16.7ms frame — likely below
threshold. But if archetype churn (spawn/destroy mid-tick) forces
extra rebuilds, it climbs.

**Scope (if greenlit):**

- Add `EngineImpl::worldViewDirty_` (atomic bool). Set on commit
  paths that grow / shrink archetypes (the swap-pop + the
  migrate-create branches in `ArchetypeTable`).
- `worldView()` accessor reuses the cached view when dirty == false;
  rebuilds only on dirty == true. The rebuild itself stays cheap.
- Per-tick stats: `EngineStats::worldViewRebuildCount` (was
  implicit before; surface for measurement).

**Test gate:**
- All existing tests pass byte-for-byte (`commit_hash_test`,
  `sharded_commit_test`, `world_view_test`).
- New `tests/world_view_lifetime_test.cpp` asserts:
    - Wave-to-wave reuse when no commits touched archetype
      structure.
    - Rebuild on the first wave after a spawn-into-new-archetype.
    - Rebuild on the first wave after a destroy that emptied a
      chunk.

**Acceptance:**
- `worldViewRebuildCount` drops by ≥ 50% on the RpgStress
  workload across 300 ticks at 100k entities.
- Total step time drops by at least the expected delta from B27.
- No regressions in any existing bench (chunk_iter, commit_path,
  migration).

**Effort:** ~1 day if greenlit, including the test.

### 3.5 Batch 29 — Per-chunk record-time command buffers
       (deferred from §3.6.4)

**Conditional batch.** Ships **only if** B27 shows
`CommandBuffer::record` + commit-time classifier > 5% of step
time at 100k entities, AND the sharded commit path is being used
(otherwise the classifier doesn't exist).

**Hypothesis:** the sharded commit path's classifier pass
(`commitBuffersSharded` Pass A + Pass B) walks every command
twice. Record-time routing would skip the classifier entirely.

**Critical caveat:** `singleThreadedCommit = true` is the default
and is faster than sharded on every measured workload (per
§3.6.3 batch 13c, §3.10.1 batch 21). If B27 shows the
single-threaded path is the dominant cost (likely), this batch
**does not ship**. It exists only as a hedge against a future
workload where sharded becomes a real win.

**Scope (if greenlit):**

- `CommandBuffer` gains a per-chunk-bin internal layout: instead
  of a single `std::vector<Command>`, hold
  `std::vector<ChunkBin>` keyed by destination archetype index.
- Recording API stays bit-for-bit identical from the caller's
  perspective; the routing is internal.
- `commitBuffersSharded` skips Pass A + Pass B; jumps straight
  to Pass C (parallel apply).
- Determinism: each chunk-bin still applies in submission order.
  Cross-chunk commands stay on the sim thread (today's "global
  lane" fallback). The hash function is unchanged.

**Test gate:**
- `commit_hash_test` and `sharded_commit_test` byte-identical
  golden.
- New `tests/per_chunk_recording_test.cpp` asserts intra-bin
  submission order, cross-bin independence, and the global-lane
  fallback for mask-toggling commands.

**Acceptance:**
- Sharded commit on B26's RpgStress workload at 100k entities
  beats single-threaded commit by ≥ 20% across all variants.
- (Otherwise the batch doesn't ship; defer back to §3.6.4.)

**Effort:** ~1 week if greenlit. The recording API stays
backward-compatible; the work is in `CommandBuffer.cpp` +
`EngineImpl::commitBuffersSharded`.

### 3.6 Batch 30 — Deterministic parallel `ctx.single`

**Conditional batch.** Ships **only if** B27 shows
`ctx.single`-bodied systems > 30% of update time at 100k
entities. Likely candidates: NPC brain bodies that use a
shared RNG.

**Hypothesis:** the rpg_demo's NPCBrainSystem is serial via
`ctx.single` because `std::mt19937` isn't thread-safe. At 100k
entities × 10% NPCs = 10k serial brain bodies / tick. If each
body is ~1µs, that's 10ms — dominating the 11.27ms `update`
phase we measured.

**Scope (if greenlit):**

- `SystemContext::parallelForWithRng<Rng>(count, grainSize,
  std::span<Rng> rngsPerWorker, callable)` — new overload that
  hands each worker its own RNG sub-stream.
- Sub-stream contract: the engine pre-seeds N worker RNGs from
  a single master seed. Each worker's RNG is **deterministic
  conditional on worker count + grain + master seed**.
- Caveat: this is **NOT bit-identical determinism** under
  changing worker count. Game code that needs strict
  determinism (replay, networked sims) must pin worker count
  or use `Config::deterministic = true` + the existing
  `ctx.single` path.
- Documented as the "fast but loosely deterministic" path.

**Test gate:**
- New `tests/parallel_rng_test.cpp` asserts:
    - Same seed + same worker count → identical results across
      runs.
    - Different worker counts produce different results (this
      is **expected** — documented contract).
    - The existing `commit_hash_test` runs the strict-determinism
      path; it does **not** use the new API.

**Acceptance:**
- NPCBrain-like serial bodies drop by ≥ 4× on a 4-worker
  machine.
- Documented as opt-in / non-strict-determinism.

**Effort:** ~1 week if greenlit. The risk surface is the
documented determinism contract; implementation is small.

### 3.7 Batch 31 — Wave-scheduler micro-opts

**Conditional batch.** Ships **only if** B27 shows
`rebuildWaves` or per-wave dispatch overhead > 1% of step time.

**Hypothesis:** at 12+ systems × 5+ waves, the wave-loop in
`EngineImpl::step` does N system-pointer derefs + dispatches per
tick. If the systems are tiny (`ctx.single` writes only), the
loop overhead is meaningful.

**Scope (if greenlit):**

- Compile-time-friendly wave dispatch: pre-bake the per-wave
  system index lists in `rebuildWaves`; the `step()` loop
  iterates a tight `std::span<const std::uint8_t>` per wave.
- Hot loop avoids the `ISystem*` indirection for the simple
  "is this system in this wave" check.

**Test gate:**
- `commit_hash_test`, `sharded_commit_test`, `task_graph_test`
  all byte-identical.
- New `tests/wave_dispatch_bench.cpp` (a bench, not a test)
  measures the per-tick wave dispatch overhead.

**Acceptance:**
- B26's `step` time drops by the expected delta from B27 (likely
  small — single-digit %).

**Effort:** ~2 days if greenlit.

### 3.8 Batch 32 — Sanitizer + soak hygiene pass

**Unconditional.** Ships regardless of B27 findings; this is
the "quality" gate before the v1.2 release.

**Scope:**

- Build the full test suite under `-fsanitize=thread` (TSAN).
  Run; fix every race the sanitizer surfaces. Most likely
  candidates: any `std::atomic<bool>` reads paired with relaxed
  stores that aren't documented as safe; benign races in
  `JobSystemStats` counters that TSAN flags but are
  intentional.
- Build under `-fsanitize=address,undefined` (ASAN + UBSAN). Fix
  every issue.
- New `tests/concurrency_soak_long.cpp` — runs the existing
  `concurrency_soak_test` for 10,000 ticks instead of 200. Not
  registered with ctest by default (too slow); opt-in via
  `-DTHREADMAXX_BUILD_LONG_SOAK=ON`.
- Public-API coverage audit: walk every public method in
  `include/threadmaxx/`; verify each has at least one test
  hitting it. Document gaps in `tests/COVERAGE_AUDIT.md`.

**Test gate:**
- TSAN run clean across all 108 tests.
- ASAN + UBSAN run clean across all 108 tests.
- Long soak passes (10k ticks).

**Acceptance:**
- All sanitizers clean; long soak clean.
- Coverage audit complete + any gaps closed with new tests.

**Effort:** ~1 week. Most of the time is hunting and fixing the
races / leaks the sanitizers surface.

### 3.9 Batch 33 — Documentation + release polish

**Unconditional.** The final batch before tagging v1.2.

**Scope:**

- New `doc/performance_tuning.md` — covers `preferredGrain`,
  `Config::singleThreadedCommit`, `setTickBudget` +
  `SkipPolicy`, the bench harness, how to capture a profile,
  how to read the §3.7 telemetry dashboard outputs.
- `doc/migration_v1_to_v1_2.md` — likely tiny (no breaking
  changes per principle #5), but document any new opt-in
  knobs.
- Doxygen audit: every public symbol in `include/threadmaxx/`
  has at least a one-line `@brief`; load-bearing methods carry
  `@thread_safety` and `@pre` notes.
- `CHANGELOG.md` v1.2.0 entry summarizing batches 26–32.
- `version.hpp` bump to 1.2.0; CMakeLists project version bump.
- New `tests/version_test_v1_2.cpp` — gate the macros against
  the version string.

**Test gate:** standard 108+ tests pass; new sanitizer + soak
binaries also pass.

**Acceptance:**
- `find_package(threadmaxx 1.2 CONFIG)` resolves cleanly on a
  fresh tree.
- The CHANGELOG accurately summarizes the phase 8 work.
- The performance-tuning doc is complete enough that a new
  game project can self-serve.

**Effort:** ~2 days.

## 4. Bench infrastructure

The §3 batches rely on the existing `bench/` directory plus the B26
additions. Inventory after B26:

| Binary                  | Purpose                                              | Gate for |
|-------------------------|------------------------------------------------------|----------|
| `commit_bench`          | Single vs sharded commit, broad sweep                | B29      |
| `commit_path_bench`     | Per-variant commit cost on Churn                     | B29      |
| `event_channel_bench`   | Lock-free emit throughput                            | —        |
| `hierarchy_bench`       | HierarchySystem resolution cost                      | —        |
| `cull_bench`            | cullByFrustum items × cameras matrix                 | —        |
| `foreach_bench`         | forEachWith / Cached / Chunk sweep                   | —        |
| `chunk_iter_bench`      | Iteration paths on canonical workloads               | B28, B31 |
| `migration_bench`       | Per-archetype-pair migration                         | —        |
| `grain_sweep`           | preferredGrain sweep                                 | B31      |
| `job_stealing_bench`    | Worker steal-ratio sweep                             | B30      |
| `pack_instances_bench`  | InstanceBufferLayout::packInstances throughput       | —        |
| `resource_handle_bench` | Refcount churn under contention                      | —        |
| `simd_kernels`          | Sibling SIMD library benches                         | —        |
| **`rpg_stress_bench`**  | **B26 addition** — RPG-shaped 100k-entity tick      | **all**  |

**Output convention:** every bench writes CSV with `BenchRow`
columns (label, workload, entities, workers, grain, mean_ns,
stddev_ns, p50/95/99_ns, throughput, steal_pct, note). The `note`
column carries the headline derived metric. PRs cite specific
CSV rows; the regression detector diffs them.

**Run protocol** (documented in `bench/README.md`):
```
cmake --build build --target rpg_stress_bench -j
./build/bench/rpg_stress_bench /tmp/before.csv
# ... apply optimization ...
./build/bench/rpg_stress_bench /tmp/after.csv
diff /tmp/before.csv /tmp/after.csv
```

## 5. Out of scope for Phase 8

These items are good but belong outside Phase 8 — either above the
library (sibling) or below the gate (no profile evidence yet).

- **SIMD math kernels.** Already shipped as `threadmaxx_simd`
  sibling library at v1.0.0. Further SIMD work happens there, not
  in core.
- **glTF / FBX skinning importer.** Sibling library. Plumbing
  through `MeshLoader` already exists (`createMesh(span, span)`)
  since 9b.2a. Belongs under `examples/rpg_demo/` or a new
  `threadmaxx_gltf` sibling library, not in core.
- **NUMA-aware allocation.** Per notes §5.1, niche-hardware
  territory. Not on the v1.x path.
- **User-defined component packing.** Per notes §5.3, very
  invasive vs. the migration / storage path. Defer indefinitely.
- **Epoch-based reclamation for event nodes.** Per notes §5.4,
  only if profile data shows the Treiber-stack allocation rate
  matters. The lock-free MPSC channel from §3.6.3 batch 13c
  already cleared the contention bar; allocator churn isn't
  flagged by any current bench.
- **Alternate steal policies.** Per notes §5.2, only on profile
  evidence of tail-latency outliers. Today's policy passes
  `job_stealing_bench` cleanly.
- **Network / physics / audio / animation pipeline.** All sibling
  libraries per `FUTURE_WORK.md` §5. The engine ships hooks
  (`PhysicsBodyRef`, `NavAgentRef`, `AnimationStateRef`,
  deterministic commit, stable entity ids), not solvers.
- **Editor / tooling UI.** Out of scope until v2.x.

## 6. Definition of done for Phase 8

Phase 8 closes when:

- ✅ All conditional B28–B31 batches that B27 evidence justified
  have shipped (or been documented as "deferred — no profile
  signal").
- ✅ B26 (the gate), B27 (the diagnostic), B32 (sanitizer pass),
  and B33 (docs polish) have all shipped unconditionally.
- ✅ ctest count is at least 108 + new tests from any conditional
  batch that landed; both `build/` and `build-werror/` pass
  100%.
- ✅ TSAN, ASAN, UBSAN all clean.
- ✅ The 10,000-tick long soak passes on at least the
  `RpgStressWorkload`.
- ✅ `commit_hash_test` and `sharded_commit_test` byte-identical
  golden against the v1.1 reference. No determinism drift.
- ✅ `bench/rpg_stress_bench` shows a measurable wall-clock
  improvement at 100k entities vs. the v1.1 baseline. Even if no
  single batch was dramatic, the cumulative wins should be
  visible.
- ✅ The performance-tuning doc is complete enough that an
  external game project can self-serve.

When the above is met, tag v1.2.0 and update CHANGELOG.md.

## 7. Beyond Phase 8 — speculative items

Documented for context, **not scheduled**. Each is contingent on
real-game evidence emerging that justifies the structural cost.

- **Per-job stack allocation reuse across ticks.** Today
  `parallelFor`'s `std::latch` is constructed fresh per call.
  Pre-cached latch + atomic-reset would save microseconds /
  call; meaningful only if `parallelFor` is called >> 1000 ×
  per tick.
- **`std::expected`-style error returns from the engine.** Today
  the engine returns `bool` and logs via `ILogger`. A
  richer error type would propagate cause + context. Defer
  until a real consumer asks for it.
- **GPU-side determinism guard.** Per-frame
  hash of the published `RenderFrame` (cameras + lights +
  draw items) to catch renderer-side reordering bugs the same
  way `commit_hash_test` catches engine-side ones. Useful if
  a future multi-threaded renderer is built.
- **Cross-batch coalesced events.** Today `SystemSkipped`
  emits one event per skipped system per tick. At high skip
  rates this is per-tick noise; coalescing into a batched
  event would reduce drain cost. Defer per §3.5 batch 12
  ("expected workloads don't measure this mattering").
- **Stronger v2 ABI.** Hand-mangled or named `extern "C"` exit
  points for C# / Rust / Go FFI. Out of scope for v1.x.

## 8. Connection to the optimization notes

For traceability, the mapping from
`threadmaxx_core_future_optimization_notes.md` → this plan:

| Notes section                          | Plan section                  |
|----------------------------------------|-------------------------------|
| §2.1 cheaper chunk iteration           | Done (§3.9.2 batch 17). New work only if B27 surfaces a fresh hot path. |
| §2.2 simpler commit path               | Done (§3.9.3 batch 18, §3.9.4 batch 19). Further work in B29 (conditional). |
| §2.3 reduce contention                 | Done (§3.6.3 batch 13c). No follow-on. |
| §2.4 tunable / measurable grain        | Done (§3.4 batch 11 + §3.9.1 batch 16). Further work in B31 (conditional). |
| §3.1 command buffer arena              | Done differently (§3.9.3 batch 18, unique_ptr-backed variants). B29 is the deeper variant. |
| §3.2 batched migrations                | Done (§3.9.4 batch 19). No follow-on. |
| §3.3 cross-wave view reuse             | B28 (conditional). |
| §3.4 predictive prefetching            | Not on the §3 path. Workload-specific; defer to game code. |
| §3.5 faster snapshot / serialization   | Done (§3.9.5 batch 20). No follow-on. |
| §4 sibling libraries                   | `threadmaxx_simd` v1.0 shipped. glTF importer pending sibling work. |
| §5 niche / lower priority              | §5 of this plan (out of scope). |
| §6 order of attack                     | §3.1 of this plan. |
| §7 rule of thumb                       | §2 of this plan. |

## 9. Bench-gate quick reference

If you're working on a Phase 8 batch and need to know which bench
to cite in the PR:

- **Iteration / chunk-walk changes** → `chunk_iter_bench`,
  `foreach_bench`
- **Commit-phase changes** → `commit_bench`, `commit_path_bench`,
  `commit_soak_test` (run as test, not bench)
- **Migration changes** → `migration_bench`
- **Wave-scheduler / grain changes** → `grain_sweep`,
  `job_stealing_bench`
- **Event channel changes** → `event_channel_bench`
- **Hierarchy changes** → `hierarchy_bench`
- **Resource registry changes** → `resource_handle_bench`
- **Render-side changes** → `cull_bench`, `pack_instances_bench`
- **Full-tick / stress workload changes** → `rpg_stress_bench` (B26)

A PR that touches the commit phase but only cites
`chunk_iter_bench` numbers is missing its gate. Either run the
right bench or explain in the PR body why the touched code path
isn't on a hot path.

---

**This document is the source of truth for Phase 8 planning.**
When a batch lands, mark it ✅ here, add a per-batch entry in
`FUTURE_WORK.md` § (the archive), and update `CHANGELOG.md`. When
Phase 8 closes, fold the unmet candidates back into "Beyond Phase
8" or document why they were dropped.
