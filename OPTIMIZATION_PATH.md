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
B26 (gate) ✅ → B27 (profile) ✅ → B28 (forEachChunk sub-job) → B29 (parallel pre-hash)
                                                                   ↓
                                                                 B30 (per-arch hash, IF C2 underperforms)
                                                                   ↓
                                                                 B32 (sanitizer + soak)
                                                                   ↓
                                                                 B33 (docs polish + v1.2 release)
```

**B26 is the gate.** ✅ landed 2026-05-20. No subsequent batch
ships without a before/after row from `rpg_stress_bench`.

**B27 is the diagnostic.** ✅ landed 2026-05-20. Output:
`bench/profile_report.md`. Identified three concrete optimization
candidates with bench gates; sequencing below reflects them.

**B28** (C1 in the profile report) — sub-job dispatch in
`forEachChunk` for large chunks. Low risk, ~3-4 ms/tick win at 100k
entities. The clean first move.

**B29** (C2 in the profile report) — parallel pre-hash + serial
mix-in. Medium risk (byte-identity proof of the hash), ~6 ms/tick
win at 100k entities.

**B30** (C3 in the profile report) — per-archetype hash rollup.
**Gated on B29.** Only ships if C2 underperforms its target; the
contract amendment (re-recorded reference hashes for any external
client) is real user-facing churn and v1.x should not pay it
without clear evidence-driven need.

**B31** (wave-scheduler micro-opts) — **downgraded by B27.** The
profile shows wave-rebuild + per-wave dispatch overhead is below
1% of step time at 100k entities. Skip unless a future workload
exposes it.

**B32 + B33** unchanged. Both ship unconditionally before the v1.2
tag.

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

### 3.3 Batch 27 — Hot-path profile sweep (diagnostic)  ✅ landed 2026-05-20

**Goal:** identify the top inefficiencies in the B26 workload
without optimizing yet. The output feeds B28+.

**As-shipped 2026-05-20:**

- `bench/rpg_systems.hpp` — shared system definitions
  (`MovementSystem`, `BrainSystem`, `RenderPrepSystem`) factored
  from `rpg_stress_bench.cpp` so both the production gate (B26)
  and the diagnostic probe (B27) link the same code.
- `bench/rpg_stress_probe.cpp` — three diagnostic passes over the
  RpgStress workload:
    - **Pass A** per-system breakdown
      (`lastUpdateSeconds` / `waitSeconds` / `peakQueueDepth` /
      `commandsLastStep` for each system at each scale)
    - **Pass B** system-mix ablation (each system selectively
      disabled at 100k entities, delta vs. baseline)
    - **Pass C** commit cost as a function of command volume
      (movement-only at 1k → 200k NPCs)
- `bench/profile_report.md` — the **deliverable**. Names three
  inefficiencies and three candidate optimizations (C1/C2/C3),
  each with a hypothesis, bench gate, expected delta, risk
  assessment, and effort estimate.
- `bench/CMakeLists.txt` — registers `rpg_stress_probe` in
  `THREADMAXX_BENCHMARKS`.

**Top three inefficiencies** (see report for the data behind each):

1. **Single-threaded commit hashing dominates at 100k cmds/tick.**
   ~7 ms / tick of the 9.8 ms commit phase is FNV-1a-64 byte-mix
   on the sim thread; payload-size-proportional (~70 ns/cmd for
   48-byte CmdSetTransform).
2. **`forEachChunk` load imbalance.** Movement is 99.9% wait-bound
   (peakQueueDepth = 1 at 100k); the NPC chunk (95% of entities)
   is one indivisible job → one worker. Workers 2-4 sit idle.
   ~3-4 ms / tick recoverable by sub-job splitting.
3. **`applyCommandImpl` storage write.** ~28 ns/cmd × 100k =
   2.8 ms / tick on the serial path. Below the headline items but
   on the same commit budget.

**Three candidate optimizations** (sequenced into B28/B29/B30):

- **C1 — Sub-job dispatch in `forEachChunk` for large chunks.**
  Hypothesis: movement update 5.09 → ~1.3 ms (4× on 4 workers);
  step drops ~3-4 ms. Risk: moderate (additive header change).
  → **B28.**
- **C2 — Parallel pre-hash + serial mix-in.** Hypothesis: commit
  9.8 → ~3.5 ms; step drops ~6 ms. Risk: high (byte-identity
  proof). `commit_hash_test.cpp` must remain byte-identical. → **B29.**
- **C3 — Per-archetype hash rollup.** Hypothesis: commit 9.8 →
  ~1 ms; step drops ~9 ms. Risk: highest (determinism contract
  change → re-recorded reference hashes for any external client).
  → **B30, gated on B29 underperforming.**

**B31 (wave-scheduler micro-opts) downgraded.** The probe shows
wave dispatch is in the noise (< 1% of step time at 100k entities).
Skipped unless a future workload exposes it.

**Verification:**
- `bench/rpg_stress_probe` builds clean on both `build/` and
  `build-werror/`.
- Full ctest **108/108** on both trees (no engine changes — pure
  diagnostic addition).

### 3.4 Batch 28 — Sub-job dispatch in `forEachChunk` (C1)

**Locked in by B27.** Attacks the #2 inefficiency (`forEachChunk`
load imbalance on large chunks). Greenlit unconditionally.

**Hypothesis:** the NPC chunk at 100k rows is one indivisible job
under today's `forEachChunk` (one job per matching chunk). When a
single chunk dwarfs all others, parallelism collapses to 1× the
worker count — only the worker that grabbed it does any work.
Movement spends 99.9% of its update window in the latch wait
(`waitSeconds / lastUpdateSeconds = 5.083 / 5.090`).

**Scope:**

- Extend `Query.hpp::forEachChunk<Required...>` with a
  `kSubJobThreshold` (proposed default: 1024 rows). When a
  matching chunk has > threshold rows, dispatch multiple sub-jobs
  per chunk, each owning a contiguous row range. Sub-job count
  per chunk = `ceil(rowCount / chunkSubJobRowBudget)` where the
  row budget is tuned per `JobSystem::workerCount` and the
  `preferredGrain` hint.
- Callback receives a sub-span of the chunk's entity / component
  spans rather than the full chunk — same contiguous-storage
  guarantee, narrower range. The documented invariant
  (`es.size() == component_spans[k].size()`) holds.
- Pure addition to `Query.hpp`. No public-API rename / removal.

**Test gate:**
- `tests/foreach_chunk_test.cpp` extended to cover sub-job
  dispatch (large chunk forces split → callback receives
  partial spans summing to full chunk size).
- `commit_hash_test`, `sharded_commit_test` byte-identical.
- All other chunk-iteration tests pass unchanged.

**Acceptance:**
- `bench/rpg_stress_bench` 100k row: `update` mean drops by
  ≥ 3 ms; `step` mean drops by ≥ 3 ms.
- `bench/chunk_iter_bench` (AI workload): no regression beyond
  5% on the small-chunk paths.
- `bench/foreach_bench`: no regression on small-chunk path.

**Effort:** ~2-3 days. The work is in `Query.hpp`; the existing
test coverage catches regressions immediately.

### 3.5 Batch 29 — Parallel pre-hash + serial mix-in (C2)

**Locked in by B27.** Attacks the #1 inefficiency (single-threaded
commit hash). Greenlit, but the byte-identity proof is the
load-bearing risk — if the new path can't reproduce the existing
hash byte-for-byte, B29 doesn't ship and the work flows into B30
instead.

**Hypothesis:** the FNV-1a-64 byte-mix per command (~70 ns/cmd)
runs serially on the sim thread because the recurrence
`h' = (h XOR b) * P` is sequential. But each command's payload
bytes are independent of every other command's bytes. A pre-pass
on worker threads can compute, per command, a precomputed-mix
value `H_cmd` that the serial commit can fold into the running
hash with one FNV-1a step (~6 ns/cmd) instead of ~70.

The math: with FNV-1a's recurrence, if you precompute powers
`P^n mod 2^64` and per-command "tail constants" via Horner-form
batching, the parallel-then-mix-in equivalence holds. The
batch-tail mix can fold an N-byte block in `~ceil(log N)` 64-bit
multiplies serially, vs. N steps in the naive form.

**Scope:**

- New `src/CommitHashBatching.hpp` / `.cpp` — pure-function
  helpers for `precomputeMix(byte_span, &H_cmd)` and
  `foldInto(running_hash, H_cmd)`.
- `EngineImpl::commitBuffer` precomputes `H_cmd` for each command
  in a `parallelFor` over the variant stream before applying
  them. The commit then applies serially (preserving submission
  order for `applyCommandImpl`) and folds the precomputed
  `H_cmd` into `stats.commitHash` per command.
- `Config::commitHashBatching = true` is the default; setting
  `false` falls back to today's serial byte-mix.
- The precompute pass writes to a per-engine arena reused across
  ticks (zero allocations steady state).

**Test gate:**
- `commit_hash_test.cpp` byte-identical golden. **This is THE
  load-bearing test.** Any reordering of the byte-mix produces a
  different hash and the test catches it. No B29 PR lands without
  this passing first.
- `sharded_commit_test.cpp` byte-identical golden.
- New `tests/commit_hash_batching_test.cpp` exhaustively
  cross-validates `precomputeMix + foldInto` against the naive
  byte-by-byte FNV-1a on random byte sequences (length 1 to
  4096). Property-based test with seeded fuzzer.

**Acceptance:**
- `bench/rpg_stress_bench` 100k row: `commit` mean drops by
  ≥ 5 ms.
- `bench/commit_path_bench`: setTransform single-threaded
  ns/cmd drops by ≥ 50%.
- `commit_hash_test` passes byte-for-byte against the
  pre-batch-29 reference goldens.

**Effort:** ~1-2 weeks, most of the time in the byte-identity
proof + the new property-based test.

### 3.6 Batch 30 — Per-archetype hash rollup (C3)

**Gated on B29.** Ships **only if** B29 underperforms its target
(<50% reduction in commit ns/cmd) **and** the workload demands a
v1.x speedup before v1.3.

**Hypothesis:** the strongest determinism guarantee
threadmaxx ships today is "byte-identical hash across runs given
the same command stream." If that's loosened to "byte-identical
hash across runs given the same final per-archetype state," the
per-command hash mix disappears entirely — the commit hash becomes
a function of N_archetypes per tick instead of N_commands.

Estimated step at 100k: ~6-7 ms (commit drops from 9.8 → ~1 ms).

**Contract amendment:** the hash semantics weaken. Any external
client that has recorded reference hashes (replay, lockstep,
network diff) MUST re-record. The new contract is documented as
"sufficient for run-vs-run reproducibility detection."

**Scope (if greenlit):**

- Per-archetype-chunk running hash, updated incrementally during
  commit.
- New `commit_hash_test` goldens, generated once.
- Migration doc — `doc/migration_v1_2_to_v1_3.md` — describing
  the contract change for clients with recorded hashes.
- `Config::legacyCommitHash = true` opt-out to preserve the v1.x
  semantics for a transition period (one MINOR cycle per the
  version.hpp deprecation policy).

**Test gate:**
- New `tests/archetype_hash_determinism_test.cpp`: same input →
  same hash across runs and machines (the new contract).
- Existing `commit_hash_test` updated with new goldens; old
  goldens captured in `tests/v1_2_legacy_commit_hash_test.cpp`
  exercising the `legacyCommitHash = true` opt-out.

**Acceptance:**
- `bench/rpg_stress_bench` 100k row: `commit` mean drops by
  ≥ 7 ms.
- `bench/commit_path_bench`: setTransform ns/cmd drops by
  ≥ 80%.
- Migration doc published.

**Effort:** ~2 weeks. Roughly half the time is the legacy-opt-out
plumbing + doc writing.

### 3.7 Batch 31 — Wave-scheduler micro-opts (DOWNGRADED)

**Skipped by B27 evidence.** The profile shows wave-rebuild +
per-wave dispatch is below 1% of step time at 100k entities. The
candidate optimization (compile-time-friendly wave dispatch via
pre-baked per-wave system index lists) would yield single-digit
percent of step time at the very most, against the C1+C2 wins
of 60%+.

**Re-eligible** if a future workload (multi-system DAG with 20+
systems and 8+ waves) exposes the dispatch cost. Until then,
parked. Note retained in `OPTIMIZATION_PATH.md` for traceability
but no work scheduled.

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
| `commit_bench`          | Single vs sharded commit, broad sweep                | B29, B30 |
| `commit_path_bench`     | Per-variant commit cost on Churn                     | B29, B30 |
| `event_channel_bench`   | Lock-free emit throughput                            | —        |
| `hierarchy_bench`       | HierarchySystem resolution cost                      | —        |
| `cull_bench`            | cullByFrustum items × cameras matrix                 | —        |
| `foreach_bench`         | forEachWith / Cached / Chunk sweep                   | B28      |
| `chunk_iter_bench`      | Iteration paths on canonical workloads               | B28      |
| `migration_bench`       | Per-archetype-pair migration                         | —        |
| `grain_sweep`           | preferredGrain sweep                                 | —        |
| `job_stealing_bench`    | Worker steal-ratio sweep                             | —        |
| `pack_instances_bench`  | InstanceBufferLayout::packInstances throughput       | —        |
| `resource_handle_bench` | Refcount churn under contention                      | —        |
| `simd_kernels`          | Sibling SIMD library benches                         | —        |
| **`rpg_stress_bench`**  | **B26 addition** — RPG-shaped 100k-entity tick      | **all**  |
| **`rpg_stress_probe`**  | **B27 addition** — Per-system / ablation / cmd-vol  | —        |

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
