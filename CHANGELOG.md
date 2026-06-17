# Changelog

All notable changes to `threadmaxx` are recorded here. The format
follows [Keep a Changelog](https://keepachangelog.com/); the project
adheres to [Semantic Versioning](https://semver.org/). Bump rules
are documented in `include/threadmaxx/version.hpp`.

The sibling `threadmaxx_simd` library has its own independent
changelog at `include/threadmaxx_simd/CHANGELOG.md`.

## [Unreleased]

(no changes since v1.3.0)

## [1.3.0] — 2026-06-17 — Q-audit batch + post-v1.2 accumulation

Minor release. Closes out the path-to-perfection audit batches Q1 — Q8
(public-surface manifest hygiene, doc modernization, deprecation pin,
coverage-audit refresh, MSAN documentation) on top of the post-v1.2.1
example / sibling-library accumulation that had been queued in the
Unreleased section.

### Added (core engine — Q-audit batch, 2026-06-17)

- **Q1 — manifest hygiene.** Three public headers that had landed on
  disk since v1.0 (`include/threadmaxx/AdaptiveGrainPolicy.hpp`,
  `CommitBreakdown.hpp`, `Tuning.hpp`) were never added to
  `THREADMAXX_PUBLIC_HEADERS` in `CMakeLists.txt`. The install glob
  caught them either way, but the manifest was stale. The umbrella
  header `threadmaxx.hpp` also missed `CommitBreakdown.hpp`,
  `Telemetry.hpp`, and `version.hpp`; all three now ride the
  one-include path.
- **Q2 — doc modernization.** `doc/configuration.md` and
  `doc/performance_tuning.md` now cover every post-v1.0 `Config`
  knob: the S6 / S8 / S9 / S10 / S11 / S16 sharded-commit micro-
  knobs and the T2 `preferredWorkerCap` adaptive cap. The
  configuration table is split into a "Core knobs" group and a
  "Commit-path knobs" group; the perf-tuning page gets a new
  "Sharded-commit micro-knobs" section walking each batch's bench
  rationale. The stale "no first-class time-scale support" claim is
  removed (`setPaused` / `setTimeScale` have been first-class since
  v1.1).
- **Q3 — `legacyCommitHash` deprecation enforcement.**
  `EngineImpl::initialize` now emits a one-shot `LogLevel::Warn`
  when the caller opts in to the v1.x byte-mix commit-hash path; a
  `static_assert(THREADMAXX_VERSION < 10400, …)` will force the
  cleanup (delete the knob + delete `tests/v1_2_legacy_commit_hash_
  test.cpp`) when v1.4 ships. The static_assert message names the
  artifacts to delete so the cleanup PR is self-directing.

### Changed (core engine — Q-audit batch, 2026-06-17)

- **Q4 — Doxygen `@brief` sweep.** Walked every public symbol;
  result was that the public surface is in excellent shape with no
  actionable gaps. Documented as a no-op for the record.
- **Q5 — README + CHANGELOG link audit.** Every README link
  resolves; CHANGELOG is fresh; sibling-status text matches install
  rules. Pure verification.
- **Q6 — `tests/COVERAGE_AUDIT.md` refresh.** Added the missing
  `CommitBreakdown.hpp` section; removed a self-contradiction in
  the Summary footer (the six load-bearing gaps B32 originally
  flagged were all closed in B32, but the footer still listed them
  as open); dropped a stale "Telemetry.hpp not in umbrella"
  footnote (Q1 fixed that).
- **Q8 — MSAN feasibility documented.** Tried bringing up a
  `build-msan` tree under clang 22 / `-fsanitize=memory`; the core
  library builds clean, but the first instrumented test trips
  `use-of-uninitialized-value` inside `std::basic_string::size()`
  because the system libstdc++ is uninstrumented. Documented in
  CLAUDE.md as MSAN-not-supported so the next contributor doesn't
  redo the investigation. ASAN already catches our own
  stack/heap use-of-uninit.

### Tooling

- `cmake/tsan.supp` now also silences the `pa_mutex_lock` /
  `pa_xmalloc` race TSAN reports against the uninstrumented
  `libpulsecommon-17.0` / `libpulse.so` frames in
  `tests/audio/test_audio_backend_pulse.cpp`. TSAN was failing on
  this test prior to Q1; with the suppression the whole TSAN tree
  is green again (518/518) and CLAUDE.md's "ASAN / UBSAN / TSAN
  trees pass clean" promise holds.

### Version pin

- `tests/version_test_v1_3.cpp` pins the v1.3 family
  (`version_string()` starts with `"1.3."`, packed ≥ 10300).
- `tests/version_test_v1_2.cpp` had been a tight v1.2.x family pin
  via `strncmp(v, "1.2.", 4)`; relaxed to a packed-floor check
  (`>= 10200`) so it stays true as the floor rolls forward. The
  family-pin pattern moves to the current MINOR's file as a
  convention going forward.

### Added (`examples/rpg_demo`, §3.11.9 batch D9)

- **Particle bursts.** Combat hits, NPC deaths, and pickup collects
  now emit short-lived particle entities. Each particle is a regular
  ECS entity carrying `Transform + Velocity + CubeRender +
  Particle` (a new UC); motion comes from the engine's existing
  `MovementSystem`, and the new `ParticleSystem` destroys entries
  whose `simulationTime - spawnTimeSeconds >= initialLifetime`.
  `ParticleEmitterSystem` drains the typed `DamageDealt` /
  `EntityDied` / `PickupCollected` channels each tick and burst-
  spawns 10 – 32 particles per event with a deterministic random
  velocity hemisphere.
- **`Particle` + `ParticleEmitter` UserComponents.** `Particle` is
  immutable after spawn (the engine derives remaining lifetime at
  read time rather than mutating the UC, avoiding two archetype
  migrations per particle per tick). `ParticleEmitter` is reserved
  for D10+'s per-entity emitter knobs.
- **Test:** `tests/rpg_demo/test_particle_lifetime.cpp` — spawn
  100 particles with monotonically increasing lifetimes via the
  seed CommandBuffer, advance ticks, verify exactly the expired
  cohort is destroyed and survivors are still alive. 14 total
  rpg_demo tests, 114 tree-wide.
- **Bench:** `bench/particle_storm_bench.cpp` (opt-in) — burst-
  spawn / age-out workload at 1k / 5k / 25k / 100k particles/sec
  (the engine ticks at 60 Hz, so per-tick spawn = `perSec / 60`).
  Two rows per scale: `particle_storm` for full step time,
  `particle_storm_commit` for the commit-phase slice. Note column
  carries the final-tick `commitHash` and mean live count for
  determinism diffing.
- **Engine evidence captured at 100k particles/sec (≈ 50 k live):**

  ```
  step_mean      = 12.30 ms       (within 60 Hz 16.7 ms budget)
  commit_mean    =  1.75 ms       (~14 % of step time)
  throughput     = 135 753 particles/sec sustained
  ```

  Commit time scales linearly with command volume (1.7 ms / 3.3k
  commits-per-tick ≈ 530 ns/command). No bottleneck in the v1.2
  commit path at this scale — the §3.11.9 spec's
  "transient-lifetime component class" trigger does NOT fire. The
  per-archetype hash rollup folds into `commitDurationSeconds`
  without becoming a visible cost.

### Added (`examples/rpg_demo`, §3.11.8 batch D8)

- **Larger, uneven terrain.** Replaced the single 60×60 flat ground
  cube with a `cellsPerSide × cellsPerSide` grid of static tiles
  whose heights follow a 4-octave fBm height field. Stress mode
  spawns 65 536 tiles (256×256); normal mode spawns 1 024 (32×32).
  Player and NPCs Y-snap to the terrain via the new
  `TerrainAttachSystem`. NPCBrainSystem now rejects Wander targets
  whose `slopeAt` exceeds `kSlopeRejectThreshold` (~19°) with up to
  3 re-rolls. `PickupSystem` switched to XZ-only distance check
  (3D Y-inclusive check would have missed pickups on hilltops).
- **`Heightmap` (header-only).** Deterministic seeded fBm noise
  with bilinear `heightAt` and central-difference `slopeAt`.
  Borrowed by `TerrainAttachSystem`, `NPCBrainSystem`,
  `AnimationSystem`'s bob baseline, and the new bench.
- **`TerrainPatch` UserComponent.** Per-tile metadata
  (`cellX`, `cellZ`); lives on the static terrain archetype so
  brain / combat queries skip the archetype on the chunk-mask test.
- **Test:** `tests/rpg_demo/test_terrain_lookup.cpp` — heightmap
  determinism, bilinear correctness, out-of-bounds clamp, slope
  threshold reachability (23 steep cells found at the configured
  parameters). 13 total rpg_demo tests, 113 tree-wide.
- **Bench:** `bench/terrain_query_bench.cpp` (opt-in) — measures
  single-threaded vs `forEachChunk`-parallel `heightAt` /
  `slopeAt` queries at 16k / 64k / 256k entity scales. CSV output
  matches the §3.9 schema (label, workload, entities, workers,
  grain, ns mean/stddev/p50/p95/p99, throughput, ns/query note).
- **Engine evidence captured at 256k entities:**
  `height_forEachChunk` 3.75 ns/query (4 workers, parallel)
  vs `height_single_threaded` 12.91 ns/query — clean 3.4× speedup,
  confirming the engine path is the right shape for this workload.

### Changed

- `AnimationSystem` now bobs around the *current* terrain Y rather
  than the spawn-time `AnimState::baseY` when a heightmap is
  installed. Pre-D8 behavior preserved when `WorldState::heightmap`
  is null (headless tests can opt out).
- `examples/rpg_demo/CMakeLists.txt` picks up `TerrainAttachSystem.cpp`.
  `Heightmap` is header-only; the bench includes the header directly.
- `tests/rpg_demo/test_animation.cpp` updated for the new
  terrain-aware bob baseline.

### Engine extension trigger (per GAME_EXTENSION.md §4 D8)

None fired. At 256k synthetic entities the parallel `forEachChunk`
path wins by 3.4× — no `SpatialHash` height-aware variant needed,
no `threadmaxx_terrain` sibling-library spinup. v1.2's abstractions
hold for D8.

## [1.2.1] — 2026-05-26 — JobSystem missed-wakeup fix

Patch release. Engine-internal change only; no public API surface
moved.

### Fixed

- **`JobSystem` missed-wakeup hang under nested parallelism on
  high-core hosts.** Pre-fix, `submit()` only notified the
  round-robin target worker's per-worker CV; if that worker was
  busy (typically blocked in a nested `JobLatch::wait`), the
  notify was wasted, and other parked workers — whose predicate
  was `self.hasWork() || stopping_` — re-parked on any wake
  whose work landed on a sibling queue. Stranded work was
  observable as an intermittent ~1/5 hang on
  `examples/rpg_demo/perf_audit_rpg_demo 300 --stress` at the
  auto worker count (71 on a 72-core host).
- **Fix is three cooperating pieces in `src/JobSystem.{hpp,cpp}`:**
  a monotonic `wakeSeq_` atomic bumped after every successful
  queue push (workers snapshot before parking; predicate exits
  on any advance), a `parkedCount_` atomic gating fan-out so the
  steady-state hot path pays zero extra cost when no worker is
  parked, and a single helper `notify_one` at `(target + 1) % n`
  that rotates across workers over a wave so any reachable
  parked worker is notified at least once. See the wake-contract
  comment block at the top of `src/JobSystem.hpp` and the
  "Wake contract (2026-05-25, missed-wakeup fix)" subsection in
  `CLAUDE.md`.
- **Pinned by `tests/job_system_missed_wakeup_test.cpp`** — 200
  nested-parallelism iterations at every worker count from 4 to
  `hardware_concurrency`, with a 60-second per-config watchdog.

### Performance

- No regression versus pre-bug timings: `rpg_demo --stress 300`
  at 71 workers, real 29–30 s / sys 2.9–3.0 s (pre-patch with
  bug 31.7 s / 3.1 s; intermediate semaphore + N-wide-notify
  forms regressed sys to 49–174 s before the helper-only design
  landed). Low-worker configs (4/8/16) measure sys 0.43–0.69 s.

### Notes

- Determinism contract unchanged — the fix is a scheduling-side
  wake signal and never touches storage or commit order. Same
  command stream → same `commitHash` stream.
- Do NOT re-introduce a single global wake futex (semaphore or
  otherwise): at 71 workers the contention costs ~50× the
  per-worker-CV design's sys time. Do NOT drop the `wakeSeq_`
  leg of the predicate: workers will re-park on any non-direct-
  queue wake and strand sibling work.

## [1.2.0] — 2026-05-21 — Phase 8: workload-driven library tightening

Additive minor release covering the Phase 8 work (batches 26–32). Two
opt-in correctness contracts changed defaults:

- **`commitHash` semantics weakened** from "byte-identical per
  command stream" to "byte-identical per final per-archetype state."
  The new default ships `Config::legacyCommitHash = false`; flip to
  `true` to preserve the v1.1 byte-mix path during the one-MINOR-cycle
  deprecation window. See `doc/migration_v1_2_to_v1_3.md` (existing,
  unchanged) and `doc/migration_v1_to_v1_2.md` (new, this release).
- **`SystemContext::workerCount()`** is now pure-virtual on the
  public interface. `SystemContextImpl` is the only derived class
  threadmaxx ships, so external code is unaffected unless it
  subclasses `SystemContext` directly (unsupported per the public-
  surface contract).

No symbol removals. No layout changes. No on-disk format change
(`kWorldSnapshotVersion` unchanged).

### Added (engine)

- **`Config::legacyCommitHash`** (batch 30, §3.6) — opt-out toggle
  preserving v1.1 byte-mix `commitHash` semantics.
- **`kForEachChunkSubJobThreshold` + `detail::chunkSubJobBudget()`**
  in `Query.hpp` (batch 28, §3.4) — public knob + helper that sub-job
  splits oversized chunks in `forEachChunk`.
- **`SystemContext::workerCount()`** (batch 28, §3.4) — pure virtual
  used by `forEachChunk` to size sub-job budgets.

### Added (docs)

- **`doc/performance_tuning.md`** (this release) — covers
  `preferredGrain`, `Config::singleThreadedCommit`, `setTickBudget` +
  `SkipPolicy`, the `bench/` harness, how to capture a profile, and
  how to read the telemetry sink outputs.
- **`doc/migration_v1_to_v1_2.md`** (this release) — the v1.1 → v1.2
  migration path. Tiny; lists the new opt-in knobs and the
  `legacyCommitHash` flag's lifecycle.

### Added (tests)

- **`tests/archetype_hash_determinism_test.cpp`** (batch 30) — pins
  the new `commitHash` contract (same per-archetype state → same
  hash; mask-reorder invariance; cross-validation between sharded
  and single-threaded paths).
- **`tests/v1_2_legacy_commit_hash_test.cpp`** (batch 30) — pins the
  v1.1 byte-mix contract under `legacyCommitHash = true`. Will be
  removed when the flag does.
- **`tests/for_each_serial_test.cpp`** (batch 32) — closes the
  `forEachSerial<...>` public-API gap.
- **`tests/concurrency_soak_long.cpp`** (batch 32) — opt-in 10,000-
  tick variant of `concurrency_soak_test` (gated by
  `-DTHREADMAXX_BUILD_LONG_SOAK=ON`). Not registered with ctest;
  ~6 min runtime in Release.
- **`tests/COVERAGE_AUDIT.md`** (batch 32) — 274-line public-API
  coverage audit. Six load-bearing gaps closed in B32.
- **`tests/version_test_v1_2.cpp`** (this release) — pins the v1.2
  version floor; companion to the existing `version_test`.

### Added (benchmarks)

- **`bench/scene_workloads.hpp` → `RpgStressWorkload`** (batch 26,
  §3.2) — 5-archetype 70k–105k-entity workload mirroring
  `rpg_demo --stress` at the engine level. Built-in components only.
- **`bench/rpg_stress_bench.cpp`** (batch 26) — the Phase 8 gate.
  Per-phase `EngineStats` decomposition across 10k / 50k / 100k
  scales.
- **`bench/rpg_systems.hpp`** (batch 27, §3.3) — shared system
  definitions factored from `rpg_stress_bench`.
- **`bench/rpg_stress_probe.cpp`** (batch 27) — three diagnostic
  passes (per-system breakdown, system-mix ablation, commit cost
  vs command volume).
- **`bench/profile_report.md`** (batch 27) — names the top three
  100k-entity inefficiencies and the C1/C2/C3 optimization
  candidates.
- **`bench/wave_dispatch_bench.cpp`** (batch 31, §3.7) — permanent
  diagnostic establishing the wave-dispatch overhead ceiling at
  ~500 ns marginal per wave (< 0.04% of a 16 ms step even at 32
  waves). Kept as the re-eligibility baseline.

### Changed (engine — performance + hygiene)

- **`forEachChunk` sub-job dispatch** (batch 28, §3.4). Chunks
  above `kForEachChunkSubJobThreshold = 1024` rows now split into
  ~8× workerCount sub-jobs. RpgStress 100k: movement update
  5.08 → 2.59 ms (-49%), `step` 15.6 → 13.0 ms (-2.6 ms / -17%).
  The `es.size() == component_spans[k].size()` invariant holds
  per-call as documented; the callback now fires once per sub-job
  rather than once per chunk.
- **Per-archetype hash rollup** (batch 30, §3.6). Default
  `commitHash` path replaced with an end-of-step rollup over
  per-chunk cached hashes. `ArchetypeChunk` gained
  `cachedHash` / `hashDirty`; every `mut*()` setter on
  `EntityStorage` and every `ArchetypeTable::{insert,
  removeSwapPop,migrate}` site flips the dirty bit.
  RpgStress 100k: `commit` 9.96 → 2.14 ms (-7.8 ms / -78%);
  `step` 14.43 → 4.41 ms (-69%, 137 → 42 ns/entity).
- **`JobLatch` shim** (batch 32, §3.8) replaces `std::latch` at
  five sites in `src/EngineImpl.cpp`. libstdc++'s `std::latch::wait`
  lowers to a futex and is invisible to ThreadSanitizer; the
  mutex+CV form is TSAN-visible and the cost difference is below
  the surrounding parallelism's overhead.
- **`stallTimeoutSeconds_` → `std::atomic<double>`** (batch 32) —
  relaxed-ordering for the watchdog-thread / sim-thread access.
- **`JobSystem::Worker::{ownPops, stolenJobs, histogram}` →
  atomics** (batch 32) — relaxed-ordering. Pure-mov on x86 /
  aarch64; sanitizer-clean replacement for the previously
  TSAN-flagged but intentional benign races.

### Changed (docs)

- **`include/threadmaxx/version.hpp`** macros and string bumped
  from 1.1.0 → 1.2.0.
- **`CMakeLists.txt`** `project(VERSION ...)` bumped to 1.2.0.

### Deferred / downgraded

- **Batch 29 (parallel pre-hash + serial mix-in)** — DEFERRED
  2026-05-21. The byte-identity gate is mathematically
  unachievable for FNV-1a-64 under integer multiplication mod
  2^64 (integer multiplication does not distribute over XOR).
  The C2 budget rolled into B30. Proof recorded in §3.5 of
  `OPTIMIZATION_PATH.md` and `CLAUDE.md §3.10.5`. The math
  probe at `/tmp/fnv_distrib_probe.cpp` was transient by
  design.
- **Batch 31 (wave-scheduler micro-opts)** — DOWNGRADED. The
  permanent `wave_dispatch_bench` shows dispatch overhead at
  ~500 ns marginal per wave; even a 30-40% reduction would save
  < 0.04% of a 16 ms step at 32 waves. Re-eligibility criteria
  in §3.7.

### Sanitizers

- **TSAN** (`build-tsan/` tree with `-fsanitize=thread`): 88/88
  pass with a single documented suppression for the
  `HudTraceSink` seqlock (industry standard for seqlocks; see
  `cmake/tsan.supp`).
- **ASAN + UBSAN** (`build-asan/` tree with
  `-fsanitize=address,undefined`): 88/88 clean.
- **Release + Werror trees**: 112/112 pass (was 108/108 at
  v1.1.0; +1 from `archetype_hash_determinism_test`, +1 from
  `v1_2_legacy_commit_hash_test`, +1 from `for_each_serial_test`,
  +1 from `version_test_v1_2`).

### Batch summary

Phase 8 (workload-driven library tightening) — batches 26–33:

| Batch | Status        | Win at 100k entities                |
|-------|---------------|-------------------------------------|
| B26   | gate          | Establishes 15.6 ms baseline        |
| B27   | diagnostic    | Names C1/C2/C3 candidates           |
| B28   | ✅ landed     | `step` -2.6 ms, `update` -2.5 ms    |
| B29   | ❌ deferred   | Math probe disproves byte-identity  |
| B30   | ✅ landed     | `commit` -7.8 ms, `step` -10 ms     |
| B31   | downgraded    | Bench shows < 0.04% recoverable     |
| B32   | ✅ landed     | TSAN/ASAN/UBSAN clean + long soak   |
| B33   | this release  | Docs + version bump + release polish |

Cumulative RpgStress 100k+5k: `step` 15.6 → 4.41 ms (-72%).

---

## [1.1.0] — 2026-05-18 — Vulkan skinned-mesh rendering

Additive minor release. The `examples/vulkan_renderer/` reference
renderer now supports skinned-mesh draws end-to-end; the rpg_demo
shows a single procedurally-generated 2-bone capsule with an
animated tip bone alongside the existing cube + pyramid entities.

### Added (renderer example, `examples/vulkan_renderer/`)

- **`opaque_skinned.vert`** — Vulkan vertex shader that blends up
  to 4 bones per vertex from a storage-buffer-bound bone matrix
  array. Reuses `opaque.frag` for the fragment stage.
- **`opaque_skinned` pipeline variant** in `VulkanPipelines`:
  56-byte vertex stride (`pos[3]f + normal[3]f + boneIDs[4]u32 +
  boneWeights[4]f`), descriptor set 0 / binding 0 SSBO layout for
  the bone matrix array. Same hot-reload + swapchain-recreate
  paths as the existing pipelines.
- **`VulkanRenderer::registerSkinnedMeshFromData(vertices, indices)`** —
  uploads a skinned-vertex-layout mesh and returns a non-negative
  `skinnedMeshId`.
- **`VulkanRenderer::setBoneMatrices(span<const float>)`** —
  per-frame bone-matrix push. Column-major mat4s packed
  contiguously. The renderer copies into the back PerFrame's
  bone buffer + updates its descriptor set.
- **Per-frame bone descriptor pool + per-PerFrame descriptor set**
  in `VulkanRenderer::Impl`. Allocated once at init; descriptor
  writes happen lazily on first / resized `setBoneMatrices` call
  per frame slot.
- **Skinned-aware draw dispatch** in `recordCamera`. The existing
  per-meshId bucket loop is now keyed by `(meshId, skinned)`;
  buckets with `skinned == true` route to `opaqueSkinnedPipe()`
  + bind the bone descriptor set + draw against the matching
  skinned mesh from `skinnedMeshSlots`.

### Added (rpg_demo, `examples/rpg_demo/`)

- **`SkinnedCapsule.{hpp,cpp}`** — pure-CPU procedural generator
  for a 12-vertex 2-bone "stick figure" mesh (3 rings × 4 verts
  each; ring 0 → bone 0, ring 1 → 50/50 blend, ring 2 → bone 1).
- **`SkinnedRenderSystem`** — single hardcoded skinned DrawItem at
  world position (8, 0, 5) with `skeletonId = 0` (dispatch flag)
  + `pose.ringSlot = 0` (bone-base offset). Falls silent when
  `worldState_->skinnedMeshId == 0` (headless / unregistered).
- **`WorldState::skinnedMeshId`** — populated by `main.cpp` after
  `engine.initialize` if `registerSkinnedMeshFromData` succeeded.
- **`main.cpp` per-frame skinning**: registers the procedural
  capsule once + pushes 32 floats (2 × mat4) per tick via
  `setBoneMatrices`. Bone 0 stays identity; bone 1 rotates around
  Z with a sine wave for visible motion.

### Contract note

`DrawItem::skeletonId` (existing, batch-8 public type) is now the
documented "use the skinned pipeline" dispatch signal in the
Vulkan reference renderer: any value `>= 0` routes through the
opaque-skinned pipeline. `pose.ringSlot` is the renderer-defined
bone-base offset into the buffer passed to `setBoneMatrices`.

### Deferred to v1.x

- **glTF skinned-mesh import** — the procedural-capsule approach
  in v1.1 proves the end-to-end pipeline works; real asset import
  remains a v1.x sibling-library candidate. See FUTURE_WORK
  §3.11.7b.5 for the scope sketch.
- **Multi-skeleton / multi-character scenes** — the current
  `setBoneMatrices` API supports it (just pass more matrices,
  per-DrawItem `pose.ringSlot` indexes), but no demo currently
  exercises N > 1 skeletons.

### Verification

- Both `build/` and `build-werror/` clean.
- ctest 108/108 on both trees (no test changes; new functionality
  is renderer/demo-side).
- rpg_demo runs validation-clean for 60 ticks with the skinned
  capsule registered + drawing (`THREADMAXX_VK_VALIDATE=1`).

---

## [1.0.0] — 2026-05-18 — Production-ready close-out

### Added

- **`include/threadmaxx/version.hpp`** — `THREADMAXX_VERSION_MAJOR/MINOR/PATCH`
  macros, packed `THREADMAXX_VERSION` integer, and `version_string()`
  for runtime logging.
- **`CHANGELOG.md`** — this file. Distills the batch history from
  `FUTURE_WORK.md` into per-release notes.
- **`cmake/threadmaxxConfig.cmake.in`** — install package config
  template (was referenced by CMakeLists.txt but missing on disk;
  `find_package(threadmaxx)` now works after install).
- **`tests/version_test.cpp`** — gates the version macros + helper.

### Changed

- **`CMakeLists.txt` `project()` VERSION**: 0.1.0 → 1.0.0.
- **README.md**: status flipped from "early but functional" to
  "production-ready (v1.0.0)"; doc index added; test count updated
  to 108.

### Batch summary up to 1.0.0

Every prior batch (1 → 24) landed under the 0.x line and is captured
in detail in `FUTURE_WORK.md`. The 1.0.0 release seals the public API.

---

## [0.24.0] — 2026-05-18 — Batch 24 (close §3.10.3 rolling items)

### Added

- **F13 — `World::forEachChunkOf(ComponentSet, fn)`** introspection
  helper. Iterates non-empty archetype chunks satisfying a mask
  predicate. Skips the engine's startup-allocated empty
  `all()`-masked chunk automatically.
- **F8 — `Engine::events<T>()` thread_local cache** with per-engine
  serial. Replaces ~30 ns mutex acquisition with a single
  atomic-load + compare on the hot path. UAF-safe across
  engine recreation at recycled addresses.
- **`Engine::engineSerial()`** `@internal`-tagged public accessor —
  returns the unique non-zero serial assigned at construction.
- 2 new tests: `tests/world_for_each_chunk_of_test.cpp` and
  `tests/event_channel_cache_test.cpp`.

---

## [0.23.0] — 2026-05-17 — Batch 23 (ergonomics rolling)

### Added

- **F11 — `Engine::userComponent<T>()`** — lazy lookup of a
  registered `UserComponentId` by `typeid(T)`.
- **F12 — `CommandBuffer::spawnBundleN(spans)`** — bulk-spawn
  helper pairing reserved handles with Bundles.
- **F10 — `Bundle::with<T>(value)`** — chainable builder that sets
  the field AND the matching presence bit.

---

## [0.22.0] — 2026-05-16 — Batch 22 (audit-driven hygiene)

### Changed

- Wave-parallel system updates route through `jobs_->submit` +
  `std::latch` instead of spawning raw `std::thread`s.
- `HierarchySystem` scratch state is now system-member state,
  reused across ticks.
- `prevTransformByIndex_` flat vector replaces the old
  `prevTransformMap_` `unordered_map`.

### Reverted

- F8 (event-channel `thread_local` cache) — initial attempt UAFed
  on engine recreation at recycled addresses; deferred to batch 24
  with a per-engine serial fix.

---

## [0.21.0] — 2026-05-16 — Batch 21 (sharded-commit fast paths)

### Added

- Pre-check fast-fallback in `commitBuffersSharded`: total commands
  < 256, no value-only commands, or chunk count < 2 → falls
  through to single-threaded `commitBuffer`.
- Engine-owned `shardMigratingBitmap_` + `shardChunkBins_` reused
  across calls.

### Documented

- Bench data showing sharded never beats single-threaded on the
  measured workloads. `singleThreadedCommit = true` remains the
  default.

---

## [0.20.0] — 2026-05-16 — Batches 16–20 (post-Milestone-6 perf)

### Added

- **Batch 16**: workload-realistic bench harness (`bench/`).
  4 scene-shaped end-to-end benches over 3 canonical workloads
  (AI-only, Render+AI, Churn).
- **Batch 17**: `forEachWith` rewritten to walk chunks internally
  (70% improvement on AI workload).
- **Batch 18**: `CmdSpawn` + `CmdAddUserComponent` moved behind
  `unique_ptr` in the variant — `Command` variant shrank from 256 B
  to 64 B.
- **Batch 19**: migration-batching hint in `EngineImpl::commitBuffer`.
- **Batch 20**: `FileTraceSink::setAsync(true)` + `Engine::snapshotAsync`
  for off-thread I/O.

---

## [0.15.0] — 2026-05-15 — Batch 15 (pre-batch-9 API polish + tests)

### Added

- **15a**: `IRenderer::onResize(w, h)` + `Engine::notifyResize`,
  `Engine::workerCount()`, `SystemStats::buildRenderFrameSeconds`,
  `RenderFrame::prevTransforms`,
  `RenderFrame::cameraIndexById`, owning-string `DebugText`
  overload, `ResourceHandle<T>::get()` / `->` / `*`.
- **15b**: industrial-grade test additions (concurrency soak,
  stitched-view concurrency, render-pass ordering, frame
  interpolation, file-trace rotation, visibility 32-camera cap,
  resize routing, resource-handle indirection, debug-text owning,
  per-system build_render_frame timing).

---

## [0.14.0] — 2026-05-15 — Batch 14 (telemetry ingestion)

### Added

- `ITraceSink` engine-streamed per-tick consumer.
- `FileTraceSink` (rolling Chrome-trace JSON).
- `HudTraceSink` (seqlock-protected latest snapshot).
- `FrameBudgetWatcher` system + `BudgetExceeded` event.
- Stall watchdog via `Engine::setStallTimeout` + `EngineStall` event.

---

## [0.13.0] — 2026-05-15 — Batches 13a/b/c (storage contention)

### Added

- **13a**: per-tick `commitHash` (FNV-1a-64) determinism safety net.
  `Config::singleThreadedCommit` toggle + `logCommitHashEvery`.
- **13b**: sharded commit path (`commitBuffersSharded`) — opt-in
  parallel apply when `singleThreadedCommit = false`.
- **13c**: lock-free MPSC event channels (Treiber-stack `emit`),
  `WorldView` wave-scoped chunk-pointer cache, opt-in benchmarks.

---

## [0.12.0] — 2026-05-14 — Batch 12 (cancellation + budgets)

### Added

- `Engine::setTickBudget(seconds)` + `SkipPolicy::Budget` / `Scripted`.
- `ISystem::skippable()` opt-in flag.
- `SystemContext::shouldYield()` cooperative-cancel poll.
- `SystemSkipped` event POD.
- `JobPriority` (`High` / `Normal` / `Low`) on `parallelFor`.
- `IResourceLoader::cancel(Engine&)` per-tick cancellation hook.

---

## [0.11.0] — 2026-05-14 — Batch 11 (frame task graph)

### Added

- `TaskTag { string_view name; uint64_t hash }` POD.
- `ISystem::dependencies()` / `provides()` / `preferredGrain()`
  optional virtuals.
- Rewritten `rebuildWaves` (topological sort + greedy packing).
- `Engine::taskGraphSnapshot()` → `vector<TaskGraphNode>` for
  HUD / Graphviz export.

---

## [0.10.0] — 2026-05-15 — Batch 10 (RPG demo example)

### Added

- `examples/rpg_demo/` — 3D RPG demo (1 player, 50 NPCs, 100
  pickups) integrating combat, save/load, multi-camera frustum
  culling, quest tracking, scale stress, procedural animation,
  preload + hot reload.
- 10 demo systems + headless test suite at `tests/rpg_demo/`.

---

## [0.9.0] — 2026-05-15 — Batches 9 + 9b.x (Vulkan reference renderer)

### Added

- **Batch 9**: Vulkan 1.3 reference renderer at
  `examples/vulkan_renderer/`. VK_KHR_dynamic_rendering pipeline,
  multi-camera viewport support, mesh / texture / shader loaders.
- **Batch 9b.1**: OBJ mesh parser (sibling-library in
  `examples/rpg_demo/`).
- **Batch 9b.2a**: `MeshLoader::createMesh` + `VulkanRenderer::setDefaultMesh*`
  end-to-end OBJ→GPU upload.
- **Batch 9b.2b**: multi-mesh dispatch (per-meshId draw loop +
  `VulkanRenderer::registerMesh*` + `CubeRender::meshId`).
- **Batch 9b.3**: shader file I/O + renderer-side `AssetReloaded`
  pipeline rebuild.

### Deferred to v1.x

- **Batch 9b.4**: full Vulkan bone-weighted skinning pipeline.

---

## [0.8.0] — 2026-05-14 — Batch 8 (render contract expansion)

### Added

- `include/threadmaxx/render/` hierarchical render PODs (Camera,
  Light, DrawItem with auxiliary refs, DebugLine / DebugPoint /
  DebugText, RenderPasses, RenderFrameBuilder, Visibility frustum
  helpers, InstanceBufferLayout, UploadRing).
- `ISystem::buildRenderFrame(RenderFrameBuilder&)` lifecycle hook.
- `RenderFrame` now carries both flat `instances` (Milestone-1
  auto-populated) and hierarchical `drawItems[pass]` / `cameras` /
  `lights` / `debugLines` / `debugPoints` / `debugText`.

---

## [0.7.0] — 2026-05-14 — Batch 7 (resource & event maturity)

### Added

- `ResourceHandle<T>` (refcounted RAII), `ResourceRegistry::addRefCounted`
  / `acquire` / `refCount`.
- Hot reload: `Engine::markResourceStale<T>(id)` + `EventChannel<AssetReloaded>`.
- `Engine::preloadUntil(predicate, timeout)` blocking yield loop.
- `IResourceLoader::onShutdown` / `markStale` / `stats` virtuals;
  `LoaderStats` POD; `Engine::aggregateLoaderStats()`.
- `Subscription` RAII subscription handle via
  `EventChannel<T>::subscribeScoped`.

---

## [0.6.0] — 2026-05-14 — Batches 6 / 6a / 6b (archetype storage)

### Added

- **Batch 6a**: generic `CommandBuffer::addComponent<T>` /
  `removeComponent<T>`, `World::archetypeSignatures()`,
  `archetype_storage_stress_test.cpp` baseline.
- **Batch 6**: chunked archetype storage. `ArchetypeChunk` /
  `ArchetypeTable`; physical migration on mask change;
  `forEachChunk<Required...>` parallel iterator;
  `World::archetypeChunkCount()` / `archetypeChunk(i)`; lazy
  stitched view for legacy parallel-vector API.
- **Batch 6b**: `UserComponent<T>` extension hook.
  `Engine::registerUserComponent<T>` → `UserComponentId`;
  `addUserComponent` / `removeUserComponent`; `user::has` /
  `tryGet` / `chunkSpan`; `World::locate`.

---

## [0.5.0] — 2026-05-13 — Batch 5 (data model widening)

### Added

- `ComponentSet` widened to 64 bits.
- 6 new POD components: `Health`, `Faction`, `AnimationStateRef`,
  `PhysicsBodyRef`, `NavAgentRef`, `BoundingVolume`.
- 3 tag-only categories: `StaticTag`, `DisabledTag`, `DestroyedTag`.
- `CommandBuffer::addTag` / `removeTag`; `World::hasTag`.
- `MaskCache` + `forEachWithCached`.
- N-tick determinism golden test.
- `kWorldSnapshotVersion` bumped 1 → 2.

---

## [0.4.0] — 2026-05-13 — Batch 4 (observability + M1 polish)

### Added

- `JobSystemStats::jobDurationHistogram` (16 log2-µs bins).
- `SystemStats::waitSeconds` + `peakQueueDepth`.
- `ChromeTraceWriter` (move-only streaming sink).
- `ILogger` + `DefaultLogger` (engine routes startup/shutdown messages).
- Persistent `EventChannel::subscribe` / `unsubscribe`.
- `HierarchyConfig::propagateScale` opt-in knob.
- `Serialization.hpp` + `WorldSnapshot` + `World::snapshot()`.
- `registerSystemAt` (insertion-order injection).
- `Bundle` / `spawnBundle` variadic spawn helper.
- `World::has<T>` / `get<T>` / `hasTag`.
- `EventChannel::subscribeScoped` (RAII `Subscription`).

---

## [0.3.0] — 2026-05-13 — Batch 3 (ergonomics + tracing)

### Added

- `Engine::reserveEntityHandle` + `SystemContext::reserveHandle` +
  batch variants.
- `ScratchArena` (chained-slab bump allocator).
- `EventChannel<T>` typed double-buffered queue + tick-end drain.
- `Engine::setTimeScale` + `Engine::setPaused`.
- `FrameSnapshot` + `writeJsonLines`.
- `IResourceLoader` per-tick async loader pump.
- `SpatialHash<Payload>` uniform-grid index.
- Parent auto-derive in `defaultSpawnMask`.
- `EventChannel<AssetReloaded>` event POD.

---

## [0.2.0] — 2026-05-13 — Batch 2 (lifecycle + time control)

### Added

- `ISystem::preStep` / `postStep` lifecycle hooks (serial).
- `ScratchArena` (chained slabs).
- `EventChannel<T>` initial implementation.
- Reserved entity handles.
- `Engine::quitRequested()`.

---

## [0.1.0] — 2026-05-12 — Batch 1 (initial public release)

### Added

- Core simulation loop with fixed-step `step()` + interpolated `run()`.
- `Engine` + `World` + `ISystem` + `CommandBuffer` + `IGame`.
- Per-worker work-stealing `JobSystem`.
- `SystemContext::parallelFor` / `single`.
- Wave scheduler based on `ISystem::reads` / `writes` masks.
- `EngineStats` + `SystemStats` per-tick reporting.
- `World::transforms` / `velocities` / `renderTags` / `userData`
  stitched parallel-vector views.
- `IRenderer` interface + `RenderFrame` (flat `instances` lane).
- Basic `IResourceLoader` registration.
- `examples/minimal/` smoke + `examples/boids/` SDL2 demo.
