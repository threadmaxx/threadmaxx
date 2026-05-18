# threadmaxx — Future Work

This document is the planning guide for extending `threadmaxx` from an
early renderer-agnostic backend into a production-ready library suitable
for a 3D RPG. It is written in a Claude Code–friendly style: practical,
phased, and implementation-oriented.

Three things live here:

1. **§1 Target outcome.** What we are aiming at.
2. **§2 Completed batches.** What recently landed, kept as a brief
   changelog so the roadmap stays honest. Detailed notes live in the
   per-feature docs and in `CLAUDE.md`.
3. **§3 Planned batches.** The forward-looking, multi-batch plan that
   carries the library through Milestones 2, 3, and 4 — i.e. to the
   point a proper 3D RPG example can be built on top of it.

Sections §4–§11 are unchanged scope/process/principles material.

Last refreshed: 2026-05-16 (Milestones 1–6 are all closed.
§3.9 (post-Milestone-6 perf plan) and §3.10.1–§3.10.2
(sharded-commit deep-dive + audit-driven hygiene) all landed
2026-05-16. §3.10.3 (ergonomics + polish) remains the next
planned tranche. §3.11 (RPG-demo-driven library exercise
plan) kicked off 2026-05-16 with batches **D1** (combat +
hierarchy + damage events), **D3** (save / load with user
components), and **D2** (multi-camera + frustum culling)
all landed. The D2 batch added the engine's first
deliberate public-API extension since §3.10 —
`Camera::viewport` (additive POD) — under the
conservative-expansion policy because the mini-map cannot
share the swapchain without it. **Batches 16–22 + D1 +
D3 + D2 all landed 2026-05-16. §3.11 batch D-audit
(2026-05-16) introduced the headless `tests/rpg_demo/` suite +
`rpg_demo_core` static library, then fixed two combat bugs the
test suite immediately exposed: sword tip extended in world
+Z (behind player) instead of player-local -Z, and player
Transform.orientation was never updated from PlayerState.yaw
so the sword stayed in fixed world space regardless of camera.
Combat now connects. **Batch D5** (scale stress + tick budget
HUD) followed, adding a `--stress` CLI flag that scales the
demo to 60,000 entities and turns on the engine's tick-budget
skip policy; cosmetic systems (DebugOverlay, DayNight, HUD)
declare `skippable()`, the NPC brain bails early via
`ctx.shouldYield()`, and HudSystem surfaces live skip counts +
`BudgetExceeded` alerts. **Batch D4** (quest system +
scripted scenarios) followed with two persistent quests
plumbed via `EventChannel<QuestProgressed>` and a
`test_determinism` regression test that runs identical
scripted inputs twice and asserts `EngineStats::commitHash`
matches per-tick. ctest now reports 88/88 on both trees. **Batch D6**
(animations + skinning) followed with the procedural Y-bob
path: every moving entity bobs in proportion to its speed,
NPCs are out of sync via per-NPC phase, and the
`AnimState` user component round-trips through save/load.
The full Vulkan skinning pipeline is deferred to a future
renderer-side batch (`batch 9b` placeholder) — unit cubes
don't visibly benefit from bone-weighted skinning so the
procedural shortcut is the right gameplay payoff. **Batch
D7** (real assets + hot reload) closes the §3.11 plan with
a procedural-path `PreloadLoader` IResourceLoader, a
boot-time `preloadUntil` call that drains 64 fake assets
before the first tick, F12-triggered `AssetReloaded`
event-flow demonstration, and aggregate asset-stats in the
HUD. Real `.obj` / shader file I/O + renderer-side pipeline
rebuild remain deferred to `batch 9b`. ctest now reports
91/91 on both trees; the §3.11 demo-driven exercise plan
is complete.** Batch
16 (gate) shipped three canonical workloads + four bench
binaries + shared `bench/common.hpp`. Batch 17 (chunk
iteration micro-optimization) rewrote `forEachWith` to walk
chunks via `WorldView`: AI-workload `forEachWith` **41 → 12
ns/entity (−70%)**, `forEachChunk` **19 → 12 ns/entity
(−35%)**. Batch 18 (command buffer compaction) moved
`CmdSpawn` + `CmdAddUserComponent` to `std::unique_ptr`-backed
variant alternatives, shrinking `sizeof(detail::Command)`
from **256 B → 64 B** (4×) — Churn workload
`setTransform`/`setVelocity`/`addRemoveTag` improved
**7–11%** single / **3–9%** sharded. Batch 19 (migration
batching) added a `reserveChunkRows` hint + adjacent-run
detection in `commitBuffer`: migration_bench's low-density
tail improved **18–26%** (256 mig/tick: 307 → 251 ns/mig;
scene-50pct@1k: 250 → 185 ns/mig). Batch 20 (async snapshot +
trace-sink) shipped `FileTraceSink::setAsync(bool)` and
`Engine::snapshotAsync(callback)` — both opt-in, both with a
dedicated background writer thread; no perf gate, pure QoL.
All five batches preserved the `commit_hash_test.cpp`
byte-identical golden. Both trees now pass ctest **80/80**
(was 79/79; +1 from `async_snapshot_test`). The §3.9 plan is
complete; further perf work past this point starts its own
§3.x section against fresh profiling evidence. All prior
batch context (1–15) is preserved below.)

## 1. Target outcome

`threadmaxx` should evolve into a reusable C++20 backend that can support:

- large world simulation,
- streamed 3D environments,
- many AI agents,
- animation and physics at scale,
- networked gameplay,
- multiple renderer backends,
- deterministic or semi-deterministic simulation modes,
- and a stable public API that game projects can depend on for years.

The current project has the right high-level shape: fixed-step
simulation, worker pool, command buffering, renderer abstraction, and
PImpl isolation. The remaining work is mostly breadth, scalability, and
production ergonomics — i.e. turning a good internal backend into a
fully usable engine library.

## 2. Completed batches

Thirteen batches plus a prep slice and three safety-net / close-out
slices have landed, closing Milestones 1, 2, and 3, the M4 contract,
and §6 phases 3+4+5+6 of M5. The remaining §3 items are the example
projects (Vulkan renderer batch 9, RPG demo batch 10) — both
`examples/...`, neither library-side. Batches 1–3
brought **Milestone 1** to
completion on 2026-05-13; batch 4 (2026-05-14) closed out the M1
polish and seeded the tracing maturity batches 5+ build on; batch 5
(2026-05-14) widened the data model for Milestone 2 — six new POD
components, three tag-only categories, a 64-bit `ComponentSet`, and
the `MaskCache` opt-in fast path; batch 6a (2026-05-14) shipped the
non-storage half of batch 6 — generic per-component transition API,
archetype signatures helper, and the storage-churn determinism
stress test; batch 6 (2026-05-14) shipped the chunked archetype
storage itself, migrating `EntityStorage` to `ArchetypeChunk`-keyed
dense arrays while preserving the public API via a lazy stitched
view; batch 6b (2026-05-14) shipped the user-extensible dense
component hook — `Engine::registerUserComponent<T>()`,
`UserComponentId`, `addUserComponent` / `removeUserComponent` free
functions, the `user::has` / `tryGet` / `chunkSpan` read helpers,
and the per-chunk `UserComponentColumn` storage that migrates
correctly across mask edits; batch 7 (2026-05-14) shipped resource
& event maturity for Milestone 3 — refcounted handles, hot-reload
protocol, loader shutdown hook + stats, blocking preload, and an
RAII event subscription handle; batch 8 (2026-05-14) shipped the
hierarchical render contract for Milestone 4 prep — cameras,
lights, per-pass draw bins, debug geometry, the `buildRenderFrame`
lifecycle hook, visibility-culling helpers, an instance-buffer
layout helper, and a per-frame upload ring. All shipped items were
pure additions to the public API or behavior-preserving internal
refactors; the only removal was an internal dead field on
`CmdSpawn` (batch 3). Detailed per-feature notes live in `doc/` and
`CLAUDE.md`.

Note on numbering: batches 7 and 8 landed before batch 6 because
§3.1's own deferral guidance ("intentionally deferred until the
renderer-side and resource-side contracts have stabilized") pointed
at doing the resource and render batches before the archetype
refactor. Batch 6a extracted everything in batch 6 that does NOT
require storage restructuring — the public API surface that the
storage refactor must preserve — so the refactor itself could change
implementation without churning the public API. Batch 6 then shipped
the chunked-storage refactor itself in one go, leaving
`UserComponent<T>` (the user-extensible dense array hook) as a
follow-on slice tracked in §3.1 as batch 6b. The batch numbers
reflect the *planned* sequence, not the order they shipped.

### Batch 1 — instrumentation, sharding, presence-aware queries

- Per-system timing and command stats (`SystemStats`, `Engine::systemStats()`).
- Sharded / work-stealing `JobSystem` (per-worker deques, no single hot
  mutex). Stress test in `tests/job_system_stress_test.cpp`.
- Per-entity `ComponentMask` + presence-aware queries (`forEachWith<...>`).
- `Parent` component + `makeHierarchySystem()` (DFS-with-memoization).
- Typed `ResourceId<T>` + thread-safe `ResourceRegistry`.

### Batch 2 — lifecycle, scratch, events, time control, reservations

- `preStep` / `postStep` hooks (serial, registration order).
- Per-job `ScratchArena` (chained-slab bump allocator).
- Typed `EventChannel<T>` (double-buffered, drained at tick end).
- `Engine::setTimeScale` / `setPaused`.
- Reserved spawn handles (`Engine::reserveEntityHandle`,
  `SystemContext::reserveHandle`).
- `EngineStats::commitDurationSeconds`; `JobSystemStats` counters.

### Batch 3 — ergonomics, tracing, async-loader contract, spatial hash

- `World::has<T>` / `World::get<T>`.
- `Bundle` + `cb.spawnBundle` (variadic factory; compile-time mask).
- `Engine::registerSystemAt` (insert at registration index).
- `Engine::reserveEntityHandles(count, span)` batch form.
- Parent auto-derive in default-mask `cb.spawn(...)`.
- `Engine::frameSnapshot()` + `writeJsonLines` in `Trace.hpp`.
- `IResourceLoader` contract + per-tick pump in `EngineImpl::step`.
- `SpatialHash<Payload>` header-only uniform-grid helper.

The public surface gained `Trace.hpp` and `SpatialHash.hpp`; everything
else extended existing headers.

### Batch 4 — observability + Milestone-1 polish

- `Serialization.hpp` — per-component `serialize` / `deserialize`
  trait pair plus `World::snapshot()` and a `WorldSnapshot` POD;
  binary format with magic+version header. Game-side restoration
  flows through `cb.spawn`.
- `JobSystemStats::jobDurationHistogram` — 16 log2-µs bins populated
  by per-worker accumulators, merged on read. `JobSystem::outstanding()`
  exposed for queue-depth sampling.
- `ChromeTraceWriter` — streaming Chrome Trace Event Format serializer
  alongside `writeJsonLines`. Built on `frameSnapshot()`.
- `EventChannel<T>::subscribe(fn)` / `unsubscribe(id)` — persistent
  per-channel callback list, invoked at drain time before the
  front/back swap.
- `HierarchyConfig::propagateScale` — opt-in scale chaining knob on
  the hierarchy system. Default off preserves prior behavior.
- `SystemStats::waitSeconds` / `peakQueueDepth` — already-computable
  primitives surfaced on the stats struct.
- `ILogger` / `Engine::setLogger` — tiny `log(level, message)`
  virtual; default sink writes to `std::cerr` at `Warn+`. Engine
  routes init / shutdown / registration / loader-error messages
  through it.

### Batch 5 — data-model widening (Milestone 2 prep)

- **64-bit `ComponentSet`.** Underlying type of `Component` is now
  `std::uint64_t`; `ComponentSet::all()` returns `0xFFFFu` (16
  allocated bits) with 48 spare. Pure internal change behind the
  existing API; serialization version bumped from 1 to 2.
- **Six new POD components** added end-to-end (enum bit, dense
  storage in `EntityStorage`, swap-and-pop branch, `mut*`, all four
  `spawn` overloads, `CmdSet*` variants, `bundle()` support,
  `World::has<T>` / `get<T>` / `tryGet*` / dense span, `Query`
  helpers, `serialize` / `deserialize`, `WorldSnapshot` field):
  - `Health { float current, max; }`
  - `Faction { std::uint32_t id; }`
  - `AnimationStateRef { ResourceId<AnimationGraph> graph; uint32_t state; float t; }`
  - `PhysicsBodyRef { std::uint64_t handle; }`
  - `NavAgentRef { std::uint64_t handle; }`
  - `BoundingVolume { Vec3 min, max; }`
  Auto-attach via per-component setter: writing the value also sets
  the presence bit. None of the six is in the default-mask
  `cb.spawn`; opt in via `Bundle` or explicit-mask overload.
- **Tag-only components** — `Component::StaticTag`,
  `DisabledTag`, `DestroyedTag`. No dense storage, presence-bit
  only. Race-free single-bit composition via `cb.addTag` /
  `cb.removeTag`; observation via `world.hasTag`.
  `EngineImpl::buildRenderFrame` skips `DisabledTag` entities.
- **`MaskCache` + `forEachWithCached`** — opt-in fast path for
  `forEachWith` when the world's mask shape is stable across ticks.
  User-owned cache rebuilt in `preStep` with `cache.rebuild(world,
  required<T...>())`; the hot loop iterates the cached indices and
  skips the per-entity mask test.
- **N-tick determinism golden test** — `tests/determinism_golden_test.cpp`
  runs a 64-tick seeded scenario twice, FNV-1a-hashes the
  `WorldSnapshot` byte streams, and asserts the hashes agree. Cheap
  regression guard for the §3.1 archetype refactor (and was already
  in place when batch 8 — render expansion — landed in §2).

`UserComponent<T>` (the user-extensible dense-array hook) is
explicitly deferred to §3.1 batch 6's archetype refactor — patching
the parallel-vector storage to hold type-erased extra arrays is
invasive enough that doing it twice (once here, once in batch 6)
is the wrong shape. The §3 plan reflects this deferral.

### Batch 6a — Archetype prep (Milestone 2 prep)

Shipped 2026-05-14. The non-storage half of §3.1 batch 6:
everything that does NOT require restructuring `EntityStorage`'s
parallel-vector layout, so that the actual chunked-storage refactor
can switch implementation without churning the public API. All
additions are header-only or pure extensions to existing files.

- **`CommandBuffer::addComponent<T>(e, value)`** — generic per-type
  transition entry. Lowers to the matching `setX` (writing the
  dense value) AND an unconditional `addTag(e, bitFor<T>())` so the
  presence bit is always attached, regardless of value semantics.
  Different from `setRenderTag` (auto-clears the bit on `meshId<0`)
  and `setParent` (auto-clears on invalid parent handle):
  `addComponent` always means "the entity logically carries T."
- **`CommandBuffer::removeComponent<T>(e)`** — detach by clearing
  the presence bit. In batch-6a's parallel-vector storage the
  dense slot is left intact (callers treat the bit, not the slot,
  as the source of truth); batch 6 then made `removeComponent`
  physically migrate the entity, and `tryGetT(e)` now returns
  `nullptr` after the transition.
- Both methods are header-only inline templates in
  `CommandBuffer.hpp`; tag-only categories trip a `static_assert`
  with a message pointing at `addTag`/`removeTag`.
- **`World::archetypeSignatures()`** — read-only inventory: a
  `std::vector<ArchetypeSignature>` of distinct per-entity
  `ComponentSet` values currently live, with per-mask counts.
  Sorted by `mask.bits()` ascending for stable ordering. O(N) in
  batch 6a; batch 6 dropped it to O(num archetypes) by reading
  straight from the table.
- **`tests/archetype_storage_stress_test.cpp`** — the determinism
  baseline the storage refactor must preserve. 8192 entities, 24
  ticks of spawn / per-tick `addComponent`+`removeComponent` on
  Health / StaticTag flip / per-tick transform integration. FNV-1a-
  hashed twice; hashes must match across runs. Also asserts the
  archetype-signature row counts sum to `world.size()` (the
  storage-shape invariant).
- **Doc updates** — `doc/command_buffers.md` gains a "Per-component
  transitions" section; `doc/components_and_queries.md` gains an
  "Inspecting archetype distribution" section; `CLAUDE.md` gains a
  §3.1-batch-6-prep section AND the "Adding a new built-in
  component" recipe is extended with the addComponent dispatch
  step. `README.md` test count 52 → 55.

55 tests pin the documented invariants.

### Batch 6 — Chunked archetype storage (Milestone 2)

Shipped 2026-05-14. The disruptive part of the original batch-6
plan: replacing `EntityStorage`'s parallel `std::vector`s with
archetype-keyed chunks. Public-API signatures locked down by batch
6a were preserved bit-for-bit; the test suite's determinism
baselines hash-match run-to-run (the stress test asserts h1 == h2,
unchanged).

- **`ArchetypeChunk` + `ArchetypeTable`** —
  `include/threadmaxx/internal/Archetype.hpp` defines the per-mask
  dense storage. One chunk per unique `ComponentSet`; only the
  component vectors whose bits appear in the chunk's mask are
  populated. `ArchetypeTable` owns the chunk list and the
  `mask.bits() → chunk index` lookup map.
- **`EntityStorage` rewired.** Slots now carry `(archetypeIndex,
  row)` instead of `denseIndex`; spawn/destroy/mut* delegate to
  `ArchetypeTable::insert` / `removeSwapPop` / `migrate`. The
  reservation lifecycle (§3.5) is unchanged: a reserved slot
  occupies no chunk row, and `materializeReserved` is the path that
  inserts into the destination archetype.
- **`EntityStorage::setMaskAndMigrate(handle, newMask)`** — the
  commit-phase entry point for every mask change. The `addTag` /
  `removeTag` / `setComponentMask` / `setRenderTag` (when meshId
  crosses 0) / `setParent` (when parent validity flips) / the §3.1
  batch-5 setters when they add their bit, all funnel through this.
  A self-mask migration is a no-op fast path.
- **Public `forEachChunk<Required...>(ctx, fn)`** in `Query.hpp`.
  Iterates archetype chunks whose mask is a superset of the
  required set, hands the callback contiguous `std::span`s for the
  chunk's entities and each requested component, runs one job per
  matching chunk via the existing `parallelFor` machinery.
- **`World::archetypeChunkCount()` / `archetypeChunk(i)`** — the
  raw chunk-access primitives that `forEachChunk` is built on.
  Game code that needs a custom traversal strategy uses these
  directly without going through `impl_()`.
- **Lazy stitched view.** `EntityStorage::ensureStitched()` rebuilds
  flat per-component vectors on demand so the legacy
  `World::transforms()` / `velocities()` / etc. spans keep working;
  any mutation marks the cache dirty and the next read pays the
  rebuild. Single-archetype worlds (the common case at world boot)
  pay zero stitching overhead because there's only one chunk to
  copy.
- **Render-frame builder migrated to chunks.**
  `EngineImpl::buildRenderFrame()` now walks
  `world.archetypes().chunks()` and skips entire chunks lacking
  `RenderTag` or carrying `DisabledTag` — the per-row mask test
  disappeared.
- **`tryGetT(e)` now returns `nullptr` when the entity's archetype
  doesn't carry T.** This is the post-removeComponent behavior the
  batch-6a doc promised. `tests/component_transition_test.cpp` was
  updated to assert the new contract.
- **`tests/foreach_chunk_test.cpp`** — chunk iteration contract
  test (multi-archetype world, per-query chunk visits + entity
  coverage + empty-result handling).
- **Doc updates** — `doc/components_and_queries.md` gains "Chunked
  storage and iteration order" + "forEachChunk" sections;
  `doc/command_buffers.md` updates the per-component transition
  section to reflect the new physical-migration semantics;
  `CLAUDE.md` "Adding a new built-in component" recipe is extended
  with the chunked-storage steps (touch `ArchetypeChunk`'s vectors,
  the `if (c.mask.has(...))` branches in `insert`/`removeSwapPop`/
  `migrate`, `ensureStitched`, and `getChunkSpan`); README test
  count 55 → 56.

The §3.1 follow-on is `UserComponent<T>` — see batch 6b below.

### Batch 8 — Render contract expansion (Milestone 4 prep)

Shipped 2026-05-14, ahead of batch 6 per §3.1's deferral guidance.
All public-API additions are pure extensions / new headers under
`include/threadmaxx/render/`; `EntityStorage` and the per-entity
component-mask shape are unchanged.

- **Hierarchical `RenderFrame`.** New fields alongside the legacy
  flat `instances` span: `cameras`, `lights`, `drawItems[Opaque |
  Transparent | ShadowCasters | Overlay]`, `debugLines`,
  `debugPoints`, `debugText`. All renderer-neutral PODs.
- **`ISystem::buildRenderFrame(RenderFrameBuilder&)` lifecycle
  hook.** Invoked after every `postStep` has committed and the
  event channels have drained, on the simulation thread, single-
  threaded, in registration order. Each registered system gets its
  own builder (persisted across ticks for steady-state zero-alloc
  use); the engine merges every system's builder into the back
  render frame in registration order, then publishes.
- **Render-side POD types under `include/threadmaxx/render/`** —
  `Camera`, `Light`, `DrawItem` (carrying `MeshSkinnedRef`,
  `AnimationPoseRef`, `MaterialOverride`, `cameraMask`), `DebugLine`
  / `DebugPoint` / `DebugText`. The decision to keep these as
  builder-pushed PODs rather than wedge them into `EntityStorage`
  as built-in components (which the prior-revision §3.2 spec called for)
  is documented in `doc/render_contract.md`: cameras and lights are
  few in number, `AnimationPose` ringbuffered doesn't fit dense
  parallel-array storage, and the hook gives game code more
  flexibility about where camera/light state lives.
- **Visibility-culling helpers.** `extractFrustum(camera) ->
  Frustum`, `intersectsFrustum(Frustum, min, max)` (AABB p-vertex
  test), `cullByFrustum(items, bounds, cameras)` (batched mask
  population). Up to 32 cameras supported simultaneously via
  `DrawItem::cameraMask`.
- **Instance-buffer layout helper.** `alignas(16) struct
  InstanceLayoutEntry` — 128 bytes, std140-friendly, predictable
  field order. `packInstance(item)` / `packInstances(items, dst)`
  project DrawItems into the layout. Any GPU backend can upload it
  directly.
- **Per-frame upload ring.** `UploadRing(frameCount, bytesPerFrame)`
  — header-only frame-to-frame bump allocator with optional grow-on-
  overflow. Backend-neutral; Vulkan / D3D12 / WebGPU / software all
  map a slab and use `head()` / `bytesPerFrame()` to record flush
  ranges.

The visibility-culling system originally specced in the prior
§3.2 revision ("built-in
system that reads `Camera` + `BoundingVolume`") shipped as the
`cullByFrustum` free function instead. The factory-style
`makeFrustumCullingSystem(...)` shape only makes sense when
cameras live in `EntityStorage`; with cameras living in the
builder, a one-call helper inside game code's own
`buildRenderFrame` is the cleaner fit.

52 tests pin the documented invariants.

### Batch 11 — Frame task graph (Milestone 5, §6 phase 3)

Shipped 2026-05-14. The first of the §3.4–§3.7 Milestone-5 perf
batches. All public-API additions are pure extensions to existing
headers (`System.hpp`, `Engine.hpp`) plus a new tiny header
`TaskTag.hpp`. No public ABI break; the wave scheduler's output is
bit-for-bit identical when no system declares tags.

- **`TaskTag` POD** — `{ std::string_view name; std::uint64_t hash; }`
  with constexpr FNV-1a hash computation in the constructor and
  **name-based equality**. Hash collisions become extra `string_view`
  comparisons in the `unordered_map` bucket chain, never wrong
  scheduling. Eliminates the class of bug DeepSeek's review flagged
  on the original spec.
- **`ISystem::dependencies()` / `provides()`** — optional virtuals
  returning `std::span<const TaskTag>`. Defaults: empty. Each
  consumed tag becomes an edge "this system runs after every
  producer of the same tag" in the DAG; producers' tags are matched
  symmetrically across all registered systems.
- **`ISystem::preferredGrain()`** — optional virtual returning
  `std::uint32_t`. Default 0 = use the existing `workers*4` chunks
  heuristic. Non-zero pins the per-system default for
  `parallelFor(count, /*grain=*/0, ...)` calls; pass-through
  `grain != 0` calls are unaffected.
- **DAG-aware `rebuildWaves`.** The first-fit packer was replaced by
  a topological-sort-then-pack algorithm. Edges from read/write
  conflicts (always `a→b` with `a<b`, so cycle-free by construction)
  combine with tag edges (either direction). Kahn's algorithm picks
  zero-in-degree nodes in registration order, preserving the no-tag
  behavior verbatim.
- **Cycle handling.** If a tag cycle stalls Kahn's algorithm, the
  lowest-index stuck system has its tag-only incoming edges dropped
  and a warning is logged via `ILogger`. The engine recovers
  gracefully and continues running — no crash, no hang.
- **`Engine::taskGraphSnapshot()`** — public debug accessor returning
  `std::vector<TaskGraphNode>`. Each row carries `{ index, name (owned
  string), wave, dependsOn[] }`. Suitable for HUD diagnostics,
  Graphviz/Mermaid export, or test assertions over the DAG shape.
- **Doc updates.** `doc/systems_and_scheduling.md` gained sections
  "Task-graph edges via tags" and "Per-system grain hints" with the
  full code example. `CLAUDE.md` gained a §3.4 batch-11 section.
  README test count 57 → 58.
- **Test.** `tests/task_graph_test.cpp` covers (1) backwards compat
  with no tags, (2) tag dependency pushes consumer into later wave,
  (3) `preferredGrain` honored, (4) cycle detection logs + recovers,
  (5) default heuristic when `preferredGrain == 0`.

### Batch 12 — Cancellation, budgets, priorities (Milestone 5, §6 phase 4)

Shipped 2026-05-14. Four pieces, all opt-in (defaults preserve existing
behavior bit-for-bit). Closes §6 phase 4.

- **Tick budget + skip policy.** `Engine::setTickBudget(seconds)` caps
  per-tick wall-clock spend; the engine samples elapsed time after
  every wave commit, flips an atomic `overBudget_` flag, and skips
  subsequent waves' `ISystem::skippable()` systems for the current
  tick. `preStep` / `postStep` / `buildRenderFrame` are NEVER skipped.
  `ISystem::skippable()` defaults to `false` so no existing system
  becomes droppable accidentally.
- **`SystemContext::shouldYield()`** — cheap atomic poll (mirrors
  `overBudget_`) for long `parallelFor` bodies to bail early on
  budget pressure.
- **`SkipPolicy::Budget` (default) and `SkipPolicy::Scripted`.**
  `Scripted` ignores the budget entirely and consults a
  `(tick, systemName)` queue populated via
  `Engine::pushScriptedSkip`; gives deterministic replay for
  lockstep / networked games. The authoritative peer runs `Budget`,
  drains `EventChannel<SystemSkipped>`, broadcasts the log;
  clients run `Scripted` with the broadcast log and produce
  byte-identical world state.
- **`EventChannel<SystemSkipped>`.** Emitted on every skip with
  `{tick, systemName, reason}`. `reason` is `"budget"` or
  `"scripted"`. Drains at tick boundary like every other typed channel.
  Per-event emission keeps the deterministic-replay path simple
  (one event per `(tick, systemName)` mapping); coalesced batched
  emission is a future tuning option if a game ever measures the
  per-tick event volume mattering, but for the expected workloads
  (a handful of `skippable()` systems × 60–120 fps) it never does.
- **`JobPriority`.** New three-valued enum (`High` / `Normal` /
  `Low`). New `parallelFor` overloads on `SystemContext` accept it;
  the no-priority overloads default to `Normal`. `JobSystem` was
  expanded with per-worker per-priority deques — own pops and steals
  scan in priority order. Backwards-compatible: pre-batch-12 calls
  go through `Normal` and behave bit-for-bit as before.
- **`IResourceLoader::cancel(Engine&)`.** New virtual (default returns
  0). The engine calls it on the sim thread immediately BEFORE
  `update()` each tick — drop newly-stale requests in the same tick
  rather than waiting another. `LoaderStats::cancelled` is a new
  counter aggregated by `Engine::aggregateLoaderStats()`.
- **Doc updates.** `doc/systems_and_scheduling.md` gained "Tick
  budgets, skipping, and `shouldYield`" + "Per-job priorities"
  sections. `doc/resource_loaders.md` gained the
  `cancel`-before-`update` ordering note. `CLAUDE.md` gained a §3.5
  batch-12 section. README test count 58 → 59.
- **Test.** `tests/cancellation_test.cpp` covers (1) no-budget runs
  every skippable system; (2) tight budget skips them and emits
  `SystemSkipped{reason="budget"}`; (3) `shouldYield()` reflects the
  over-budget flag in a later-wave system; (4) `Scripted` policy
  reproduces a captured skip log; (5) `JobPriority` API coverage
  across High/Normal/Low; (6) loader `cancel()` fires before
  `update()` and `cancelled` aggregates.

### Batch 14 — Telemetry ingestion close-out (Milestone 5, §6 phase 6)

Shipped 2026-05-15. Closes §6 phase 6 by shipping the *ingestion*
side of the telemetry stack — the *primitives* (job histograms,
Chrome-trace writer, frame snapshots, system stats) all landed in
batch 4. Five additions, all opt-in (defaults preserve prior
behavior bit-for-bit):

- **`ITraceSink`.** New public header
  `include/threadmaxx/Telemetry.hpp`. Engine-streamed per-tick
  consumer interface: `onFrame(const FrameSnapshot&)` is called
  on the sim thread after the frame is built and published.
  Default sink is `nullptr` (zero cost). Install via
  `Engine::setTraceSink(ITraceSink*)`. Sink owns its buffering
  and any off-thread I/O; the engine never copies the snapshot.
  Lifetime: the sink must outlive the engine (mirror of
  `setRenderer` / `setLogger`).
- **`FileTraceSink`.** Built-in rolling Chrome-trace JSON sink.
  Wraps an internal `ChromeTraceWriter`; when the active file
  crosses `Config::rotationBytes` (default 64 MiB) the sink
  closes it and opens the next, incrementing the rotation
  index. `pathTemplate` accepts `%N` substitution or appends
  `.N.json`. Synchronous I/O on the sim thread inside `onFrame`
  — keep `rotationBytes` reasonable.
- **`HudTraceSink`.** Seqlock-protected latest-snapshot sink.
  `tryGet(LatestTelemetry&)` is lock-free, torn-write-free,
  callable from any thread. `LatestTelemetry` carries headline
  numbers only (tick, step_s, avg_s, commit_s, jobs, commands,
  aliveEntities, commitHash, workerCount, totals). Designed for
  a renderer/HUD thread polling once per render frame.
  `alignas(64)` on the sequence counter keeps it off the data
  cache line.
- **`FrameBudgetWatcher`.** Built-in `ISystem`. Construct with
  `FrameBudgetWatcher{&engine, targetSeconds}` and register
  normally. `postStep` reads `engine.stats().lastStepSeconds`
  and emits a `BudgetExceeded` event when the just-finished tick
  exceeded the target. `exceedCount()` reports the lifetime count.
  Reads / writes empty, so the watcher lands in any wave without
  contention.
- **Stall watchdog.** `Engine::setStallTimeout(seconds)` spawns a
  background watchdog thread (`EngineImpl::watchdogThreadFn_`).
  The sim thread publishes `(step-start-ns, active-tick)` via
  relaxed atomics at the top of every `step()`. The watchdog
  wakes every `0.25 * timeout` and, if the running tick has
  exceeded the threshold AND no `EngineStall` has been emitted
  for it yet (CAS-guarded), emits one via
  `events<EngineStall>()`. Safe to emit from another thread:
  the lock-free MPSC channel (§3.6 batch 13c) is doing the
  publishing. `setStallTimeout(0.0)` joins the watchdog;
  `~EngineImpl` does too. Zero cost when disabled (the default).
- **Test.** `tests/telemetry_sink_test.cpp` covers all five
  pieces: fake `ITraceSink` counts `onFrame` calls and verifies
  the published tick; `FileTraceSink` rotates across 50 ticks
  under a 512-byte budget; `HudTraceSink` reader thread polls
  during 220 ticks and observes no torn reads; `FrameBudgetWatcher`
  with a 1µs target fires `BudgetExceeded` on every tick; stall
  watchdog catches a deliberately-slow 200ms tick within ~62ms
  and emits `EngineStall`.
- **Doc updates.** README test count 61 → 65 (one test per
  batch-13c & batch-14 deliverable). CLAUDE.md gained a
  §3.7 batch 14 section. `doc/tracing.md` and
  `doc/stats_and_profiling.md` documented the sink contract.
  New `doc/telemetry.md` (cross-linked from `doc/index.md`)
  collects the runtime/diagnostic surface.

This completes §6 phase 6 — every multi-threading-and-performance
roadmap phase has landed.

### Batch 13c — Storage contention close-out (Milestone 5, §6 phase 5)

Shipped 2026-05-15. Closes §6 phase 5 by landing the items
deferred from batches 13a/13b. Three independent pieces; no
public API breakage:

- **Lock-free MPSC event channels.** `EventChannel<T>::emit` is
  no longer mutex-protected. The back buffer is now a
  Treiber-stack-style atomic linked list: `emit` allocates a
  node and CAS-prepends; `drain` `exchange()`-detaches the
  entire list, walks it, and reverses into `front_` to restore
  per-thread FIFO order. Subscriber list still mutex-guarded
  (low-frequency subscribe / unsubscribe). `pendingCount()` is
  now an atomic counter — approximate under concurrent emit.
  Public `subscribe` / `subscribeScoped` / `drainTick` semantics
  unchanged. Side benefit: the previous mutex's latent
  self-deadlock on recursive emit-from-callback during drain is
  gone (the new design captures the back stack before invoking
  subscribers).
- **`WorldView`.** New public class in
  `include/threadmaxx/World.hpp`: a wave-scoped read-only view
  of the chunk inventory. Caches `(const ArchetypeChunk*)`
  pointers + total entity count at view-build time; stable for
  the duration of a wave (commits happen between waves so the
  view never goes stale mid-wave). Exposed via
  `SystemContext::worldView() -> const WorldView&`. Engine
  rebuilds it before each wave and between serial preStep /
  postStep commits. Game code captures `view.chunks()` (a span)
  into worker lambdas instead of repeatedly indexing
  `World::archetypeChunk(i)`. Test:
  `tests/world_view_test.cpp` — empty world, single archetype,
  two archetypes; verifies chunk pointer / entity-count
  consistency and the same view shared across multiple
  `parallelFor` calls.
- **Soak test + benchmarks.** `tests/commit_soak_test.cpp` —
  4096 ticks × two workloads × both commit paths. Asserts
  per-tick `commitHash` and final `WorldSnapshot` FNV-1a agree
  across paths under long-horizon load, plus the lock-free
  event channel survives ~100M events drained. Runtime ~3s.
  Opt-in benchmark binaries: `bench/commit_bench` (compares
  single-threaded vs. sharded commit paths across multiple
  entity counts) and `bench/event_channel_bench` (measures
  lock-free emit throughput at varying producer counts). Enable
  via `-DTHREADMAXX_BUILD_BENCHMARKS=ON`. On the workloads
  tested, the sharded commit path's classifier overhead
  currently exceeds its parallelism win across all sizes
  (256 → 131k entities) — the documented recommendation is to
  keep `singleThreadedCommit = true` as the default. The bench
  infra exists so this can be re-evaluated as workload shapes
  shift.

Items still deferred (not blocking phase 5 closure;
contingent on profiler-confirmed need):

- **Per-chunk command buffers.** Record-time routing into
  per-chunk buckets would skip the classifier pass entirely but
  requires `CommandBuffer` recording-API changes. Tracked in
  §3.6.4 as a future potential batch if commit-time profiling
  ever flags the classifier as the bottleneck.
- **Read-only world snapshot pointer caching across waves.**
  The current `WorldView` is wave-scoped (rebuilt between
  waves). A multi-wave-cached snapshot would help queries that
  reuse chunk pointers across waves; deferred for the same
  profiler-driven reason.

Both are now §3.6.4 candidates rather than active batches.

### Batch 13b — Sharded commit phase (Milestone 5, §6 phase 5)

Shipped 2026-05-15, hours after batch 13a — the safety net pre-validated
the design, so 13b's implementation collapsed to a focused
three-pass classifier + parallel apply. Effort ~0.5 days.

- **`EngineImpl::commitBuffersSharded`.** Three-pass parallel
  commit. Pass A: walk all of one system's buffers, build the
  migrating-entity set (entities touched by any non-value-only
  command). Pass B: walk again in submission order, hash inline,
  apply migrate-possible commands and any commands on migrating
  entities on the sim thread immediately, queue value-only
  commands (`SetTransform` / `SetVelocity` / `SetAcceleration` /
  `SetUserData`) on non-migrating entities into per-destination-
  chunk bins. Pass C: each non-empty bin runs as one
  `JobSystem` job and the sim thread `latch`-waits.
  Per-chunk bins write disjoint memory; the four value-only
  setters look up by `EntityHandle`, so a swap-pop during pass B
  doesn't break pass C.
- **`Config::singleThreadedCommit = false`.** Public toggle that
  selects the sharded path. Default remains `true`; the sharded
  path is a pure performance opt-in. Wave commits in `step()`
  dispatch through `commitBuffersSharded` when off; pre/postStep
  always run through the single-threaded `commitBuffer` (one
  buffer at a time, classifier wouldn't help).
- **Shared apply/hash helpers.** `applyCommandImpl(cmd, storage)`
  and `hashCommandImpl(h, cmd, spawnResult)` factor the variant
  dispatch so `commitBuffer` and `commitBuffersSharded` use the
  same mutation/hashing code. The single-threaded path applies-
  then-hashes inline; the sharded path hashes chunk-local
  commands at queue time (deterministic inputs) and global
  commands at apply time (so `CmdSpawn`'s result handle is
  available). Both produce identical hash sequences by
  construction.
- **`stitchedDirty_` is now `std::atomic<bool>`.** Worker threads
  applying chunk-local commands all flip the dirty flag through
  the `mut*()` setters; making the flag atomic-relaxed prevents
  a benign-in-practice data race per the C++ memory model.
  Single-byte cost, no perf impact.
- **Test.** `tests/sharded_commit_test.cpp` reruns the batch-13a
  five scenarios under `singleThreadedCommit = false` on a
  4-worker engine, hash-compared against the single-threaded
  reference. Per-tick `commitHash` AND final `WorldSnapshot`
  hash must agree across paths for every (scenario, tick) pair.
  Plus: run-vs-run sharded stability, empty-buffer sharded path,
  stale-handle tolerance in the chunk-local lane. Runtime ~21s.
- **Doc updates.** README test count 60 → 61. CLAUDE.md gained a
  §3.6 batch 13b section documenting the classifier rules.
  `doc/stats_and_profiling.md` notes the `singleThreadedCommit`
  toggle alongside the `commitHash` field.

The remaining §6 phase 5 items (per-chunk command buffers,
read-only snapshot, lock-free events) are tracked in §3.6.3 as
batch 13c.

### Batch 13a — Commit-hash determinism safety net (Milestone 5, §6 phase 5 prep)

Shipped 2026-05-15. Lands the deterministic-runtime safety net plus
the `Config` toggles batch 13b's sharded commit path will key off.
No behavior change: the commit phase is still single-threaded;
what changed is that every commit now produces a per-tick
FNV-1a-64 hash that *future* sharded code must reproduce
bit-for-bit. Effort ~0.5 days.

- **`EngineStats::commitHash`.** Running FNV-1a-64 over every
  applied mutation (spawn / destroy / setX / addTag / removeTag /
  user-component blob) in the commit phase. Reset to the FNV-1a-64
  offset basis at step start; published at step end. Same inputs
  → same hash, across runs and machines. Implementation in
  `EngineImpl::commitBuffer` mixes each variant's discriminator +
  entity handle + value bytes inline with the existing
  `std::visit` dispatch. Cost: a few ns per applied command.
- **`Config::singleThreadedCommit = true`.** Public toggle for
  the deterministic reference path. Today it's the only path; the
  knob is the documented immediate fallback if batch 13b's sharded
  path ever divergences in production.
- **`Config::logCommitHashEvery = 0`.** Opt-in production
  diagnostic. When `N > 0` the engine logs `commitHash` via
  `ILogger` at `Info` every N ticks (default `0` = off, zero
  cost). Two clients with the same seed but diverging logs
  pinpoint the offending tick for local repro.
- **Trace integration.** `writeJsonLines` adds a
  `"commit_hash":"0x…"` field; `ChromeTraceWriter` adds the same
  to its `step` event's `args`. A diff between two trace files
  surfaces the diverging tick instantly.
- **Test.** `tests/commit_hash_test.cpp` — five seeded churn
  scenarios (1k/8k/32k entities × {transform-only,
  transform+health, transform+statictag, parent-hierarchy,
  mixed multi-setter}) × 256 ticks × 2 runs each.
  Per-tick `commitHash` and final `WorldSnapshot` hash must agree
  across runs. Plus: different command stream yields different
  hash; pause leaves hash at the FNV-1a basis;
  `logCommitHashEvery` cadence + content; default `0` is silent.
  Runtime ~7.5 s end-to-end.
- **Doc updates.** README test count 59 → 60.

The test is the baseline batch 13b will hash-compare against.

### Batch 7 — Resource & event maturity (Milestone 3)

Shipped 2026-05-14, ahead of batch 6 per §3.1's deferral guidance.
All public-API additions are pure extensions to existing headers
(`Resource.hpp`, `EventChannel.hpp`, `Engine.hpp`).

- **Refcounted `ResourceHandle<T>`.** `ResourceRegistry::addRefCounted`
  + `acquire(id)` return an RAII handle; the slot is freed (and its
  generation bumped) on last drop. Legacy `add` / `remove` path is
  unchanged and independent.
- **Hot-reload protocol.** `Engine::markResourceStale<T>(id)` is the
  typed dispatch; `IResourceLoader::markStale(index, generation,
  type)` is the type-erased loader-side hook (default no-op). Loaders
  emit `AssetReloaded{oldIndex, oldGeneration, newIndex,
  newGeneration, type}` on the engine's typed event channel after
  replacing the asset; `AssetReloaded::matches<T>(id)` is the helper
  subscribers use to filter.
- **Boot-time blocking preload.** `Engine::preloadUntil(predicate,
  timeout = 5s)` pumps every loader's `update()` in a yield loop
  until the predicate returns true or the timeout elapses. Simulation
  does not advance.
- **`IResourceLoader::onShutdown(Engine&)`.** Engine calls it in
  reverse-registration order before each loader is destroyed; engine
  guarantees `update` will not be called again afterward.
- **`IResourceLoader::stats()` + `Engine::aggregateLoaderStats()`.**
  `LoaderStats { pendingLoads, inFlight, ready, failed,
  memoryFootprint, memoryBudget }` per loader; engine sums them for
  HUD readouts.
- **RAII event subscriptions.** `EventChannel<T>::subscribeScoped(fn)`
  returns a `Subscription` (move-only, type-erased) that
  auto-detaches on destruction. The channel's subscriber list now
  lives in a `std::shared_ptr<SubscriberList>` so a `Subscription`
  can hold a `weak_ptr` and safely no-op if the channel destructs
  first.

46 tests pin the documented invariants.

## 3. Planned batches — the road to Milestones 2–4

This is the forward plan. The goal at the end of §3 is "a 3D RPG
example can be developed on top of threadmaxx without patching engine
internals." That requires three things the library does not yet have:

- a wider data model (more component slots, archetype-style storage),
- a richer rendering contract (passes, cameras, lights, skinned poses),
- a real renderer to prove the contract — Vulkan, per §3.8.

The batches are sized like prior batches (5–9 additive items each),
sequenced so each one is shippable on its own and the public API
grows monotonically. The mapping to milestones (§8):

| Batch | Theme                              | Milestone(s)          |
|-------|------------------------------------|-----------------------|
| ~~4~~ | ~~Observability + small Milestone-1 polish~~ | ✅ landed 2026-05-14 — see §2 |
| ~~5~~ | ~~Data model widening~~            | ✅ landed 2026-05-14 — see §2 |
| ~~6a~~ | ~~Archetype prep (non-storage half of 6)~~ | ✅ landed 2026-05-14 — see §2 |
| ~~6~~ | ~~Chunked archetype storage~~      | ✅ landed 2026-05-14 — see §2 |
| ~~6b~~ | ~~`UserComponent<T>` extension hook~~ | ✅ landed 2026-05-14 — see §2 |
| ~~7~~ | ~~Resource & event maturity~~      | ✅ landed 2026-05-14 — see §2 |
| ~~8~~ | ~~Render contract expansion~~      | ✅ landed 2026-05-14 — see §2 |
| ~~9~~ | ~~Vulkan reference renderer (example)~~ | ✅ landed 2026-05-15 — see §2 / §3.1 below |
| ~~10~~ | ~~3D RPG demo example~~           | ✅ landed 2026-05-15 — see §2 / §3.2 below |
| ~~11~~ | ~~Frame task graph (§6 phase 3)~~ | ✅ landed 2026-05-14 — see §2 |
| ~~12~~ | ~~Cancellation, budgets, priorities (§6 phase 4)~~ | ✅ landed 2026-05-14 — see §2 |
| ~~13~~ | ~~Storage contention reduction (§6 phase 5)~~ | ✅ landed 2026-05-15 — see §2 |
| ~~14~~ | ~~Telemetry ingestion (§6 phase 6 close-out)~~ | ✅ landed 2026-05-15 — see §2 |
| ~~15~~ | ~~Audit-driven hygiene + pre-batch-9 API polish~~ | ✅ landed 2026-05-15 — see §2 / §3.6.5 |
| ~~16~~ | ~~Workload-realistic benchmark harness (gate)~~ | ✅ landed 2026-05-16 — see §3.9.1 |
| ~~17~~ | ~~Chunk iteration micro-optimization~~ | ✅ landed 2026-05-16 — see §3.9.2 |
| ~~18~~ | ~~Command buffer arena + compact payloads~~ | ✅ landed 2026-05-16 — see §3.9.3 |
| ~~19~~ | ~~Migration batching by archetype pair~~ | ✅ landed 2026-05-16 — see §3.9.4 |
| ~~20~~ | ~~Async snapshot + trace-sink off-thread (QoL)~~ | ✅ landed 2026-05-16 — see §3.9.5 |

§3.8 covers the Vulkan defaulting strategy across batches 8–10.
§3.9 covers the post-Milestone-6 measurement-driven plan (batches
16–20), derived from `threadmaxx_core_future_optimization_notes.md`.

Batches 9 and 10 are **example projects**, not library batches —
they consume the existing public surface and prove a real game can
be built on top. The library-side perf batches 11–14 can ship
independently of the examples; in practice the natural ordering is
**11 → 9 → 12 → 13 → 10 → 14**, but the dependencies are loose
enough that 11/12/13 can land before either example, and 14 can
land alongside 10 because telemetry quality is best measured under
a real game's workload. See §3.4–§3.7 for the per-batch scopes and
the cross-batch sequencing rationale.

### 3.1 Batch 9 — Vulkan reference renderer (Milestone 4)  ✅ landed 2026-05-15

The first concrete renderer that exercises the §2 batch-8 render
contract. Lives in `examples/vulkan_renderer/`, NOT in the core
library — the renderer-agnostic guarantee on the public surface
holds.

**As-shipped scope** (v1; advanced features deferred to batch 10's
RPG demo because that's where they have a real consumer):

- `examples/vulkan_renderer/` — Vulkan 1.3 (dynamic rendering,
  synchronization2, timeline semaphores, shaderDemoteToHelperInvocation),
  GLFW for window/surface. Opt-in via CMake: silently skipped when
  any of `Vulkan`, `glfw3`, or `glslc` is missing — same pattern as
  `examples/boids`.
- Ships as a **static library** `threadmaxx::vulkan_renderer` plus a
  build-verification smoke binary `threadmaxx_vulkan_smoke`. The
  separate Vulkan demo scene was dropped — batch 10's RPG demo is
  the showcase, and it links against the static library. The smoke
  is just `engine + renderer + 1 spinning camera + 1 debug-line cube`
  proving end-to-end boot, multi-camera projection, swapchain
  resize, and clean shutdown across ~300 ticks.
- `VulkanRenderer` implements `IRenderer` against the hierarchical
  `RenderFrame`: multi-camera (`frame.cameras` iterated with
  `DrawItem::cameraMask` filtering), per-pass binning (only Opaque
  is drawn in v1; Transparent/ShadowCasters/Overlay are received
  but not yet emitted), instanced opaque draws (per-instance vertex
  binding fed from `InstanceLayoutEntry` via `packInstance`), debug
  line + point pipelines, frame-in-flight ring with timeline
  semaphores + per-image renderFinished semaphores, swapchain
  recreate on `onResize`.
- Asset loaders:
    - `MeshLoader` — synthesizes a unit-cube fallback. Real mesh
      I/O (OBJ / glTF) deferred to batch 10.
    - `TextureLoader` — synthesizes a 1×1 white fallback. Real
      texture I/O deferred to batch 10.
    - `ShaderLoader` — accepts embedded SPIR-V (the v1 path) AND
      file-backed SPIR-V; on `Engine::markResourceStale<Shader>`
      the loader re-reads bytecode and emits `AssetReloaded` on the
      typed event channel. The renderer-side subscriber for
      pipeline rebuild is deferred to batch 10 (v1 ships embedded
      shaders so there's no triggering condition).
- Shaders live in `shaders/`: `opaque.vert/frag` (Lambert + ambient,
  per-instance push-constant view*proj + light dir), `debug_line.vert/frag`
  and `debug_point.vert/frag` (push-constant view*proj). CMake's
  `add_custom_command` runs `glslc --target-env=vulkan1.3 -O` then
  `EmbedSpv.cmake` packs the SPIR-V words into a header so the
  static library has no runtime file dependency.
- All three loaders track GPU resources in their own owned-vectors
  and expose `releaseGpuResources()`. The renderer calls each one
  from `shutdown()` right after `vkDeviceWaitIdle`, before tearing
  down the `VulkanContext` — that closes the ordering gap with
  `Engine::shutdown`'s loader teardown (which fires *after*
  `IRenderer::shutdown` and would otherwise hit a destroyed device).
- Validation-clean under the Khronos validation layer (opt in via
  `THREADMAXX_VK_VALIDATE=1` in the smoke binary).
- Cross-platform CI is out of scope for this batch — verified on
  Linux only, with a `find_package`-gated opt-in so CI matrices
  without Vulkan SDKs build cleanly without it.

**Deferred to batch 10 or follow-ons** (everything the original §3.1
spec called for that v1 doesn't yet do):

- Depth pre-pass + shadow pass (single depth attachment is in; a
  light-view shadow map isn't).
- Skinned mesh pipeline + bone-matrix upload (`MeshSkinnedRef`,
  `AnimationPoseRef`).
- PBR-ish opaque shader (v1 is Lambert + ambient; PBR comes when
  textures are loaded from disk).
- Real mesh / texture / file-watcher I/O.
- Pipeline rebuild on `AssetReloaded` (loader-side plumbing is in
  place; the renderer-side subscribe is the missing piece).
- DebugText rendering (the public hook exists; a font atlas would
  be a batch 10 addition).
- Transparent / ShadowCasters / Overlay passes.
- Cross-platform CI matrix.

**Why the smoke isn't a full demo scene**: batch 10's RPG demo is
the showcase the spec called for. Building both a "Vulkan smoke
scene" AND an "RPG demo on top of the Vulkan renderer" duplicates
work and gives the library two examples that drift out of sync
with each other; the smoke is intentionally minimal so it stays
green as a build-verification gate while batch 10 evolves the
content.

### 3.2 Batch 10 — 3D RPG demo example (Milestone 6 lead-in)  ✅ landed 2026-05-15

Closes Milestone 6. Built on top of the batch-9 Vulkan renderer;
proves a real game can be developed against threadmaxx's public
surface without touching the engine.

**As-shipped scope:**

- `examples/rpg_demo/` — depends on
  `threadmaxx::vulkan_renderer` (and therefore on Vulkan + GLFW +
  glslc). Silently skipped at configure time if the renderer target
  is missing.
- Scene: 60×60 terrain, a player (blue cube), 50 NPCs (mixed
  hostile/friendly with simple Idle/Wander/Flee state machine), 100
  pickup cubes scattered on the floor. 152 entities total at boot.
- 11 systems registered in registration order (which the wave
  scheduler tops-sorts):
  `NPCBrainSystem` (preStep rebuilds a `SpatialHash<EntityHandle>`,
  update reads world + writes per-NPC `Velocity`),
  `PlayerInputSystem` (writes player `Velocity` from poll-state
  WASD + camera yaw),
  `CameraSystem` (writes player yaw via `UserComponent<PlayerState>`,
  `buildRenderFrame` pushes a third-person `Camera`),
  `MovementSystem` (`forEachWith<Transform, Velocity>` integrates,
  skips `DisabledTag`),
  `PickupSystem` (queries the spatial hash near the player, flips
  `DisabledTag` on pickups, increments
  `PlayerState.pickups`, emits `PickupCollected` events),
  `DayNightSystem` (postStep advances sun angle, `buildRenderFrame`
  pushes a directional `Light`),
  `CubeRenderSystem` (snapshots `CubeRender` user-component +
  Transform during update, emits a `DrawItem` per entity in
  `buildRenderFrame` to `RenderPass::Opaque`),
  `DebugOverlaySystem` (16-segment AOI circles + player aim line via
  `RenderFrameBuilder::addDebugLine/Point`),
  `SaveLoadSystem` (F5 quick-save, F9 diagnose),
  `HudSystem` (F1 toggle `FileTraceSink`, subscribes to
  `PickupCollected` via `subscribeScoped`, prints stats every 60
  ticks in postStep, marked `skippable`).
- Four `UserComponent<T>` PODs registered at boot: `CubeRender`,
  `NpcState`, `PlayerState`, `Pickup`. Demonstrates the §3.1 batch
  6b extension path — none of these are built-in, the engine never
  names them, the game registers them at startup and the systems
  pass the `UserComponentId`s around via a `UserComponentIds`
  struct.
- All built-in components touched at least once:
  Transform / Velocity / Faction / BoundingVolume / Health (every
  entity has at least three of these); StaticTag on terrain;
  DisabledTag flipped by PickupSystem; tag bits round-trip through
  the snapshot via `ComponentSet`. No `Parent` chains in v1 — the
  third-person camera attaches manually rather than via
  hierarchical transforms — but the engine's hierarchy plumbing is
  exercised at compile time via the public headers regardless.
- Engine subsystem coverage:
    - Spatial-hash AOI (batch 3 / §3.3) via SpatialHash<EntityHandle>.
    - Event subscriptions (batch 4) via subscribeScoped to
      PickupCollected.
    - Chrome-trace capture (batch 4 / §3.7 batch 14) via F1 toggle
      installing a FileTraceSink with `/tmp/rpg_demo_trace.%N.json`.
    - Serialization (batch 4) via `world.snapshot()` + `serialize`
      to `/tmp/rpg_demo_save.bin` on F5.
    - User-component extension (batch 6b) — described above.
    - Pass-aware rendering (batch 8) via
      `RenderFrameBuilder::addDrawItem(RenderPass::Opaque, ...)`.
    - Worker-pool scheduling — every system declares its
      reads/writes; the scheduler runs non-conflicting systems
      concurrently across waves.
- No engine patches needed. The entire demo lives in
  `examples/rpg_demo/` (14 .cpp/.hpp files) plus the public
  threadmaxx headers and the `threadmaxx::vulkan_renderer` static
  library. Both `build/` and `build-werror/` build it cleanly;
  `ctest` still reports 79/79 on both trees after the demo lands.
- Controls — `W/A/S/D` move, arrow keys rotate the camera, `Q/E`
  zoom, `F1` toggle Chrome-trace, `F5` quick-save, `F9` load and
  diagnose, `Esc` / window close exit.

**Deferred to follow-ons** (none of these block the M6 close):

- UserComponent persistence in `WorldSnapshot`. Today's
  `SaveLoadSystem` save+load is a round-trip *of the built-in
  components only*; load re-reads the file and prints a diagnostic
  summary rather than tearing down the world (which would leave
  every entity without its CubeRender / NpcState / PlayerState /
  Pickup attached and break rendering / AI). The §3.1 batch 6b
  docs already call user-component persistence out as a game-side
  responsibility, so the demo follows that contract rather than
  trying to fake it.
- Hierarchy: the third-person camera attaches manually in
  `CameraSystem` rather than via a `Parent` chain. A future
  follow-on can switch to the hierarchy system to demonstrate the
  multi-level transform composition path.
- Audio, particles, post-processing — out of scope for the engine
  (sibling libraries per §3.3).
- Real disk-loaded mesh / texture / shader assets. The renderer
  ships embedded SPIR-V + a unit-cube fallback mesh; the demo
  works against that surface. Real asset I/O would extend the
  asset loaders inside `examples/vulkan_renderer/` and is
  orthogonal to the demo's correctness.

### 3.3 Items intentionally NOT in §3

The following are good extensions but belong above the library (or in
sibling libraries). Calling them out so they don't accidentally creep
into a batch.

- **Animation pipeline math** (blend trees, IK, cloth). The
  `AnimationStateRef` / `AnimationPose` slots give game code a place
  to put the math; the engine does not own it.
- **Physics solver.** The `PhysicsBodyRef` slot is the integration
  point. A Jolt-backed integration example may ship alongside
  `examples/rpg_demo/` but is not in the core library.
- **Networking / replication / rollback.** Deterministic commit and
  stable entity IDs are the engine's contribution; the wire protocol
  is a sibling library.
- **Navmesh / pathfinding.** Sibling library; the engine just
  provides the scheduling primitives and the `NavAgentRef` slot.
- **Editor / hot-reload UI.** Out of scope until the public API has
  stabilized through M4.

### 3.4 Batch 11 — Frame task graph  ✅ landed 2026-05-14

See the per-batch entry in §2 for the as-shipped shape. The
roadmap-side notes that informed the design (DeepSeek's review,
hash-collision elimination, no-tags backward compatibility) are
preserved as comments in `tests/task_graph_test.cpp` and
`doc/systems_and_scheduling.md`.

### 3.5 Batch 12 — Cancellation, budgets, priorities  ✅ landed 2026-05-14

See the per-batch entry in §2 for the as-shipped shape. The Budget
vs Scripted split that informed the design (per DeepSeek's review)
is preserved as `engine.setSkipPolicy(SkipPolicy::{Budget|Scripted})`
plus the `EventChannel<SystemSkipped>` broadcast log; clients
replay deterministically.

### 3.6 Batch 13 — Storage contention reduction (Milestone 5, §6 phase 5)

The commit phase currently runs single-threaded on the simulation
thread by design (it's what makes the engine deterministic).
That's fine when commits are short; under heavy spawn/destroy
churn, it becomes the bottleneck a profiler flags first. Batch 13
attacks the contention without giving up determinism.

Gating: independent of 11/12. Builds on the chunked storage from
batch 6 (parallel commit needs per-chunk locks). Total effort
~2 weeks — split into **13a (safety net)** and **13b (sharded
commit)** so the runtime divergence check ships *before* the
optimization that depends on it.

#### 3.6.1 Batch 13a — Determinism safety net  ✅ landed 2026-05-15

Lands the deterministic-runtime safety net plus the `Config`
toggles that batch 13b's sharded path will key off. No behavior
change: the commit path is still single-threaded; what changed is
that every commit now produces a per-tick FNV-1a-64 hash that
*future* sharded code must reproduce bit-for-bit.

Deliverables shipped:

- **`EngineStats::commitHash`.** Running FNV-1a-64 over every
  applied mutation (spawn / destroy / setX / addTag / removeTag /
  user-component blob) in the commit phase. Reset to the
  FNV-1a-64 offset basis at step start; published at step end.
  Cheap (a few ns per command). Same inputs → same hash, across
  runs and machines. Implemented in `EngineImpl::commitBuffer`
  alongside the existing `std::visit` dispatch — every variant
  arm mixes its discriminator + entity handle + value bytes.
- **`Config::singleThreadedCommit = true`.** Public toggle for
  the deterministic reference path. Today it's the only path;
  batch 13b will plumb the sharded code under
  `singleThreadedCommit = false` and key off the runtime hash to
  prove the two paths agree. Documented as the immediate
  fallback if any divergence is ever discovered in production.
- **`Config::logCommitHashEvery = 0`.** Opt-in production
  diagnostic. When N > 0 the engine logs `commitHash` via
  `ILogger` at `Info` every N ticks. Default 0 = off, zero cost.
  Two clients with the same seed but diverging logs pinpoint the
  offending tick for local repro.
- **Trace integration.** `writeJsonLines` adds a
  `"commit_hash":"0x…"` field; `ChromeTraceWriter` adds the same
  to its `step` event's `args`. A diff between two trace files
  surfaces the diverging tick instantly.
- **`tests/commit_hash_test.cpp`** — **five seeded churn
  scenarios** (1k/8k/32k entities × {transform-only,
  transform+health, transform+statictag, parent-hierarchy,
  mixed multi-setter}), each run **twice** for **256 ticks**.
  Per-tick `commitHash` AND the final `WorldSnapshot` FNV-1a
  hash must agree across runs. Plus targeted assertions:
  different command stream (one extra spawn) yields a different
  hash; pause leaves hash at the FNV-1a basis;
  `logCommitHashEvery` emits at the expected cadence with
  matching hash values; default `logCommitHashEvery=0` emits
  nothing.

The test is the baseline batch 13b will hash-compare against —
the moment the sharded path lands, the same suite runs with
`singleThreadedCommit = false` and the existing per-tick
assertions catch any reordering bug as a loud first-tick
mismatch.

#### 3.6.2 Batch 13b — Sharded commit phase  ✅ landed 2026-05-15

Lands the sharded commit path itself. Effort ~0.5 days (the
batch-13a safety net pre-validated the design, so the implementation
collapses to a two-pass classifier + parallel apply).

Deliverables shipped:

- **Sharded commit phase.** When `Config::singleThreadedCommit
  = false`, each system's wave commit runs through
  `EngineImpl::commitBuffersSharded`, a three-pass classifier:
  (A) build the migrating-entity set; (B) walk all buffers in
  submission order, hash inline, apply global commands (anything
  that may toggle a mask bit) immediately on the sim thread, queue
  value-only commands (`SetTransform` / `SetVelocity` /
  `SetAcceleration` / `SetUserData`) on non-migrating entities into
  per-destination-chunk bins; (C) submit each non-empty bin as one
  `JobSystem` job and `latch`-wait. Migrate-possible commands stay
  serial to preserve registration-order semantics. The default
  remains `singleThreadedCommit = true` until the path soaks.
- **Shared apply/hash helpers.** `commitBuffer` and
  `commitBuffersSharded` now both go through the file-local
  `applyCommandImpl(cmd, storage) -> EntityHandle` (mutation only)
  and `hashCommandImpl(h, cmd, spawnResult)` (hash only). The
  single-threaded path applies-then-hashes inline; the sharded
  path hashes chunk-local commands at queue time and global
  commands at apply time. Both produce identical hash sequences.
- **Atomic `stitchedDirty_` in `EntityStorage`.** Worker threads
  applying chunk-local commands all flip the dirty flag; relaxed
  atomic store prevents a benign-in-practice data race per the
  C++ memory model.
- **`tests/sharded_commit_test.cpp`** — reruns the batch-13a
  scenarios with `singleThreadedCommit = false`, hash-compared
  against the single-threaded reference path. The five
  (1k/8k/32k entities × transform / health / static-tag / parent /
  mixed) scenarios run for 256 ticks each on a 4-worker engine.
  Per-tick `commitHash` AND final `WorldSnapshot` FNV-1a hash
  agree across paths for every (scenario, tick) pair. Plus:
  run-vs-run sharded stability (workers race, results don't);
  empty-buffer sharded path; stale-handle tolerance in the
  chunk-local lane.

Deferred to **batch 13c** (the natural next-step batch that closes
out §6 phase 5):

- **Per-chunk command buffers.** Today's design bins at commit
  time, after recording. A record-time routing scheme would skip
  the classifier pass entirely but requires redesigning the
  `CommandBuffer` recording API. Effort ~1 week; deferred until
  profiling shows the classifier pass as a measurable bottleneck.
- **Read-only world snapshot per wave.** Worker jobs today read
  through `const World&`; an explicit immutable snapshot pointer
  would let queries cache chunk pointers across multiple
  `parallelFor` calls. Independent perf opt; deferred for the
  same reason.
- **Append-only lock-free event channels.** `EventChannel<T>::emit`
  still uses a mutex. Replacement with an MPSC queue is a
  contained internal change; the public API stays unchanged.
  Deferred to batch 13c so the commit-phase work and the
  event-channel work can be benchmarked + soaked independently.

#### Risks and mitigations

- **Determinism preservation.** Per-chunk parallel commits must
  produce a state byte-identical to the single-threaded reference.
  Mitigation in three layers: (1) commands within a chunk still
  apply in submission order (each bin processed by one worker);
  (2) cross-chunk commands fall back to deterministic
  registration-order serial commit; (3) the per-tick
  `commitHash` (batch 13a) provides a runtime safety net. If a
  divergence is ever discovered in production,
  `Config::singleThreadedCommit = true` is the documented
  immediate fallback — the sharded path is a pure performance
  opt-in.
- **False sharing.** The current commit-time-bin design uses one
  `std::vector<Command*>` per chunk, so headers don't sit next to
  each other in cache. The per-chunk *command buffer* design
  (batch 13c) will need the alignas-based padding fix.

### 3.6.3 Batch 13c — Storage contention close-out  ✅ landed 2026-05-15

See the per-batch entry in §2 for the as-shipped shape. The
lock-free MPSC event channel and `WorldView` wave-scoped
snapshot shipped; per-chunk record-time command buffers and a
read-only cross-wave world snapshot were re-tracked as §3.6.4
candidates since the microbench data showed the sharded path's
classifier overhead exceeds its parallelism win at all
currently-tested workload shapes (so optimizing further is
premature without a real workload demanding it).

### 3.6.4 Batch 15+ — Profiler-driven follow-ons (potential)

Not scheduled; contingent on real-game profiling once batches 9
(Vulkan renderer) and 10 (RPG demo) expose end-to-end commit-
phase pressure. If the sharded commit ever becomes a hot spot:

- **Per-chunk command buffers.** Record-time routing into
  thread-local buckets keyed on the entity's current archetype;
  skips the commit-time classifier pass. Requires
  `CommandBuffer` recording-API changes.
- **Read-only cross-wave world snapshot.** Pin a snapshot
  pointer across multiple waves so query helpers can cache
  chunk pointers more aggressively than the current per-wave
  `WorldView`.

Both items are documented as candidates rather than batches so
the roadmap doesn't accumulate speculative scope. The
microbenchmark + correctness suite in batch 13c is the gate.

### 3.6.5 Batch 15 — Audit-driven hygiene + pre-batch-9 API polish  ✅ landed 2026-05-15

A focused hardening pass that closes the gaps surfaced by the
post-batch-14 audit. Scope mirrors the audit report: fix
remaining concurrency / lifetime bugs, fill API holes the
Vulkan reference renderer (batch 9) will hit, and seed the
test/benchmark coverage that matters for production
readiness. Effort ~1 week, splittable into 15a (bugs +
critical API gaps that block batch 9) and 15b (broader test
+ bench coverage).

The HIGH-severity bugs surfaced by the audit shipped as
hotfixes on 2026-05-15 (not as a batch): hierarchy-cycle
infinite loop, `EntityStorage::ensureStitched` data race
under multi-worker reads, `getEventChannelRaw` concurrent-
insert race vs. the stall-watchdog thread. Tests:
`tests/hierarchy_cycle_test.cpp`,
`tests/event_callback_reemit_test.cpp`.

What's left for batch 15:

API polish (blocking batch 9 — Vulkan renderer):

- `IRenderer::onResize(uint32_t w, uint32_t h)` hook + a
  matching `Engine::notifyResize(w, h)` so the
  swapchain-bound renderer learns of window changes without
  back-channels.
- Built-in "previous transform" stitch on `RenderFrame`.
  Every renderer ends up reimplementing
  `unordered_map<EntityHandle, Transform>`; expose the
  engine's last-frame snapshot directly.
- Owning-string variant of `DebugText` (or engine-side copy
  into a per-tick arena) — `std::string_view` lifetime
  tied to next swap is impractical with `std::format`
  temporaries.
- Camera id → index helper. Today
  `DrawItem::cameraMask` is bitset-indexed; multi-camera
  game code reinvents the mapping. Expose
  `RenderFrame::cameraIndexById(id) -> std::optional<u8>`.
- `ResourceHandle<T>` indirection (`operator->`, `operator*`,
  or `get()`) so users don't write `*reg.get(h.id())` every
  time.
- Public `Engine::workerCount()` accessor (today only via
  `jobSystemStats().workerCount`).
- Per-system `buildRenderFrame` cost in `SystemStats`
  (today bundled into step duration; Vulkan example will
  want it broken out).

Test coverage:

- `tests/concurrency_soak_test.cpp` — 8 workers ×
  sharded commit × budget+scripted skips × hot-reload
  events × RAII subscriptions × stall watchdog × 50k
  entities × 30s. Catches cross-batch interaction bugs.
- `tests/stitched_view_concurrency_test.cpp` — TSAN-
  oriented stress on `world.transforms()` post-commit
  from multiple worker jobs, validating the §3.6
  ensureStitched mutex fix continues to hold.
- `tests/render_pass_ordering_test.cpp` and
  `tests/render_frame_interpolation_test.cpp` — pre-
  batch-9 contract coverage.
- `tests/file_trace_sink_rotation_test.cpp` — focused
  rotation test with multi-file inspection.
- `tests/visibility_culling_32_cam_test.cpp` — boundary
  test for the 32-camera mask cap (Vulkan example
  with cascaded shadows may approach it).

Benchmarks:

- Hierarchy resolution cost at N=1k/10k/100k × chain
  depth.
- `forEachChunk` vs. `forEachWith` vs.
  `forEachWithCached` comparison.
- `cullByFrustum` throughput across N cameras × N
  draw items.
- `JobSystemStats::stolenJobs / totalJobs` at varying
  grain × worker count × wave width.
- `ResourceRegistry` refcounted-acquire/release under
  contention (refcount churn from streaming asset
  loaders).
- `InstanceBufferLayout::packInstances` throughput.

Doc polish:

- `Renderer.hpp` — document the
  `initialize() → false` contract (engine skips
  rendering; shutdown is a no-op for a never-
  initialized renderer).
- `ScratchArena.hpp` — clarify the "slabs survive
  across waves" claim (today `SystemContextImpl` is
  recreated per wave so the arena's lifetime is the
  wave; this is intentional but the doc reads as
  if arenas are engine-owned).
- `Telemetry.hpp::FileTraceSink` — document the
  silent-no-op behavior when the path can't be
  opened.
- `EventChannel.hpp` — explicit "warm channels at
  setup" rule for cross-thread first calls.

Gating: batch 15 should land BEFORE batch 9 starts so
the Vulkan example doesn't have to navigate around
known gaps. Batch 15a (the must-haves) is roughly
2–3 days; 15b can land alongside or after batch 9.

**As-shipped 2026-05-15.** Both 15a and 15b landed in a
single pass.

Batch 15a (seven API additions, all opt-in / additive — no
public API breakage):

- `IRenderer::onResize(w, h)` + `Engine::notifyResize(w, h)`
- `Engine::workerCount()` accessor
- `SystemStats::buildRenderFrameSeconds` per-system timing
- `RenderFrame::prevTransforms` parallel span + engine-owned
  per-tick `prevTransformMap_` refresh
- `RenderFrame::cameraIndexById(id) -> optional<uint8_t>` +
  `kMaxCameras` (= 32)
- `RenderFrameBuilder::addDebugText(pos, sv, color)` owning-
  string overload with per-builder arena
- `ResourceHandle<T>::get() / operator-> / operator*`

Batch 15b shipped 10 new tests (concurrency_soak,
stitched_view_concurrency, render_pass_ordering,
render_frame_interpolation, file_trace_sink_rotation,
visibility_culling_32_cam, renderer_resize,
resource_handle_indirection, debug_text_owning,
build_render_frame_seconds), 6 new benchmarks (hierarchy,
cull, foreach, resource_handle, pack_instances,
job_stealing) under the existing `-DTHREADMAXX_BUILD_BENCHMARKS=ON`
opt-in, and the documented clarifications on Renderer.hpp /
ScratchArena.hpp / Telemetry.hpp / Engine.hpp::events.

Test count: 79. Both default and `-Werror` builds pass
`ctest` 100% in ~33s.

What is NOT in 15:

- Vulkan renderer integration. That IS batch 9 — 15 is the
  pre-flight check that batch 9 won't trip on a known API
  gap.
- TSAN / sanitizer CI integration. The soak + stitched-view
  tests are structured to surface races under TSAN if run
  with it locally; the CI knob is a separate concern.
- Async file-trace sink. The existing `FileTraceSink` is
  sim-thread; for game-side use under tight budgets a custom
  sink threading the writer off-thread is the recommendation
  — out of scope here.

### 3.7 Batch 14 — Telemetry ingestion close-out  ✅ landed 2026-05-15

See the per-batch entry in §2 for the as-shipped shape. The
exact API ended up as `Engine::setTraceSink(ITraceSink*)`
(pointer-based; matches `setRenderer` / `setLogger`) rather than
the originally-sketched `traceSink(ITraceSink&)`. `FileTraceSink`
is not header-only (it owns a `ChromeTraceWriter` and an
`ofstream`); `HudTraceSink` is seqlock-based rather than a true
SPSC ringbuffer — the snapshot is small enough that a seqlock-
protected POD is both simpler and faster than a queue.

### 3.8 Vulkan as the implicit default for M4+

A note on strategy, since the user-facing question came up.

**Yes — designing toward Vulkan as the default reference renderer
for M4+ is the right call, and it stays compatible with the
renderer-agnostic core.** Concretely:

- The `IRenderer` interface and the flat `RenderFrame` are
  renderer-agnostic by construction; Vulkan slots in cleanly as a
  consumer. The batch-8 hierarchical `RenderFrame` (✅ §2) is still
  API-agnostic — it speaks in cameras, lights, draw items, and pose
  buffers, not in `VkCommandBuffer`.
- The Vulkan renderer lives in `examples/vulkan_renderer/`, NOT in
  the core library. It is opt-in via CMake and skipped when the
  Vulkan SDK isn't available — the same pattern `examples/boids` uses
  for SDL2 today. Core library users who never touch Vulkan pay zero
  cost.
- Optional shared helpers (instance buffer layout, frame allocator,
  upload-ring scaffolding) live in `include/threadmaxx/render/` and
  are renderer-neutral (see batch 8 in §2). Any backend (Vulkan, WebGPU via
  Dawn, D3D12, Metal via MoltenVK) can use them.
- Vulkan's style — pre-recorded command buffers, explicit batching,
  one frame-graph snapshot per submit — matches threadmaxx's existing
  per-tick `RenderFrame` snapshot model very well. Minimal
  impedance mismatch.
- Cross-platform reach is good: Linux + Windows + Android natively,
  macOS via MoltenVK (best-effort), web via WebGPU through a
  parallel adapter later.
- Costs to acknowledge up front:
  - Vulkan SDK becomes a build dep for the example (not the lib).
  - Shaders go through `glslc` (or `slang`) at build time; CMake
    rule under the example only.
  - Window/surface dep (GLFW preferred over SDL2 here — GLFW has
    cleaner Vulkan surface helpers, smaller footprint).
  - Vulkan-specific gotchas (descriptor management, validation
    layers in debug, swapchain recreation on resize) are
    confined to the example.

The strategy that protects the library is: **every renderer-facing
addition in batch 8 (✅ §2) was justified by a non-Vulkan-specific use case
first, and any future render-side addition will hold to the same rule.** If a feature only makes sense for Vulkan, it belongs in
`examples/vulkan_renderer/`, not in the core. That keeps the door
open for a WebGPU, D3D12, or even a pure-software reference renderer
later without API churn.

### 3.9 Post-Milestone-6 plan — measurement-driven tightening

Batches 1–15 closed Milestones 1 through 6. Section §3.6.4
("Profiler-driven follow-ons") has been waiting on real-workload
measurement to graduate from speculation to scheduled work, and
`threadmaxx_core_future_optimization_notes.md` codifies the
philosophy explicitly: *next gains come from cheaper chunk
traversal, cheaper command handling, fewer contention points,
better grain tuning, and occasional cache-friendly batching —
not another structural rewrite.*

§3.9 is the near-future plan derived from those notes. Five
library-side batches, sequenced so the measurement harness lands
first and every subsequent batch ships with a before/after number
in its PR. The §3.6.4 candidates are folded back in where
batch-16 evidence makes them concrete.

The mapping to the notes' "recommended order of attack" (§6):

| Notes ordering            | This plan          |
|---------------------------|--------------------|
| 1. reduce chunk iter      | §3.9.2 batch 17    |
| 2. reduce command path    | §3.9.3 batch 18    |
| 3. low-contention events  | done — batch 13c   |
| 4. measurable grain       | §3.9.1 batch 16    |
| 5. wave-local caching     | §3.9.2 batch 17    |
| 6. batching / prefetch    | §3.9.4 batch 19    |
| 7. SIMD in siblings       | §3.9.6 (not core)  |

#### 3.9.1 Batch 16 — Workload-realistic benchmark harness (gate)  ✅ landed 2026-05-16

Every later perf batch in §3.9 must clear a numeric bar on a
representative scene. Today's `bench/` directory has eight
micro-benchmarks (per batch 13c + batch 15b); this batch adds
scene-shaped end-to-end workloads and the reporting plumbing so
a regression in `forEachChunk` against an RPG-shaped scene is
visible without spelunking through perf counters.

**As-shipped 2026-05-16** — six new files, four new bench
targets:

- `bench/scene_workloads.hpp` — three deterministic seeds:
  - **`AiOnlyWorkload`** (`kAiCount` = 1,024) — Transform +
    Velocity + BoundingVolume; half also Health.
  - **`RenderAiWorkload`** (`kRenderCount` = 20,000) —
    ~50% RenderTag, ~50% Velocity, ~5% StaticTag. The
    rpg_demo shape scaled up.
  - **`ChurnWorkload`** (`kChurnCount` = 100,000) — Transform
    + Velocity, ~25% Health. Drives commit-phase and migration
    pressure.
  - `benchConfig(workers, entityCount, sharded=false)` helper
    returns a `Config` with deterministic + no-sleep + large
    initial capacity, used by every bench.
- `bench/common.hpp` — header-only `Stopwatch`,
  `LatencyHistogram` (mean / p50 / p95 / p99 / stddev,
  sort-on-finalize), `BenchRow` POD + `CsvWriter` (writes to
  stdout AND optional `argv[1]` file), `runIters(...)`
  convenience wrapper.
- `bench/chunk_iter_bench.cpp` — `forEachWith` /
  `forEachWithCached` / `forEachChunk` / `rawMaskedWalk` on AI
  and Render+AI workloads. Read-only accumulation body (no
  writes / no DCE) so iteration cost is measured pure.
- `bench/commit_path_bench.cpp` — `setTransform` /
  `setVelocity` / `addRemoveTag` / `spawnDestroy` on the
  Churn workload, run under both
  `singleThreadedCommit={true, false}`.
- `bench/migration_bench.cpp` — `setHealth` / `removeTag`
  alternating, driving migrations between the
  no-Health and with-Health archetypes. Two sweeps:
  density (fix N=32k, vary migrations/tick) and scene (fix
  50% density, vary N).
- `bench/grain_sweep.cpp` — sweeps `ISystem::preferredGrain`
  across {8, 16, 32, 64, 128, 256, 512} on AI and Render+AI
  workloads with `parallelFor(grain=0)` so the engine picks up
  the hint. Reports steal_pct alongside the percentile breakdown.
- `bench/README.md` — inventory, output-format reference, the
  shipping bar each §3.9.x batch must clear, and "how to add a
  new bench" instructions.

CSV column layout is fixed by `common.hpp::BenchRow`:
`label, workload, entities, workers, grain, mean_ns, stddev_ns,
p50_ns, p95_ns, p99_ns, throughput, steal_pct, note`. The `note`
column carries the headline derived metric
(`ns_per_entity=…` / `ns_per_cmd=…` / `ns_per_mig=…`) so a
glance at the CSV gives the same answer the bench would highlight
in a human-readable summary.

**Baseline numbers** (Release, 4 workers, dev workstation —
informational only; future PRs cite the CSVs that ship with the
binary, not these):

- `chunk_iter_bench` (AI-only, 1k entities, ns/entity):
  `forEachWith ≈ 41.4` · `forEachWithCached ≈ 44.6` ·
  `forEachChunk ≈ 18.9` · `rawMaskedWalk ≈ 2.4`. The
  ~16-ns gap between `forEachChunk` and `rawMaskedWalk` is the
  headroom batch 17 attacks.
- `commit_path_bench` (Churn, 100k entities, ns/cmd):
  `setTransform 125 (single) / 152 (sharded)` ·
  `setVelocity 95 / 114` · `addRemoveTag 145 / 283` ·
  `spawnDestroy ~1.4 µs / ~1.7 µs`. Confirms the §3.6.3
  finding that the sharded path's classifier overhead
  currently exceeds its parallelism win on every variant —
  batch 18's arena-backed recording is the right next attack
  on the value-only variants.
- `migration_bench` (density sweep, 32k scene): per-migration
  cost drops from ~1.0 µs at 16 mig/tick to ~158 ns at 32k
  mig/tick — fixed per-tick overhead amortizes as density
  rises. Batch 19's pair-batched path should improve the
  density tail.
- `grain_sweep` (Render+AI, 20k entities): clear elbow at
  grain ≥ 64 (`steal_pct` drops from ~47% to ~6%, mean step
  time roughly halves). Suggests a `preferredGrain` default of
  64–128 for canonical workloads; per-system tuning lives in
  batch 17.

Effort: ~3 hours. No public API changes. Gates every later
batch in §3.9. Both default and `-Werror` builds compile clean
on first pass; `ctest` still reports 79/79 on both.

#### 3.9.2 Batch 17 — Chunk iteration micro-optimization (gated on 16)  ✅ landed 2026-05-16

Goal: shave overhead off the chunk traversal hot path without
changing the public surface. Notes §2.1, §3.3.

**As-shipped 2026-05-16** — three landings, all in
`include/threadmaxx/Query.hpp`. No `.cpp` files touched; the
public callable shape and semantics are unchanged. ctest still
reports 79/79 on both `build/` and `build-werror/`, including
`commit_hash_test.cpp` and `sharded_commit_test.cpp` golden
determinism tests.

**Landing 1 — `forEachWith` switched to chunk-iteration internally.**
The original implementation called `world.componentMasks()`,
checked the mask per entity in the hot loop, and indexed
through the stitched cache for every component access. The new
implementation reads chunk pointers from
`SystemContext::worldView().chunks()`, builds a matching-chunk
list once (mask check **once per chunk** instead of once per
entity), and walks each chunk's per-component vectors directly.
The callable signature is unchanged
(`(EntityHandle, const C0&, …, CommandBuffer&)`); the chunked
iteration is purely an internal rewrite.

**Landing 2 — Allocation-free common case for the matching list.**
New `detail::ChunkMatchList` is a small-buffer-optimized vector
of `std::size_t` (inline cap = 32; heap spill above). Both
`forEachWith` and `forEachChunk` build their matching-chunk
list through it; the previous `forEachChunk` `std::vector<size_t>
matching` allocation per call is gone in the typical case. A
per-call `std::vector<size_t> matchIndices` is still created
for capture-by-value into the worker lambda — bounded by
archetype count, allocates ≤ 1 page on every real workload.

**Landing 3 — `MaskCache::reserve(size_t)` + `capacity()` public
overloads.** Users that know the expected match count up front
(e.g. "matches every entity; reserve `world.size()`") can
pre-warm so the first `rebuild()` skips the reallocation. The
allocation-preserved-on-`clear()` semantics already shipped in
batch 5; the new methods just expose the prefix-capacity knob.

**Internal cleanup.** `detail::getChunkSpan<C>` (previously
defined in a second `namespace detail { … }` block lower in
the file) was hoisted into the first `detail` block so the
rewritten `forEachWith` can use it. Single definition; pure
file reordering.

**Measured wins** (`build/bench/chunk_iter_bench`, AI workload,
1k entities, 4 workers; 3-run median of ns/entity):

| Path                | Before (16) | After (17) | Δ        |
|---------------------|-------------|------------|----------|
| `forEachWith`       | 41.4        | **12.4**   | **−70%** |
| `forEachChunk`      | 18.9        | **12.3**   | **−35%** |
| `forEachWithCached` | 44.6        | 60–70 ⚠   | noisy    |
| `rawMaskedWalk`     | 2.4         | 2.5        | ≈0       |

`forEachWith` now matches `forEachChunk` because both paths
follow the same chunk-walking strategy internally — the
per-entity-callable wrapper is the only difference, and the
compiler inlines it through. The Render+AI workload (20k
entities) shows all four paths converging near 65–67
ns/entity, indicating it is compute-bound on the
accumulation body, not iteration-bound — no batch-17 path
moved that needle. The `forEachWithCached` noise on AI is
intrinsic to the 64-iteration measurement window at this
workload size (`stddev/mean ≈ 20–35%`); the unrelated
pre-existing `foreach_bench` (write-heavy, larger windows)
shows the cached variant remains the fastest of the three
at scale. Treated as bench variance, not a landing
regression — re-measure with a wider iteration window if a
real-game profile flags it.

Public surface impact: additive only — `MaskCache::reserve`
and `MaskCache::capacity` are the only new symbols. Everything
else is internal (header-only) refactoring. No call site needs
to change to pick up the speedup; rebuilding existing code
against the new header is sufficient.

#### 3.9.3 Batch 18 — Command buffer arena + compact payloads (gated on 16)  ✅ landed 2026-05-16

Goal: cut allocations and variant overhead in command
recording. Notes §2.2, §3.1.

**As-shipped 2026-05-16.** The investigation surfaced a sharper
diagnosis than the original spec: the dominant inefficiency was
not the `std::vector<Command>` regrowth pattern (which already
benefits from `reserve()`), but the **variant size** itself.
`std::variant<CmdSpawn, CmdSetTransform, …>` was 256 B because
`CmdSpawn` (248 B) and `CmdAddUserComponent` (112 B) dwarfed
every other alternative — a `vector<Command>` of 100k value-only
commands consumed ~25 MB with ~80 % padding. The right cut
turned out to be smaller than the spec called for: move the two
oversize variants to heap-backed `std::unique_ptr` wrappers,
leaving the variant at the next-largest size
(`CmdSetTransform` at 48 B → variant at 64 B).

Three files changed; public API unchanged:

- **`CommandBuffer.hpp`** — new type aliases
  `detail::CmdSpawnPtr = std::unique_ptr<CmdSpawn>` and
  `detail::CmdAddUserComponentPtr = std::unique_ptr<CmdAddUserComponent>`.
  The `Command` variant lists these two alternatives in place of
  the old POD versions; every other alternative is unchanged.
  `<memory>` is the only new include.
- **`CommandBuffer.cpp`** — all six `spawn` overloads + both
  `spawnBundle` overloads use `std::make_unique<detail::CmdSpawn>(...)`.
  No other recording method changed; the four value-only setters
  stay one `emplace_back` of a small POD.
- **`UserComponent.hpp`** — `addUserComponent<T>` constructs
  via `auto c = std::make_unique<detail::CmdAddUserComponent>()`
  and stores the inline / heap payload via `c->`.
- **`EngineImpl.cpp`** — `applyCommandImpl` /
  `hashCommandImpl` / `commandTargetEntity` use a small `unwrap`
  helper that dereferences the `unique_ptr` for the two
  pointer-backed alternatives and returns the variant alternative
  by reference otherwise. The bodies of the visit branches stay
  byte-for-byte the same; the helper just gives them a single
  uniform `c.field` access pattern. The hash function is
  unchanged — the same FNV-1a-64 bytes are mixed in the same
  order, just sourced from `*ptr` instead of inline storage.

The choice deliberately *avoided* the spec's "1-byte tag +
≤23-byte inline payload" wire format. That layout would have
needed a custom byte-arena, a hand-rolled visitor, and a refactor
of every `applyCommandImpl` branch — and the math doesn't work:
`CmdSetTransform` already needs 48 bytes (EntityHandle +
Transform), which doesn't fit in 23. The smaller change
accomplishes the same headline goal (small variant, low memory
pressure) with a fraction of the blast radius. Per-chunk
record-time routing (the §3.6.4 candidate) remains parked.

**Measured wins** (`build/bench/commit_path_bench`, Churn workload
100k entities, 4 workers, 3-run median ns/cmd):

| Variant       | Single (B16 → B18)        | Sharded (B16 → B18)       |
|---------------|----------------------------|----------------------------|
| `setTransform`| 125.6 → **117.1 (−7%)**    | 152.0 → **138.4 (−9%)**    |
| `setVelocity` | 95.7  → **88.1 (−8%)**     | 114.5 → **109.5 (−4%)**    |
| `addRemoveTag`| 145.4 → **129.5 (−11%)**   | 283.6 → **274.2 (−3%)**    |
| `spawnDestroy`| 1442  → 1599 (+10% ⚠)      | 1680  → 1696 (+1%)         |

Clean wins on every high-frequency value setter. The
`spawnDestroy` regression is expected — each spawn pays one
extra `new` for the `CmdSpawn` payload. Worth the trade because
spawn is rare in real workloads (~1% of commands in `rpg_demo`),
while `setTransform` and `setVelocity` happen every tick on
every moving entity.

**Memory:** `sizeof(detail::Command)` dropped from **256 B to
64 B** (4×). A 100k-entry `std::vector<Command>` now holds ~6.4 MB
instead of ~25.6 MB. Batch-17's chunk iteration numbers stayed
intact (`forEachWith` 13.8 ns/entity, `forEachChunk` 10.0
ns/entity on AI workload).

**Verification:** both `build/` and `build-werror/` compile clean
on first pass; `ctest` still reports 79/79 on both, including
`commit_hash_test.cpp` and `sharded_commit_test.cpp` — proving
the per-tick `commitHash` is **byte-identical** to the
pre-batch-18 reference. The `unwrap` helper is the load-bearing
piece: by dereferencing the `unique_ptr` before the
`mixHashBytes` call, the hash sees the same POD bytes it always
did.

Public surface impact: **zero**. Recording API (`spawn`,
`setTransform`, `addUserComponent<T>`, …) is unchanged; the
variant alternatives in `detail::Command` differ but that
namespace is internal-only.

#### 3.9.4 Batch 19 — Migration batching by archetype pair (gated on 16)  ✅ landed 2026-05-16

Goal: when many entities migrate between the same archetype
pair in one tick, perform the move in one contiguous loop
instead of N individual swap-and-pops. Notes §3.2.

**As-shipped 2026-05-16.** Profiling against the batch-16
`migration_bench` produced a sharper diagnosis than the spec:
the per-migration cost was already at **158 ns at high
density** (cache-friendly memcpy territory, 14 component
vector touches × ~11 ns each). The spec's "bulk-push N rows
into the destination, then one swap-and-pop range" would have
required a custom path in `ArchetypeTable` and a refactor of
`migrate()` — high risk for a tail-case win. The right cut
turned out to be smaller: **pre-reserve the destination
chunk's vectors** when `commitBuffer` detects a run of
consecutive commands all toggling the same mask bit. The
geometric growth that `std::vector` does inside the dst
chunk's per-component vectors is amortized into a single
`reserve` call per run.

Three changes; all internal:

- **`ArchetypeTable::reserveChunkRows(dstMask, extra)`**
  (`include/threadmaxx/internal/Archetype.hpp` + `Archetype.cpp`).
  Looks up (or creates) the chunk for `dstMask` and reserves
  `current + extra` capacity on every per-component vector
  whose bit is in the mask. The chunk's `entities`,
  `denseToSlot`, and `masks` vectors are always reserved.
- **`commitBuffer` adjacent-run detection**
  (`src/EngineImpl.cpp`). For each command, classify by
  `(variant, payload-key)`: `CmdAddTag(tag)`,
  `CmdRemoveTag(tag)`, `CmdSetHealth`, `CmdSetFaction`,
  `CmdSetBoundingVolume` (the §3.1 batch-5 setters that
  attach a presence bit). Greedy-extend the run while the
  next command has the same key. If the run length is ≥ 8,
  predict the destination mask from the first entity's
  current archetype and call `reserveChunkRows`. Then apply
  the run command-by-command using the existing
  `applyCommandImpl` + `hashCommandImpl` path — submission
  order preserved, commit hash byte-identical.
- **Threshold:** runs shorter than 8 commands skip the hint
  (the overhead of the predict + reserve isn't worth it for
  tiny runs).

The bigger refactor the original spec called for (bucket
commands by `(srcArch, dstArch)`, bulk-push rows, bulk
swap-and-pop) remains parked. Per the optimization notes
(§3.2): *"This should stay behind profiling until the current
commit model shows a clear migration bottleneck."* The shipped
pre-reserve captures the headline win — geometric growth
amortization — without touching the carefully-tested
`migrate()` swap-pop sequencing.

**Measured wins** (`build/bench/migration_bench`, 4 workers,
3-run median ns/mig):

| Workload                       | Batch 16 | Batch 19 | Δ        |
|--------------------------------|----------|----------|----------|
| density-32k @ 256 mig/tick     | 307      | **251**  | **−18%** |
| density-32k @ 1024 mig/tick    | 198      | **175**  | **−12%** |
| density-32k @ 4096 mig/tick    | 166      | 167      | ≈0       |
| density-32k @ 32k mig/tick     | 159      | **152**  | −4%      |
| scene-50pct @ 1024 entities    | 250      | **185**  | **−26%** |
| scene-50pct @ 8192 entities    | 185      | **170**  | −8%      |
| scene-50pct @ 32k entities     | 166      | **149**  | **−10%** |
| scene-50pct @ 100k entities    | 158      | **147**  | **−7%**  |

Headline wins on the **low/medium density tail** (~12–26%),
where vector geometric growth dominated. At very high
densities the vectors are already large enough that one extra
geometric step is a small fraction of total work — modest
wins (4–10%) but consistent.

**Verification:** both `build/` and `build-werror/` compile
clean; `ctest` reports **80/80** on both, including
`commit_hash_test.cpp` and `sharded_commit_test.cpp` — proving
the per-tick `commitHash` is byte-identical to the
pre-batch-19 reference. Batch 17 chunk-iteration numbers
(`forEachWith 12.7 ns/entity`) and batch 18 commit-path
numbers (`setTransform 115.6 ns/cmd`) both preserved.

Public surface impact: **zero**. `reserveChunkRows` is in
`internal/` and only invoked by `EngineImpl`. The recording
API and commit semantics are unchanged.

Sharded commit path (`commitBuffersSharded`) was **not**
updated in this batch — the sharded path is opt-in
(`singleThreadedCommit = false`) and currently slower on
every measured workload (per §3.6.3 batch 13c). Adding the
same adjacent-run detection there is straightforward but
moot until the sharded path itself wins a benchmark.

#### 3.9.5 Batch 20 — Async snapshot + trace-sink off-thread (quality of life)  ✅ landed 2026-05-16

Goal: take long save / trace operations off the sim thread so
a quick-save can't blow `FrameBudgetWatcher`. Notes §3.5. Not
perf-gated — purely a stutter quality fix.

**As-shipped 2026-05-16.** The spec's "JobSystem job that runs
`world.snapshot()` against the published `WorldView`"
description didn't quite match how `WorldView` works: the view
caches *chunk pointers*, not chunk *contents*, so a job
running `world.snapshot()` on a worker would still race
mid-snapshot commits. The shipped design is cleaner: capture
the snapshot synchronously on the sim thread (vector copies
are ~ms even at 100k entities) and dispatch the user's
callback to a **dedicated engine-owned background thread**.
The sim-thread snapshot work is the only sync part — typically
under a millisecond — and the I/O happens off-thread.

Three additions; all opt-in:

- **`FileTraceSink::setAsync(bool)` + `isAsync()`**
  (`include/threadmaxx/Telemetry.hpp` +
  `src/Telemetry.cpp`). When `setAsync(true)`, `onFrame` copies
  the borrowed `FrameSnapshot` into an `OwnedFrameSnapshot`
  (deep copy of the `systems` span) and enqueues onto an
  internal `std::deque` under a mutex+CV. A dedicated writer
  thread drains the queue and performs the file I/O. Producer
  side is one short critical section + a notify; budget the
  sim-thread cost at a few microseconds. Setting `false`
  joins the worker and drains the remainder synchronously.
  Default stays synchronous → bit-for-bit pre-batch-20
  behavior. Dtor joins the worker automatically.
- **`Engine::snapshotAsync(callback)`**
  (`include/threadmaxx/Engine.hpp` + `src/Engine.cpp` +
  `src/EngineImpl.{hpp,cpp}`). Captures
  `world().snapshot()` synchronously on the sim thread, then
  posts the user's callback onto a dedicated engine-owned
  background snapshot thread. The worker is lazily spawned on
  the first call; joined in `Engine::shutdown` and in
  `~EngineImpl` (defensive). Multiple in-flight callbacks
  queue in submission order. The callback receives the
  snapshot by value (move-constructed) and runs on the
  background thread — game code is responsible for not
  calling back into the engine's mutation API.
- **`tests/async_snapshot_test.cpp`** — two sub-tests. (A)
  Async `FileTraceSink` writes a valid Chrome-trace file
  across 64 ticks under load; `setAsync(true→false)` cleanly
  joins the writer; the file ends with the expected closing
  `]`. (B) `snapshotAsync` callbacks fire in submission
  order, the snapshot reflects state at the moment of the
  call (monotonically growing entity count under a
  `SpawnEveryTick` system), and `Engine::shutdown` joins the
  worker so every queued callback has fired by the time
  shutdown returns.

**Consistency contract** (documented on the API): the
snapshot reflects state at the moment `snapshotAsync` was
called — i.e., the last committed wave. Commits that happen
after this method returns do not retroactively appear. Same
model as the renderer's double-buffered `RenderFrame`.

**Verification:** both `build/` and `build-werror/` compile
clean; ctest reports **80/80** on both trees (was 79/79
before batch 20; +1 from `async_snapshot_test`). The async
writer thread is joined in `FileTraceSink::~FileTraceSink`
and `EngineImpl::~EngineImpl`; no leaked threads.

Public surface impact: **additive**.
- `FileTraceSink`: gains `setAsync(bool)` + `isAsync()`.
- `Engine`: gains `snapshotAsync(std::function<void(WorldSnapshot)>)`.

No existing call sites need to change. Synchronous behavior
is unchanged when `setAsync(false)` (the default).

### 3.10 Post-§3.9 follow-ons (current)

§3.9 closed; §3.10 captures the next sweep of work. Three
parallel tracks, sequenced for ROI:

- **§3.10.1 Batch 21** — sharded-commit overhead deep-dive
  (gated on `commit_path_bench` regressions exposed by §3.9).
- **§3.10.2 Batch 22** — audit-driven hygiene (post-§3.9
  codebase sweep; HIGH-severity findings only).
- **§3.10.3 Batch 23** — ergonomics + polish (lower-severity
  audit findings + missing-feature requests from
  `examples/rpg_demo/`).

#### 3.10.1 Batch 21 — Sharded-commit overhead reduction  ✅ landed 2026-05-16

The §3.6.3 batch 13c microbench takeaway was "sharded commit
slower than single-threaded on every measured workload."
Batch 21 traces the overhead to four roots and ships fast
paths that turn the addRemoveTag regression into a tie and
shave 5–11% off the value-only sharded paths.

**Root-cause analysis** (`commit_path_bench` on Churn 100k):

1. `commandIsMigrating(cmd)` + `commandTargetEntity(cmd)`
   both go through `std::visit` (~5 ns each). Pass A and
   Pass B each call them once per command → 4 visits × 5 ns
   × 100k = **2 ms of pure dispatch overhead**.
2. `std::unordered_set<EntityHandle>` for the migrating set
   — 100k inserts ≈ ~10 ms of hashing + heap allocations.
3. `storage.locate(e)` per command in Pass B for the
   chunk-bin routing — 100k × ~30 ns = **3 ms of slot
   indirection**.
4. Single-archetype workloads: only one chunk → at most one
   parallel job in Pass C → **no parallelism win** but full
   overhead paid.

The addRemoveTag workload hit all four failure modes at
once: every command migrates, so the migrating set fills to
100k AND no value-only fast-path commands exist AND we still
walked the buffers twice. Result: **274 ns/cmd** vs 145
ns/cmd single-threaded.

**Shipped 2026-05-16** — four changes:

- **Per-buffer value-only counter**
  (`CommandBuffer::valueOnlyCount()`,
  `include/threadmaxx/CommandBuffer.hpp` +
  `src/CommandBuffer.cpp`). The four value-only recording
  methods (`setTransform` / `setVelocity` / `setUserData` /
  `setAcceleration`) bump a per-buffer counter. The commit
  phase sums these without re-scanning the variant stream.
- **Three pre-condition fast paths** in
  `EngineImpl::commitBuffersSharded`. Any one failure falls
  through to `commitBuffer` per source buffer:
    - `totalCommands < 256` — overhead exceeds win at small
      batches.
    - `totalValueOnly == 0` — every command migrates,
      Pass C would be empty.
    - `chunks().size() < 2` — only one archetype, no
      parallelism possible.
- **Engine-owned migrating bitmap** replacing the
  `std::unordered_set<EntityHandle>`. `EngineImpl` now owns
  `shardMigratingBitmap_` (`std::vector<uint8_t>` keyed by
  `EntityHandle::index`) + `shardMigratingIndices_` (the
  indices touched this call, for fast-clear at the end).
  Preserved across commits so the steady state pays zero
  allocations. Lookups are 1-byte indexed reads; inserts
  are `bitmap[idx] = 1` + a parallel push_back to the
  index list.
- **Engine-owned chunkBins reuse**. `EngineImpl` now owns
  `shardChunkBins_`; the commit clears each bin's contents
  (preserving allocations) and resizes the outer vector
  only when chunks have grown.
- **New `EntityStorage::slotCount()` accessor** so the
  bitmap can be pre-sized to the safe upper bound.
- **New `MultiArch` workload in `commit_path_bench`** —
  four distinct archetype shapes × 25k entities each, with
  setTransform commands spread evenly. Used to test whether
  sharded can *win* on a multi-archetype workload.

**Measured wins** (3-run median ns/cmd, Churn 100k):

| Variant       | B18 sharded | B21 sharded | Δ        |
|---------------|-------------|-------------|----------|
| `setTransform`| 138.4       | **131.0**   | −5.4%    |
| `setVelocity` | 109.5       | **97.6**    | **−10.9%** |
| `addRemoveTag`| 274.2       | **134.7**   | **−50.9%** |
| `spawnDestroy`| 1696        | **1635**    | −3.6%    |

The **addRemoveTag −51% win** is the headline: the
fall-through kicks in (all migrating, single chunk in §3.10.1
runs), so sharded now matches the single-threaded path
instead of paying 2× for nothing. The value-only paths gain
modestly from the bitmap + reused storage.

**Finding: sharded cannot beat single-threaded on tested
workloads.** The MultiArch sweep (4 archetypes, 25k each):
single 109 ns/cmd vs sharded 137 ns/cmd. Sharded still
loses by ~25%. Root cause: even with 4 parallel jobs, the
serial Pass A + Pass B classifier overhead (~130 ns/cmd of
visits + locate + hash) exceeds what 4-way parallelism can
recover on a 50 ns/cmd applyCommandImpl path. The hash itself
(~70 ns/cmd of FNV-1a byte-by-byte over the 48-byte
`CmdSetTransform`) is on the serial path and is fixed by the
determinism contract.

**Conclusion: sharded commit is a fallback, not a default.**
The current implementation is the most we can do without
structural changes (record-time chunk routing, parallel hash
reduction, or a different determinism contract — all parked).
`singleThreadedCommit = true` remains the default; the
sharded path is the documented immediate fallback if any
divergence is ever observed in production AND a workload
emerges where per-cmd apply >> classifier overhead.

**Verification:** both `build/` and `build-werror/` compile
clean; ctest still reports **80/80** on both trees, including
all the commit-hash determinism goldens.

#### 3.10.2 Batch 22 — Audit-driven hygiene  ✅ landed 2026-05-16

Post-§3.9 codebase audit surfaced 15 findings (HIGH +
MEDIUM + LOW). Batch 22 shipped four HIGH/MEDIUM fixes
plus one ergonomic add and one documentation fix. F8
investigated then reverted on safety grounds.

**As-shipped 2026-05-16:**

- **F1 (HIGH) — Wave-parallel system updates via JobSystem.**
  `EngineImpl.cpp` was spawning raw `std::thread`s for each
  concurrent system in a wave, joined at end-of-wave. At
  60 Hz with multi-system waves that's hundreds of
  `std::thread` creations per second. The fix routes the
  wave's non-tail systems through `jobs_->submit` + a
  `std::latch`; the tail continues to run on the sim thread
  to avoid a wasted submit + wait round-trip. Workers are
  already parked on a CV, so wakeup is sub-µs instead of
  multi-ms `thread` create. No public API change; preserved
  determinism + 80/80 ctest.
- **F3 (HIGH) — HierarchySystem allocation hygiene.**
  `HierarchySystem.cpp` was allocating one `unordered_map`,
  four `std::vector`s, and a full-population `worldT` copy
  per tick (per call inside `ctx.single()`). Promoted all
  scratch state to system-member fields; `clear()` /
  `assign()` reuse the allocations across ticks. Steady
  state pays zero allocations after the first tick with a
  given entity count.
- **F4 (HIGH-doc) — Stitched-view + sharded-commit contract.**
  Added a 25-line `@par Stitched-view contract` block to
  `World.hpp` explaining the safe consumption patterns for
  `world.transforms()` / `velocities()` / etc. during a
  sharded commit's Pass C. The contract documents three
  safe consumption modes (inside a wave from a non-writing
  worker, between waves with single-threaded commit, or via
  the chunked path / `has<T>` / `get<T>`) and one unsafe
  mode (reading the stitched view from a non-sim thread
  during a sharded commit Pass C in flight).
- **F7 (MEDIUM) — prevTransformMap → flat vector.**
  `EngineImpl::prevTransformMap_` was a freshly-cleared
  `unordered_map<EntityHandle, Transform>` rebuilt every
  tick. Replaced with two flat vectors keyed by
  `EntityHandle::index` — `prevTransformByIndex_`
  (Transform) and `prevTransformGenByIndex_` (the
  generation guard). The read path checks `gen[idx] ==
  handle.generation` to filter stale entries; the write
  path `resize`s on demand. Zero-allocation steady state
  after the first frame.
- **F10 (MEDIUM) — `Bundle::with<T>(value)` builder.**
  Added a chainable method to the `Bundle` POD that sets
  the per-field value AND attaches the matching presence
  bit in `initialMask` in one call. Composes for
  `Bundle{}.with(Transform{...}).with(Velocity{...}).with(Health{...})`
  — much harder to forget the mask half than the
  field-and-mask split. Decided to keep the
  `bundle(Transform{}, Velocity{}, …)` factory too, since
  it remains the simplest path for "value-only" cases.

**F8 (MEDIUM) investigated then reverted.** The initial
implementation used a `thread_local static EventChannel<Ev>*
cachedChannel` with `Engine*` validity guard to skip the
`eventChannelsMtx_` on hot-path `events<T>()` calls. It
broke `commit_soak_test` and `concurrency_soak_test` because
back-to-back engine creation/destruction in tests can land a
fresh engine at the recycled address of a destroyed one —
the cached channel pointer then dangles. A correct
implementation needs a per-engine version counter alongside
the cached pointer, which is more bookkeeping than the
~30 ns mutex acquire is worth. Reverted with an explicit
comment in `EventChannel.hpp::Engine::events()`; the public
"warm channels at setup" documentation stands as the
recommended workaround for cross-thread emit hot paths.

**To-defer (§3.10.3 ergonomics + polish, planned):**

- F11 — `Engine::userComponent<T>()` token lookup.
- F12 — bulk-spawn helper.
- F13 — `World::forEachChunkOf` introspection helper.

**To-defer (lower priority, profile-driven):**

- F2 — `ResourceRegistry::get<T>()` raw-pointer aliasing
  with the legacy `add`/`remove` path. Already documented;
  the refcounted path is the recommended replacement.
- F5 — `RenderFrameBuilder` debug-text string-view fragility.
  Currently fine; would only fire under a future refactor.
- F6 — `commandIsMigrating` double-visit in sharded commit.
  Already addressed by batch 21's bitmap (now visited once
  in Pass A, lookup is bitmap-indexed in Pass B).
- F8 — proper event-channel cache requires per-engine
  version counter; deferred until profile data justifies it.
- F9 — `aggregateLoaderStats` virtual-call churn. Per-tick
  diagnostic; cache only if profiling flags it.
- F14 — `CmdAddUserComponent::size` field encoding. Internal
  detail; rename only on a deliberate batch.
- F15 — `EventChannel::subscriberCount` mutex acquisition.
  HUD-only, single-digit ns; rename only if observed in
  trace data.

**Verification:** both `build/` and `build-werror/` compile
clean on first pass; ctest **80/80** on both. The rpg_demo
runs validation-clean for 120 ticks. The `commit_hash_test`
and `sharded_commit_test` goldens remain byte-identical to
the pre-batch-22 reference — no behavior change observable
from the engine's external surface.

Public surface impact:
- **Additive**: `Bundle::with<T>(value)` template method.
- **Documentation-only**: `World.hpp` stitched-view contract
  block.
- **Internal**: F1 / F3 / F7 are pure refactors with no
  visible behavior change.

#### 3.10.3 Batch 23 — Ergonomics + polish (rolling)

Defers from §3.10.2 plus the missing-feature requests from
`examples/rpg_demo/`. Items land opportunistically as they
become useful; this is a rolling batch, not a single ship.

**As-shipped 2026-05-17:**

- **F11 — `Engine::userComponent<T>()`** (additive). Lazy
  lookup of a previously-registered `UserComponentId` by
  `typeid(T)`. Returns an invalid id (test with
  `.valid()`) when never registered — no auto-register on
  miss. Pairs with the existing
  `Engine::registerUserComponent<T>()`; both go through
  the engine-internal `UserComponentRegistry`. Designed to
  unblock the demo's `UserComponentIds*`-threading
  pattern (every system constructor takes the pointer);
  new systems can now call `engine.userComponent<MyType>()`
  on demand.
  New `engine_user_component_lookup_test.cpp` verifies
  the contract: registered types resolve, unknown types
  return `invalid`, and re-registration returns the same
  id (idempotent per spec).

- **F12 — `CommandBuffer::spawnBundleN(span<const EntityHandle>,
  span<const Bundle>)`** (additive, landed 2026-05-17).
  Bulk-spawn helper: pairs each reserved handle with the matching
  bundle and emits N `spawnBundle` commands. The shorter span
  bounds the count (mismatched-length spans are tolerated; extra
  entries silently skipped). Pre-reserves command-buffer storage
  to amortize the per-spawn `emplace_back` growth. Combined with
  the §3.9.4 batch 19 migration-batching hint inside
  `commitBuffer`, a bulk spawn pays roughly one geometric-growth
  event per destination chunk regardless of N. Required `<span>`
  added to `CommandBuffer.hpp`'s include list. New
  `tests/command_buffer_spawn_n_test.cpp` (16-entity split
  archetype + partial-span tolerance).

**Still planned:**

- F13 — `World::forEachChunkOf` introspection helper.
- F8 (deferred from §3.10.2) — proper event-channel cache
  via per-engine version counter.

### 3.11 RPG-demo-driven library exercise plan

#### 3.11.0 Batch D-audit — Demo test suite + critical bug fixes  ✅ landed 2026-05-16

Interjected between batches D2 and D5 after a manual playtest
revealed that combat never connected — the player could swing,
but enemies never took damage. Sets up a dedicated headless test
suite for the demo, then uses it to capture (and fix) the bugs.

**Architecture changes:**

- **`rpg_demo_core` static library.** `examples/rpg_demo/`
  now builds the system + game-logic .cpp files into a static
  library that the executable and tests both link against. GLFW
  is a public dep of `rpg_demo_core` (for the global `Input`
  state). Vulkan is only linked into the executable. Tests get
  to exercise the actual shipped code rather than re-implementing
  it inline.
- **`tests/rpg_demo/` test subfolder.** Five headless tests
  registered under the `rpg_demo.test_*` namespace:
  - `test_round_trip` — save / load round-trip via the actual
    `SaveLoadSystem` (replaces the inline-reimplementation
    `tests/rpg_save_load_test.cpp` which is now removed).
  - `test_combat` — spawns a controlled NPC at the predicted
    sword-tip world position, injects the F-key edge, verifies
    HP drops by `kSwordDamage`. The bug catcher.
  - `test_npc_brain` — runs 300 ticks, walks every NPC chunk,
    verifies at least one entered `Fight` or `Flee`.
  - `test_pickup` — spawns a controlled pickup inside
    `kPickupRadius` of the player, verifies collection +
    `PlayerState.pickups` increment.
  - `test_input_edges` — injects three edge bits, steps one
    tick, verifies all three were consumed (regression test
    for the input-edge consumption pattern).
- **`tests/rpg_demo/DemoTestHarness.hpp`** — shared
  `HeadlessFixture` + `makeHeadless()` + `injectEdge()` /
  `resetEdges()` helpers. Field order matters: `game` before
  `engine` so the engine's `~EngineImpl::shutdown()` (which
  calls `game_->onTeardown`) sees a live game on tear-down.
- **`DemoGame::ids()` public getter** — small API tweak so
  tests can fetch the registered `UserComponentIds` without
  re-registering. Pre-D-audit the field was private-only.

**Bugs found and fixed:**

1. **Sword tip computed in the wrong direction** —
   `CombatSystem.cpp` rotated `(0, 0, +length)` by the sword's
   world orientation, placing the tip BEHIND the player (the
   demo's camera math has the player facing world `-Z` when
   yaw = 0). Combat could only "hit" entities at `(player.xz)
   + (0.5, 0.6)` in fixed world space, which almost never
   overlapped a hostile NPC. **Fix:** rotate `(0, 0, -length)`
   instead. The full 3×3 quaternion-to-matrix expansion is
   needed (not the abbreviated form that assumed `dirX = dirY
   = 0`) so the tip rotates with the sword's full world
   orientation, including the player's yaw.
2. **Player `Transform.orientation` never updated** —
   `PlayerInputSystem` tracked `PlayerState.yawRadians` for
   movement direction but never wrote the matching rotation
   into the player's actual `Transform.orientation`. Since
   `HierarchySystem` propagates `parent.world * child.local`,
   the sword (a Parent-attached child) stayed in fixed world
   space regardless of which way the player turned. **Fix:**
   `PlayerInputSystem.update` now writes
   `Transform.orientation = quat(yaw around Y)` via a
   `cb.setTransform` call alongside the existing velocity +
   PlayerState writes. Hierarchy propagation then carries the
   rotation into the sword, and the (-length)-rotated tip
   tracks the camera.

**Bugs investigated and dismissed:**

- `PlayerInputSystem::takeEdges()` consuming ALL edge bits
  was initially flagged as a regression risk for F1 / F5 /
  F8 / F9 keys. Investigation showed it's actually fine:
  `HudSystem` / `SaveLoadSystem` consume their edges in
  `preStep` which runs BEFORE any wave's `update`, so by the
  time `PlayerInputSystem::update` calls `takeEdges()` those
  bits are already cleared by their owners. The test
  `test_input_edges` is the regression guard.

**Verification:**
- Both `build/` and `build-werror/` clean.
- **ctest 85/85 on both trees** (was 81; +5 demo tests, −1
  removed duplicate `rpg_save_load_test`).
- The rpg_demo runs **validation-clean for 300 ticks** under
  `THREADMAXX_VK_VALIDATE=1` on the `-Werror` tree.
- Interactive playtest: combat now visibly connects when the
  player faces an NPC and presses F.

**Files:** 5 new test files in `tests/rpg_demo/`, 1 new
harness header, 1 new test CMakeLists, demo CMakeLists
refactored to define the static-library target, 1 removed
duplicate test, 1 small `DemoGame.hpp` getter add, 2 demo
.cpp bug fixes (CombatSystem, PlayerInputSystem).

**Effort:** ~2 hours actual, including the test
infrastructure setup that future §3.11 batches will reuse.


§3.11 takes the opposite stance to §3.9–§3.10: instead of
"measure → optimize", it's "build a real game → discover
gaps → fix the library." The premise is that `rpg_demo` as
of batch 10 ships ~152 entities, 10 systems, and 4 user
components — it covers the public-API basics but leaves
many engine features unexercised against real workloads.

Each §3.11 batch adds **one game-shaped feature** to
`rpg_demo` and is allowed to schedule **one core library
follow-on** if implementation surfaces a library gap. The
core follow-ons go into §3.10.3 (ergonomics) or §3.12+ (new
perf / structural work) depending on shape.

Each batch must:

1. **Add a real game feature** — not a contrived test path.
2. **Cite the engine subsystems it exercises** — the
   per-batch as-shipped block lists them so readers know
   what was tested.
3. **Run validation-clean for ≥ 300 ticks** on both build
   trees, including under `THREADMAXX_VK_VALIDATE=1`.
4. **Keep ctest 80/80** — any newly-introduced library
   change ships with a test.
5. **Stay within the engine's public API** unless a core
   batch is scheduled — patches to `include/threadmaxx/` or
   `src/` outside the per-batch core follow-on are not
   allowed.

#### 3.11.1 Batch D1 — Combat + hierarchy + damage events  ✅ landed 2026-05-16

**Game feature:** Player can swing a sword (F key). The
sword is a `Parent`-attached child entity of the player,
positioned at hip-front via `Parent::localOffset` and
propagated each tick by the engine's `HierarchySystem`. On
swing-start (rising-edge detection on `PlayerState.swordSwingTimer`),
the `CombatSystem` queries the spatial hash within
`kSwordTipRadius` of the sword tip and emits `DamageDealt`
events for every hostile NPC found. NPCs in `Fight` mode
charge the player; on HP < 30% they enter `Retreat` and
flee at `kRetreatSpeed`. Killed NPCs flip `DisabledTag`
(visibly disappear) and drop a 2× pickup at their death
location. HUD shows live kill counter; floating "HP: 18/60"
labels appear over every damaged NPC, color-coded by
health fraction (white > 66%, yellow 33–66%, red < 33%).

**As-shipped 2026-05-16** — six new files +
extensions to four existing systems:

- **`CombatSystem.{hpp,cpp}`** — reads the sword's
  hierarchy-propagated world transform, computes tip
  position via quaternion rotation of `(0,0,length)`,
  queries the spatial hash, emits `DamageDealt` events.
  Rising-edge detection (`prevSwingTimer_ < 0 && curr > 0`)
  guarantees one damage burst per swing.
- **`DamageSystem.{hpp,cpp}`** — `preStep` drains the
  `DamageDealt` channel into a per-target accumulator
  (multiple hits in one tick compose into one Health write).
  `update` applies the HP changes via `cb.setHealth`
  (preserving each entity's `max` from the pre-hit snapshot)
  and emits `EntityDied` on the kill blow.
- **`RespawnSystem.{hpp,cpp}`** — `preStep` drains
  `EntityDied`, reserves a fresh pickup handle via
  `engine.reserveEntityHandle()` per kill. `update` adds
  `DisabledTag` to the corpse and spawns the gold pickup
  at the death position with `Pickup{2u}` value.
- **`HealthBarSystem.{hpp,cpp}`** — runs in
  `buildRenderFrame` (not `update`), iterates chunks
  carrying `Health + Transform - DisabledTag`, emits one
  `DebugText` per damaged entity via the **owning-string
  overload** added in §3.6.5 batch 15a. Color choice based
  on HP fraction.
- **`DemoTypes.hpp` extensions** — five new types:
  `SwordTag` user-component (sword length), `DamageDealt`
  event, `EntityDied` event, plus `NpcState::Fight` and
  `NpcState::Retreat` enum values, plus
  `PlayerState::swordSwingTimer` and `swordKills` fields.
  New constants: `kSwordSwingSeconds`, `kSwordDamage`,
  `kSwordTipRadius`, `kPlayerMaxHP`, `kHostileMaxHP`,
  `kFriendlyMaxHP`.
- **`PlayerInputSystem` extension** — now reads the F-key
  attack edge via `takeEdges()`, decrements
  `swordSwingTimer` by `ctx.dt()` each tick, arms a new
  swing when the edge fires AND the timer is at 0. Writes
  via `addUserComponent` (overwrite semantics for an
  already-present user component).
- **`NPCBrainSystem` extension** — added `Fight` and
  `Retreat` states. Hostile NPCs switch from `Idle` /
  `Wander` straight to `Fight` (instead of `Flee`) when
  the player enters AoI. `Fight` charges at the player at
  `kChargeSpeed`, stopping at `kFightStopDist` so they
  don't overshoot. Low-HP transition to `Retreat`; runs
  for `kRetreatDur` at `kRetreatSpeed`. Friendly NPCs keep
  the pre-D1 `Flee` behavior.
- **`HudSystem` extension** — subscribes to `EntityDied`
  via `subscribeScoped` (a second `threadmaxx::Subscription`
  member); each emit increments `WorldState::totalKills`.
  HUD line now reads
  `[hud] tick=N entities=M pickups=P kills=K sun=S`.
- **`DemoGame.cpp` extensions** — registers `SwordTag`
  user-component; pre-warms `PickupCollected` /
  `DamageDealt` / `EntityDied` channels on the sim thread
  per the documented "warm channels at setup" rule; spawns
  the sword as a `Parent`-attached child of the reserved
  player handle (initialMask = Transform + Parent +
  BoundingVolume); inserts `makeHierarchySystem()` between
  `MovementSystem` and `CombatSystem` so the sword
  transform is propagated before combat reads it.

**Engine subsystems exercised** (none in the demo
pre-D1):

- `Parent` component + `HierarchySystem` (sword
  attachment).
- `EventChannel<DamageDealt>` + `EventChannel<EntityDied>`
  with persistent subscribe + RAII `Subscription`.
- `SpatialHash::forEachInRadius` (existing path,
  exercised at a new query position each swing).
- Owning-string `RenderFrameBuilder::addDebugText` from
  batch 15a.
- `Engine::reserveEntityHandle` (per-death loot drop).
- `cb.setHealth` + `cb.addTag(DisabledTag)`.
- `World::tryGetHealth` + `World::tryGetTransform` +
  `World::tryGetFaction`.
- `Engine::events<T>()` warm-at-setup pattern.

**Library gaps surfaced:** **none.** Implementation
stayed entirely within the public API; no core batch
scheduled. (The conservative-expansion policy held.)

**Verification:** both `build/` and `build-werror/` compile
clean on first pass; ctest still 80/80 on both trees;
`rpg_demo` runs validation-clean
(`THREADMAXX_VK_VALIDATE=1`) for 300 ticks on werror.
Entity count post-spawn is 153 (was 152; +1 for the sword).

**Effort:** ~3 hours actual; ~7 new + 5 modified files.

#### 3.11.2 Batch D2 — Multi-camera + frustum culling  ✅ landed 2026-05-16

**Game feature:** Three live cameras share the swapchain:
- **Main third-person** (full-screen, perspective): the
  existing batch-10 camera unchanged.
- **Top-down mini-map** (top-right corner, orthographic):
  the player + every entity rendered from above with a
  fixed 35-unit half-height ortho. Visible every frame.
- **Aim PIP** (center, narrow perspective): a 0.5-rad FOV
  shoulder-mounted camera that appears only while the
  player's sword is mid-swing (`swordSwingTimer > 0`).
  Disappears between attacks.

Items outside the main camera's frustum are culled away;
the mini-map's wide ortho frustum picks up everything in
~70 units around the player; the aim PIP's narrow cone
shows only what the player is swinging at.

**As-shipped 2026-05-16** — one library-side addition,
one renderer-side fix, three demo updates:

**Library (one minimal additive change):**
- **`threadmaxx::Viewport` POD + `Camera::viewport` field**
  (`include/threadmaxx/render/Camera.hpp`). Normalized
  (0..1) `{x, y, width, height}` rect, defaults to full
  screen. Renderers map to pixel coords via the active
  swapchain extent. Conservative-expansion policy held —
  the multi-camera mini-map can't exist in a corner
  without this, so it's a justified extension.

**Renderer (Vulkan):**
- **Per-camera viewport + scissor** in `recordCamera`.
  Reads `cam.viewport` and sets `vkCmdSetViewport`
  + `vkCmdSetScissor` per camera, so each renders into
  its declared rect. Pre-D2 cameras stay full-screen
  (default viewport `{0,0,1,1}`).
- **Pre-packed instance buffer.** Previous design called
  `ensureBuffer` per camera, which destroyed the buffer
  mid-frame when N cameras' instance counts varied — this
  surfaced as a Vulkan validation flood
  (`vkCmdBindVertexBuffers / VkBuffer was destroyed`)
  with 3 cameras active. The fix pre-packs **every
  camera's instance slice** into one contiguous buffer
  in `recordFrame` BEFORE the camera loop, then each
  camera binds with `pOffsets = slice.offsetBytes` and
  draws `slice.instanceCount` rows. One `ensureBuffer`
  call per frame, no destroy-while-bound.

**Demo:**
- **`CameraSystem`** now emits up to three cameras per
  frame in `buildRenderFrame`. Stashes a copy in
  `WorldState::activeCameras` so the cull pass can read
  the same set. The aim PIP is emitted only when
  `PlayerState::swordSwingTimer > 0` (last seen in
  `update`).
- **`CubeRenderSystem`** now builds a parallel
  `(DrawItem, BoundingVolume)` array and calls
  `threadmaxx::cullByFrustum` against `activeCameras`
  before pushing items into the builder. Items with
  `cameraMask == 0` (visible to no camera) are dropped.
- **`WorldState`** gained
  `std::vector<threadmaxx::Camera> activeCameras` plus
  the three `kViewport*` and `kCameraId*` constants.

**Engine subsystems newly exercised:**

- `Camera::viewport` (D2's library addition).
- `RenderFrame::cameras` — array now non-trivially
  populated (2 or 3 cameras vs 1 before).
- `DrawItem::cameraMask` — non-trivial per-item filter
  driven by `cullByFrustum`.
- `extractFrustum` + `cullByFrustum` — the visibility
  helpers shipped in batch 8 but never had a real-game
  consumer in the demo.
- `threadmaxx::ProjectionMode::Orthographic` — first use
  of the ortho path; the mini-map's view matrix is
  hand-constructed for the top-down direction.

**Library gaps surfaced:**
1. `Camera::viewport` (added — conservative-expansion
   tier: blocking the mini-map's corner placement).
2. **Vulkan renderer `ensureBuffer` lifecycle bug** —
   fixed in-place in `examples/vulkan_renderer/`. Pre-D2
   the single-camera path never triggered the bug
   because there was no second camera to invalidate the
   first's binding. No core library change needed.

**Verification:**
- Both `build/` and `build-werror/` compile clean on
  first pass (after the viewport fix).
- **ctest 81/81 on both trees.**
- `rpg_demo` runs **validation-clean for 300 ticks** on
  the `-Werror` tree under `THREADMAXX_VK_VALIDATE=1`
  with all three cameras emitting actively (no
  per-frame Vulkan validation messages).

**Files:** 1 library file
(`include/threadmaxx/render/Camera.hpp`), 1 renderer file
(`examples/vulkan_renderer/src/VulkanRenderer.cpp`), 4
demo files (`DemoTypes.hpp`, `CameraSystem.{cpp,hpp}`,
`CubeRenderSystem.{cpp,hpp}`, `DemoGame.cpp`).

**Effort:** ~2 hours actual.

#### 3.11.3 Batch D3 — Save / load with user components  ✅ landed 2026-05-16

**Game feature:** Three save / load paths:
- **F5** — synchronous quick-save. Captures the world
  snapshot + every user-component value + key world-state
  fields, writes to `/tmp/rpg_demo_save.bin` on the sim
  thread. Blocks the current tick until the file is closed.
- **F8** — asynchronous quick-save (new). Captures user
  components on the sim thread, then uses
  `Engine::snapshotAsync` (batch 20) so the
  `world.snapshot()` call AND the file write happen on the
  engine-owned background thread. Sim thread keeps stepping.
  Stutter-free.
- **F9** — full restore. Reads the save file, queues one
  commit's worth of `cb.destroy` (for every alive entity)
  + `cb.spawnBundle` (into pre-reserved handles, with
  `Parent.parent` re-pointed through the snap-index map)
  + `addUserComponent` per saved blob. Updates
  `WorldState::player` and `sword` to the new handles.

**As-shipped 2026-05-16** — `SaveLoadSystem.{hpp,cpp}`
fully rewritten; new `rpg_save_load_test.cpp` in `tests/`
to gate the round-trip without GLFW.

The on-disk format is `examples/rpg_demo/`-specific and
documented at the top of `SaveLoadSystem.hpp`:

```
[magic 'RPGS']  [version: u32]
[built-in WorldSnapshot via threadmaxx::serialize]
[section count: u32 = 5]
For each user component:
  [name: string]  [stride: u32]
  [entry count: u64]
  [entries: (snap_idx: u32, blob[stride]) ...]
[world state: player_snap_idx u32, sword_snap_idx u32,
              totalKills u32, sunAngle f32]
```

The `snap_idx` field is the entity's position in
`WorldSnapshot.entities` at save time — built on the
worker thread by walking the just-captured snapshot. Reads
emit `EntityHandle{snap_idx, 0}` as the captured handle so
the load path can use `handle.index` as the snap index
directly (no separate map structure).

**Engine subsystems newly exercised:**

- `Engine::snapshotAsync` (batch 20) — F8 path.
- `Engine::reserveEntityHandles` batch form — load
  reserves all N handles in one call so Parent references
  can be populated before commit fires.
- `cb.destroy` of every alive entity in a single
  `single()` callback, followed by `cb.spawnBundle` of the
  rebuilt N entities in the same commit. Submission-order
  semantics guarantee destroys run before spawns.
- Parent handle translation via the snap-index map — proves
  that hierarchy chains survive a save/load round-trip.
- `World::archetypeChunkCount` / `archetypeChunk(i)` +
  `user::chunkSpan<T>` (batch 6b) for the user-component
  capture pass — walks every chunk that carries a
  user-component bit, pairs each row with its
  `EntityHandle`.

**Library gaps surfaced: none.**
User-component serialization stayed entirely in
`SaveLoadSystem.cpp` (per §3.1 batch 6b's "game-side
responsibility" rule). The pattern was repetitive (a
template + 5 calls per user-component type) but not
verbose enough to justify a library-side
`UserComponentSerializer<T>` helper. Conservative-expansion
policy held.

**Test coverage:**
`tests/rpg_save_load_test.cpp` — programmatic round-trip on
a 10-entity scene (player + sword child + 5 hostile NPCs +
3 pickups) with all 5 user-component types attached. Saves
to `/tmp/tmx_rpg_save_test.bin`, reads back, and
hash-compares every user-component vector byte-for-byte
plus the player / sword snap indices, totalKills, and
sunAngle field. Lives in `tests/` (not the rpg_demo target)
because the demo ships as an executable and the test
re-implements the wire format inline rather than linking
the demo's `SaveLoadSystem` directly.

**Verification:**
- Both `build/` and `build-werror/` compile clean on first
  pass.
- **ctest 81/81 on both trees** (was 80; +1 from
  `rpg_save_load_test`).
- `rpg_demo` runs validation-clean for 300 ticks on the
  `-Werror` tree.

**Files:** 1 rewritten (`SaveLoadSystem.{hpp,cpp}`), 1 new
test (`tests/rpg_save_load_test.cpp`), 3 modified
(`Input.{hpp,cpp}` for F8 edge wiring, `DemoGame.cpp` for
the new `engine_` ctor arg, `main.cpp` for the controls
help comment).

**Effort:** ~2 hours actual.

#### 3.11.4 Batch D4 — Quest system + scripted scenarios  ✅ landed 2026-05-16

**Game feature:** Two persistent quests — **"Collect 25
pickups"** and **"Defeat all hostile NPCs"** — visible in
the HUD. Quest progress advances via event subscriptions
(no per-tick polling); the HUD prints a one-liner whenever
a quest advances, plus a `[hud] quests: N/2 complete`
summary every 60 ticks. Hostile quest target is sized at
spawn time from the actual NPC roll, not a hardcoded
constant.

The "scripted scenarios" half ships as the determinism test
bed in `tests/rpg_demo/test_determinism.cpp` — runs the
demo twice with an identical fixed input script and asserts
per-tick `commitHash` equality across runs. This is the
regression test bed the spec called for; the demo CLI
`--replay <path>` mode is **not** shipped (would require a
file-backed input log; per the conservative-expansion
policy that's deferred until a real demand emerges).

**As-shipped 2026-05-16:**

- **`QuestState` + `QuestId` + `QuestProgressed`** types in
  `DemoTypes.hpp`. `WorldState::quests` is a `std::vector`
  seeded by `DemoGame::onSetup` with two entries.
- **`QuestSystem.{cpp,hpp}`** — subscribes to
  `PickupCollected` and `EntityDied` via `subscribeScoped`;
  the callbacks update the matching quest's `progress` and
  emit `QuestProgressed` on advance / completion. Pure
  event-driven, no per-tick `update` body.
- **`DemoGame::onSetup`** seeds `quests` + counts hostile
  NPCs into `WorldState::hostileSpawnCount` during the
  randomized NPC spawn loop. The kill-quest target =
  hostileSpawnCount (typically ~30 in vanilla, ~6000 in
  stress mode).
- **`HudSystem`** subscribes to `QuestProgressed` with a
  fourth `Subscription` member (`questSub_`) and prints
  `[quest] <name> — N/T  ✓ COMPLETE` on advance. The
  60-tick periodic summary line prints `[hud] quests:
  N/M complete`.

**Engine subsystems newly exercised in the demo:**

- A 4th typed event channel (`QuestProgressed`) demonstrates
  the standard "event-driven game logic via
  `subscribeScoped`" pattern.
- `EngineStats::commitHash` — surfaced via the new
  determinism test, confirms run-vs-run reproducibility.
- `Config::deterministic = true` flag.

**Library gaps surfaced: none.** The whole feature stayed
within the public API. The original spec called for
`Engine::setSkipPolicy(Scripted) + pushScriptedSkip` for
replay — but those exist in the library since batch 12 and
are documented as the **server→client replay channel** for
networked games. For the demo's regression-test use case,
the simpler "deterministic re-run with identical inputs"
pattern is sufficient and exercises `commitHash` directly.

**Items deferred** (matching the spec's "expected gaps"):

- `Engine::serializeSkipLog` / `deserializeSkipLog`
  helpers. Not shipped — no demo CLI path uses them yet.
  The in-process determinism test pattern in
  `test_determinism.cpp` is the recommended user-side
  recipe; if a real demand emerges, schedule a §3.10.3
  follow-on.
- `--replay <path>` CLI mode on the demo. Would require a
  binary input-log format + file I/O. Not shipped.
- `Engine::setTimeScale` "4x replay" — the engine API
  already exists since batch 4; the demo just doesn't
  bind it to a key. A one-line wiring could be added
  later when the replay CLI lands.

**Test coverage:**

- `tests/rpg_demo/test_quests.cpp` — spawns 30 extra
  pickups under the player so PickupSystem rapidly fires
  `PickupCollected`. Asserts the pickup-quest advances to
  `25/25` and the `completed` flag flips within 30 ticks.
- `tests/rpg_demo/test_determinism.cpp` — runs the
  identical 5-attack scripted input across the default
  scene for 60 ticks twice. Asserts per-tick
  `commitHash` is byte-identical between runs AND the
  final hash is non-trivial (not the FNV-1a basis,
  meaning real commits happened). First-tick mismatch
  surfaces any hidden non-determinism (unseeded RNG,
  `std::time`-dependent path, etc.).

**Verification:**
- Both `build/` and `build-werror/` clean.
- **ctest 88/88 on both trees** (was 86; +2 from
  `test_quests` + `test_determinism`).
- `rpg_demo` runs **validation-clean for 300 ticks** under
  `THREADMAXX_VK_VALIDATE=1` on `-Werror`, with the new
  quest summary line printed every 60 ticks.

**Files:** 5 modified
(`DemoTypes.hpp`, `DemoGame.cpp`, `HudSystem.{cpp,hpp}`,
`CMakeLists.txt`), 2 new system files
(`QuestSystem.{cpp,hpp}`), 2 new tests
(`tests/rpg_demo/test_quests.cpp`,
`tests/rpg_demo/test_determinism.cpp`).

**Effort:** ~1.5 hours actual.

#### 3.11.5 Batch D5 — Scale stress + tick budget HUD  ✅ landed 2026-05-16

**Game feature:** `--stress` (or `-s`) CLI flag scales the
demo to **10,000 NPCs + 50,000 pickups** (60,003 entities
total) AND turns on the engine's tick-budget skip policy.
At this scale the per-tick cost easily blows the
16.67 ms / 60 Hz budget; the engine drops cosmetic
systems (`DebugOverlaySystem`, `DayNightSystem`,
`HudSystem`-update) so the gameplay loop stays live. The
HUD shows live skip counts and the cumulative
`BudgetExceeded` count.

**As-shipped 2026-05-16:**

- **Stress-mode CLI**. `main.cpp` parses `--stress` / `-s`
  and sets `WorldState::stressMode`. The vanilla 153-entity
  scene is unchanged (the new code paths are no-ops when
  the flag is off).
- **`DemoGame::onSetup` configures scale + budget**. When
  `stressMode == true`: spawn counts become `kStressNpcCount`
  (10,000) + `kStressPickupCount` (50,000),
  `engine.setTickBudget(1/60)` + `setSkipPolicy(Budget)` are
  applied, and `threadmaxx::FrameBudgetWatcher` is
  registered so `BudgetExceeded` events emit. Channels
  pre-warmed on the sim thread per the "warm channels at
  setup" rule.
- **`skippable() = true`** on `DebugOverlaySystem` and
  `DayNightSystem` (the HUD was already skippable from
  batch 10). The engine emits `SystemSkipped{tick,
  systemName, reason}` for each dropped wave slot.
- **`HudSystem` subscriptions**. Three new `Subscription`
  members + corresponding callbacks accumulate
  `WorldState::totalSkippedHud`,
  `totalSkippedOverlay`, `totalSkippedDayNight` and
  `budgetExceededCount`. The HUD postStep line now reads
  `[hud] tick=N entities=M pickups=P kills=K sun=S
  OVER=X skips[hud=h,ovr=o,dn=d]` when in stress mode.
- **NPC brain early-bail**. `NPCBrainSystem::update` polls
  `ctx.shouldYield()` every 512 entities; if the engine's
  `overBudget_` flag is set, the loop drops the unwalked
  NPC slice and emits a one-line diagnostic. Unprocessed
  NPCs keep their previous-tick velocity (visually
  stutter-free thanks to the engine's interpolated render
  frame).

**Engine subsystems newly exercised in the demo:**

- `Engine::setTickBudget` + `setSkipPolicy(Budget)`.
- `ISystem::skippable()` true on three cosmetic systems.
- `EventChannel<SystemSkipped>` drain into HUD.
- `EventChannel<BudgetExceeded>` drain into HUD.
- `threadmaxx::FrameBudgetWatcher` (built-in system).
- `SystemContext::shouldYield()` inside a serial loop.

**Library gaps surfaced: none.** All required hooks were
shipped by batch 12 (cancellation, budgets, priorities) and
batch 14 (telemetry ingestion). The conservative-expansion
policy held: `--stress` mode worked on the existing public
API without library changes. Documented potential follow-on
in §3.11 (lazy-rebuild `SpatialHash`) **not pursued** —
spatial hash rebuild at 60k entities is ~3 ms / tick on the
dev machine; not the dominant cost. The dominant cost at
this scale is the stitched-view rebuild + per-tick chunk
walks in cosmetic systems, both of which the skip policy
correctly drops.

**Items deferred:**

- `JobPriority::High` for combat + brain;
  `JobPriority::Low` for cosmetic. The current brain is
  serial (`ctx.single`), so JobPriority doesn't apply. A
  parallel brain (which would benefit from `JobPriority`)
  is a separate batch.
- `MaskCache` + `forEachWithCached` integration. The
  existing combat / brain paths don't iterate by mask;
  they iterate by chunk (NPCBrain) or by spatial-hash
  result (Combat). `MaskCache` doesn't apply directly.

**Test coverage:** new `tests/rpg_demo/test_skip_policy.cpp`
verifies that `setTickBudget` + `SkipPolicy::Budget` +
`FrameBudgetWatcher` together produce non-zero
`budgetExceededCount` AND at least one cosmetic-system skip
across 30 ticks. To avoid spawning 60k entities in the test
(slow under ctest's parallel scheduler), the test sets a
100 ns budget — small enough that a 153-entity tick blows it
every time.

**Measured at stress scale** (dev machine, 4 workers):
- 60,003 entities, 180-tick run.
- `OVER=119` over-budget alerts on tick 120 (~100% of
  ticks blowing the 16.67 ms budget).
- `skips[ovr=120]` — `DebugOverlaySystem` skipped on
  every blown tick.
- `skips[hud=0]` — HudSystem's `postStep` runs (post-step
  hooks are never skipped per the §6 phase-4 contract);
  the update is no-op so skipping it changes nothing.
- `skips[dn=0]` — `DayNightSystem` runs `postStep` only
  (also never skipped); its `update` is a no-op.
- Player movement + combat events + pickup events still
  flow normally; the gameplay loop is fully live.

**Verification:**
- Both `build/` and `build-werror/` clean.
- **ctest 86/86 on both trees** (was 85; +1 from
  `test_skip_policy`).
- `rpg_demo` runs **validation-clean for 180 ticks** under
  `THREADMAXX_VK_VALIDATE=1` with `--stress` on the
  `-Werror` tree.

**Files:** 7 modified
(`DemoTypes.hpp`, `DemoGame.cpp`, `NPCBrainSystem.cpp`,
`HudSystem.{cpp,hpp}`, `DebugOverlaySystem.hpp`,
`DayNightSystem.hpp`, `main.cpp`), 1 new test
(`tests/rpg_demo/test_skip_policy.cpp`).

**Effort:** ~2 hours actual.

#### 3.11.6 Batch D6 — Animations + skinning  ✅ landed 2026-05-16 (procedural path)

**As-shipped 2026-05-16** — procedural Y-bob path. Every
moving entity visibly "walks" via
`Transform.position.y = baseY + sin(time * freq + phase) *
amp * speedRatio`. The player bobs subtly while moving
(`freq=8, amp=0.10`); NPCs bob more prominently
(`freq=5-7, amp=0.20`) with per-NPC phase offsets so a
group doesn't move in lockstep. Stationary entities sit
still — the bob fades out as velocity goes to zero.

Files:
- **`AnimState` user component** (`DemoTypes.hpp`) — POD
  with `baseY`, `phase`, `frequency`, `amplitude`. The
  6th user component registered by `DemoGame::onSetup`.
- **`AnimationSystem.{cpp,hpp}`** — reads `(Transform,
  Velocity, AnimState)` chunks; writes modulated Y via a
  batched `cb.setTransform` call in `ctx.single()`. Skips
  rows whose Y didn't change. Uses `tick() * dt()` for
  deterministic simulation time.
- **Wave ordering** — registered AFTER `MovementSystem`
  (so X/Z integration is done) and BEFORE
  `HierarchySystem` (so the player's sword inherits the
  bobbed Y via the Parent chain).
- **`DemoGame::onSetup` attaches `AnimState`** to player +
  every NPC. Per-NPC `phase = i * 0.31` keeps them out of
  sync.
- **`SaveLoadSystem` round-trips `AnimState`.** Section
  count bumped to 6 (added `"AnimState"`); pre-D6 save
  files are no longer readable (by design — game-specific
  internal format, no per-section version negotiation).

**Engine subsystems newly exercised in the demo:**

- 6th user-component registration via
  `Engine::registerUserComponent<AnimState>` —
  demonstrates the user-component scaling pattern.
- Typed `user::chunkSpan<AnimState>` chunk-walking inside
  `AnimationSystem`.
- `SystemContext::tick() * dt()` deterministic
  simulation-time pattern.

**Library gaps surfaced: none.** Procedural path stayed
entirely within the public API.

**Items deferred (full Vulkan skinning pipeline):**

The spec's "full skinned-mesh playback" path — the engine
slots `AnimationStateRef` / `AnimationPoseRef`,
`RenderInstance::skeleton` / `pose` upload via
`UploadRing`, vertex shader bone-weighted skinning —
is **deferred to a future renderer-side core batch
(tentatively `batch 9b — skinning pipeline`)**. Reasons:

1. The demo's unit-cube renderer wouldn't visibly
   benefit from real bone-weighted skinning. Cubes
   don't have bones.
2. The Vulkan renderer (batch 9) was always shipped as
   v1 with skinning deferred. Full skinning is a
   1-2 week renderer rewrite (descriptor sets, pose
   buffer ring, bone-weight vertex attributes, skinning
   pipeline, glTF skeleton import).
3. Procedural Y-bob is the right gameplay-side payoff
   for D6 — visible NPCs walking convincingly without
   dragging in renderer scope. Conservative-expansion
   policy held.

**Test coverage:** `tests/rpg_demo/test_animation.cpp` —
spawns one NPC with velocity = (2, 0, 0) and `AnimState`
defaults (baseY=1.0, freq=8, amp=0.20). Runs 90 ticks
(≈ 1.5 s sim time, ~2 full cycles). Asserts Y range
covers above AND below baseY and oscillation amplitude
is meaningful (`maxY - minY > 0.05`). Measured
`Y range = [0.900, 1.100]` — exactly
`baseY ± amp × speed_ratio` (0.20 × 0.50 = 0.10).

**Verification:**
- Both `build/` and `build-werror/` clean.
- **ctest 89/89 on both trees** (was 88; +1 from
  `test_animation`).
- `rpg_demo` runs **validation-clean for 300 ticks**
  under `THREADMAXX_VK_VALIDATE=1` on `-Werror`. NPCs
  visibly bob in motion.

**Files:** 4 modified (`DemoTypes.hpp`, `DemoGame.cpp`,
`SaveLoadSystem.{cpp,hpp}`, two `CMakeLists.txt`), 2 new
system files (`AnimationSystem.{cpp,hpp}`), 1 new test
(`tests/rpg_demo/test_animation.cpp`).

**Effort:** ~1 hour actual for the procedural path.
Full skinning pipeline would be a separate ~2-week batch.

---

**Original D6 spec (for reference):** NPCs visibly "walk"
via a procedural pose (simple sine-wave bob, parameterized
by velocity magnitude). The player has a swing animation
when attacking. Animation state survives save/load.

**Engine subsystems exercised:**

- `AnimationStateRef` + `AnimationPoseRef` engine slots
  (currently unused).
- `RenderInstance::skeleton` / `pose` — Vulkan renderer
  uploads pose data to the GPU.
- `UploadRing` (batch 8) for pose buffer streaming.
- `Engine::registerUserComponent<AnimState>` for the
  per-entity animation parameters.

**Expected library gaps:** the Vulkan renderer doesn't
currently support skinning (batch 9 deferred it). This
batch likely requires a real renderer-side addition:
descriptor set for pose buffer, vertex shader skinning math,
animation upload pipeline. Could become a `batch 9b — skinning
pipeline` core batch.

**Effort:** ~3 days (split: 1 day demo, 2 days renderer
skinning).

#### 3.11.7 Batch D7 — Real assets + hot reload  ✅ landed 2026-05-16 (procedural path)

**As-shipped 2026-05-16** — procedural path that exercises
the engine's `IResourceLoader` + `preloadUntil` + `LoaderStats`
+ `AssetReloaded` event surface without dragging in
renderer-side OBJ parsing / shader-file I/O (deferred to
`batch 9b`, the same future renderer-side batch as full
skinning).

**Files:**

- **`PreloadLoader.{cpp,hpp}`** — a demo-side
  `IResourceLoader` impl. Seeds 64 "fake assets" at
  construction; each `update()` moves 4 from `pendingLoads`
  → `ready`. Reports proper `LoaderStats` (pending /
  inFlight / ready / memoryFootprint / memoryBudget). The
  loader takes ~16 ticks to drain → simulates a typical
  multi-tick boot-time load. Validates the engine's
  `addResourceLoader` ownership transfer.
- **`DemoGame::onSetup` boot-time `preloadUntil`** — after
  `addResourceLoader(PreloadLoader)`, calls
  `engine.preloadUntil([&]{ return loader->allDone(); },
  5s)`. Pumps the loader's `update()` without advancing
  the simulation. Logs final
  `aggregateLoaderStats` + elapsed ms.
- **F12 input edge + `kEdgeReloadShader`** — `HudSystem::
  preStep` consumes the edge and emits a synthetic
  `AssetReloaded` event on the engine's typed channel.
  Real renderer-side pipeline rebuild on this event is
  deferred to `batch 9b`.
- **HudSystem `reloadSub_` subscription** — prints
  `[asset] reloaded: <old> → <new>` per event. Verifies
  the demo-side subscriber side of the hot-reload pattern
  actually fires.
- **HUD asset-stats line** — periodic 60-tick output now
  includes `[hud] assets: pending=P inFlight=I ready=R
  mem=M MiB`. Aggregates across **every** registered
  loader (the renderer's Mesh / Texture / Shader loaders
  + the new PreloadLoader).

**Engine subsystems newly exercised in the demo:**

- `Engine::addResourceLoader` ownership transfer.
- `IResourceLoader::update` per-tick pump on the sim
  thread, after the last postStep commits.
- `IResourceLoader::stats()` → `Engine::aggregateLoaderStats()`
  aggregation across all registered loaders.
- `IResourceLoader::onShutdown` reverse-registration-order
  teardown contract (validated at engine destruction).
- `Engine::preloadUntil(done, timeout)` blocking yield
  loop — drives the loader's `update()` without ticking
  the simulation, falls back to timeout after 5s.
- `EventChannel<AssetReloaded>` subscriber side (the
  hot-reload event flow).
- `Engine::events<AssetReloaded>().emit(...)` direct emit
  from game code (substitute for the renderer-side
  `ShaderLoader::markStale` path that ships next).

**Library gaps surfaced: none.** The procedural path
stayed entirely within the public API. Full renderer-side
hot-reload + file I/O is deferred — same
conservative-expansion call as batch D6.

**Items deferred (renderer-side / `batch 9b` placeholder):**

1. Real `.obj` mesh loading. The renderer's `MeshLoader`
   has a `createUnitCube` fallback; real OBJ parsing
   would be a sibling-library addition (~1 day).
2. Real shader file I/O. The renderer's `ShaderLoader`
   currently ships embedded SPIR-V; loading from disk +
   `glslc` runtime invocation is a ~2-day rework.
3. Renderer-side `AssetReloaded` subscriber that rebuilds
   pipelines on the actual reload event. The shader
   loader's `markStale` already emits the event; the
   missing piece is `VulkanRenderer` re-creating the
   relevant `VkPipeline` when its tracked id matches.
4. `ResourceHandle<Mesh>` refcount demo from game code.
   The renderer already uses refcount internally; the
   demo doesn't directly hold mesh ids.

**Test coverage:**

- **`tests/rpg_demo/test_preload.cpp`** — verifies the
  `preloadUntil` call inside `DemoGame::onSetup` drains
  the queue: post-init `aggregateLoaderStats` reports
  `pending=0 inFlight=0 ready=64 mem≥4MiB`. **PASS**.
- **`tests/rpg_demo/test_asset_reload.cpp`** — installs
  a local `AssetReloaded` subscriber, injects 3 F12
  edges across 3 ticks, asserts 3 deliveries. Drains
  one extra tick to let the last emit propagate.
  **PASS** (3 deliveries).

**Verification:**
- Both `build/` and `build-werror/` clean.
- **ctest 91/91 on both trees** (was 89; +2 from
  `test_preload` + `test_asset_reload`).
- `rpg_demo` runs **validation-clean for 180 ticks**
  under `THREADMAXX_VK_VALIDATE=1` on `-Werror`. Boot
  output reads `[preload] done=1 ready=64 pending=0
  memMiB=4.0 elapsed=0ms`; periodic HUD shows
  `[hud] assets: pending=0 inFlight=0 ready=64 mem=4.0
  MiB`.

**Files:** 7 modified
(`DemoTypes.hpp` /
`Input.{hpp,cpp}` /
`DemoGame.cpp` /
`HudSystem.{cpp,hpp}` /
demo `CMakeLists.txt`,
test `CMakeLists.txt`),
2 new system files
(`PreloadLoader.{cpp,hpp}`), 2 new tests
(`tests/rpg_demo/test_preload.cpp`,
`tests/rpg_demo/test_asset_reload.cpp`).

**Effort:** ~1 hour actual for the procedural path. The
deferred full-asset / renderer-side rework remains a
1–2 week future batch.

---

#### 3.11.7b Batch 9b — Real assets + renderer-side hot reload (split)

Batch 9b carries the four items D7 deferred. Originally
scoped as one ~2-week renderer rework; split into three
shippable sub-batches so each lands independently:

- **9b.1 — OBJ mesh parser (sibling-library).** Pure CPU
  parser in `examples/rpg_demo/ObjLoader.{hpp,cpp}`. No
  Vulkan / engine deps. Standalone unit test in
  `tests/rpg_demo/test_obj_loader.cpp`. Renderer
  integration (multi-mesh dispatch) deferred to 9b.2 —
  the current `VulkanRenderer::recordFrame` assumes a
  single hardcoded `cubeHandle`.  ✅ **landed 2026-05-17**
  (see as-shipped below).
- **9b.2a — OBJ→GPU upload path.** Generic
  `MeshLoader::createMesh(engine, vertices, indices)` +
  `VulkanRenderer::setDefaultMeshFromData` setter. Demo
  loads `examples/rpg_demo/assets/cube.obj` at startup,
  replacing the procedural cube. Single-mesh dispatch
  preserved.  ✅ **landed 2026-05-17** (see below).
- **9b.2b — Multi-mesh dispatch.** Per-meshId draw loop +
  renderer slot table + `CubeRender::meshId` + pyramid
  asset for pickups.  ✅ **landed 2026-05-17**
  (see §3.11.7b.3 below).
- **9b.3 — Shader file I/O + hot reload.** Pipeline shaders
  now register with `ShaderLoader` at create time; F12 →
  renderer's `reloadShaders` → `markResourceStale<Shader>`
  → loader re-reads `.spv` → `AssetReloaded` →
  renderer subscriber rebuilds the affected `VkPipeline`.
  ✅ **landed 2026-05-17** (see §3.11.7b.4 below).
- **9b.4 — Vulkan skinning pipeline.** Bone-weight
  vertex attributes, pose buffer ring, descriptor sets,
  vertex shader skinning math, glTF skeleton import.
  Heaviest sub-batch (~1-2 weeks per the original D6
  spec). Procedural Y-bob from D6 remains the fallback
  when no skeleton is bound. See §3.11.7b.5 for the
  three-stage breakdown deferring full implementation
  to a focused future batch.

#### 3.11.7b.1 Batch 9b.1 — OBJ parser  ✅ landed 2026-05-17

**Files:**

- **`examples/rpg_demo/ObjLoader.hpp`** — public API:
  `MeshData` POD (positions + normals flat-packed at the
  opaque pipeline's 24-byte stride; 16-bit indices),
  `ObjParseResult`, free functions `parseObj(string_view)`
  and `parseObjFile(string_view path)`.
- **`examples/rpg_demo/ObjLoader.cpp`** — single-pass
  parser. Supports `v`, `vn`, `vt` (parsed-then-ignored),
  `f a/b/c d/e/f g/h/i [...]` with `a`/`a//c`/`a/b`
  forms. N-gon faces fan-split into triangles. Missing
  normals default to (0,1,0). Malformed value lines
  silently skipped; structural failures (16-bit index
  overflow, no faces) surface as `ok=false`.
- **`examples/rpg_demo/CMakeLists.txt`** — `ObjLoader.cpp`
  added to `rpg_demo_core` sources.
- **`tests/rpg_demo/test_obj_loader.cpp`** — 7 cases:
  single triangle, hand-rolled cube (8v/6vn/6 quads →
  36 corners), pentagon (5-gon → 9 corners), missing-normal
  fallback, malformed-line tolerance (`#`/`o`/`mtllib`/
  `usemtl`/`g`/`s` + bad-float `v`), empty input, no-faces
  input. **PASS**.
- **`tests/rpg_demo/CMakeLists.txt`** — `test_obj_loader`
  registered.

**Library gaps surfaced: none.** Parser is a sibling-
library addition per §3.3 — no engine API changes.

**Items still deferred to 9b.2 / 9b.3:**

1. `MeshLoader::createFromObj` renderer-side upload.
2. Multi-mesh dispatch in `VulkanRenderer::recordFrame`.
3. Real shader file I/O + runtime `glslc`.
4. Renderer-side `AssetReloaded` pipeline rebuild.
5. Full bone-weighted skinning pipeline.

**Verification:**
- Both `build/` and `build-werror/` clean.
- **ctest 94/94 on both trees** (was 93; +1 from
  `test_obj_loader`).
- rpg_demo executable still builds + runs validation-clean
  (parser is unwired into the renderer path).

**Files:** 2 new (`ObjLoader.{hpp,cpp}`), 1 new test
(`test_obj_loader.cpp`), 2 modified CMakeLists.

**Effort:** ~1 hour actual.

#### 3.11.7b.2 Batch 9b.2a — OBJ→GPU upload path  ✅ landed 2026-05-17

Plumbs the 9b.1 parser into the Vulkan renderer. The
renderer's hardcoded procedural unit-cube (the
`kCubeVertices` / `kCubeIndices` arrays in
`MeshLoader.cpp`) is now replaced at startup by a real
`.obj` asset loaded from disk. No multi-mesh dispatch
yet — the single `cubeHandle` is swapped for a different
single mesh; per-entity-class meshIds remain 9b.2b's
scope.

**Files:**

- **`examples/vulkan_renderer/src/MeshLoader.{hpp,cpp}`** —
  added `createMesh(engine, vertices, indices)` (generic
  upload). `createUnitCube` refactored to delegate to it,
  collapsing two upload paths to one. New method asserts
  the input matches the opaque pipeline's vertex layout
  (`vertices.size() % 6 == 0`, `indices.size() % 3 == 0`)
  and returns an invalid handle on violation. `<span>`
  added to the header.
- **`examples/vulkan_renderer/include/threadmaxx_vk/VulkanRenderer.hpp`** —
  added `setDefaultMesh(ResourceHandle<Mesh>)` and
  `setDefaultMeshFromData(std::span<const float>,
  std::span<const std::uint16_t>)`. The latter uses the
  internal `MeshLoader` so callers don't need access to
  the private loader API. `<span>`, `Resource.hpp`,
  `Mesh.hpp` added to the public header.
- **`examples/vulkan_renderer/src/VulkanRenderer.cpp`** —
  implementations. `setDefaultMesh` move-assigns into
  `impl_->cubeHandle`; the prior handle's refcount drops
  via `ResourceHandle::~ResourceHandle` and the slot
  frees if nothing else holds it.
- **`examples/rpg_demo/assets/cube.obj`** — sample asset
  exercised at startup. 8 v + 6 vn + 6 quads = 12
  triangles = 36 corners after fan-split. Same shape the
  procedural cube produces, byte-for-byte.
- **`examples/rpg_demo/CMakeLists.txt`** — added
  `RPG_DEMO_SOURCE_DIR` compile def (PUBLIC on
  `rpg_demo_core`) so `main.cpp` can resolve
  `assets/cube.obj` regardless of CWD.
- **`examples/rpg_demo/main.cpp`** — after
  `engine.initialize(game)` returns, calls
  `rpg::parseObjFile` on `assets/cube.obj` and feeds the
  result through `renderer->setDefaultMeshFromData`.
  Failure path logs the reason and falls back silently
  to the procedural cube (the renderer's
  `initialize()`-time `createUnitCube` already ran).

**Engine subsystems newly exercised in the demo:**

- The end-to-end `parseObj` → `MeshLoader::createMesh`
  → `engine.resources().addRefCounted<Mesh>` →
  `VulkanRenderer::setDefaultMesh` pipeline, top to
  bottom.
- `ResourceHandle<Mesh>` move-assign and the slot-free
  side-effect on the previous default mesh.

**Library gaps surfaced: none.** All additions are
public API on the example renderer; the core threadmaxx
library is unchanged.

**Items still deferred to 9b.2b / 9b.3 / 9b.4:**

1. Multi-mesh dispatch in `VulkanRenderer::recordCamera`
   (one bind+draw per unique meshId per camera slice).
2. `CubeRender::meshId` field + demo assigns different
   shapes to different entity classes.
3. Real shader file I/O + runtime `glslc`.
4. Renderer-side `AssetReloaded` pipeline rebuild.
5. Full bone-weighted skinning pipeline.

**Test coverage:** the parser is unit-tested in 9b.1
(`tests/rpg_demo/test_obj_loader.cpp`); the upload +
default-mesh swap is exercised by the rpg_demo running
validation-clean. No new headless test is added — a
Vulkan-aware harness for the upload path would require a
real device + validation layers in CTest, which the
project hasn't adopted.

**Verification:**
- Both `build/` and `build-werror/` clean.
- **ctest 94/94 on both trees** (unchanged from 9b.1 —
  no new tests; correctness gate for this sub-batch is
  the rpg_demo running validation-clean and printing
  `[rpg_demo] obj asset: …/assets/cube.obj corners=36
  installed=1`).

**Files:** 1 new (`assets/cube.obj`), 5 modified
(`MeshLoader.{hpp,cpp}`, `VulkanRenderer.hpp`,
`VulkanRenderer.cpp`, `rpg_demo/CMakeLists.txt`,
`rpg_demo/main.cpp`).

**Effort:** ~1 hour actual.

#### 3.11.7b.3 Batch 9b.2b — Multi-mesh dispatch  ✅ landed 2026-05-17

Closes the multi-mesh half of batch 9b.2. The renderer
now dispatches per-meshId: each camera's pre-packed
instance slice is sub-divided into per-mesh buckets, and
the draw loop binds the matching mesh + draws each
bucket separately. The demo loads a second OBJ asset
(`assets/pyramid.obj`), registers it, and assigns its
meshId to every pickup spawn. Default-cube paths (every
non-pickup entity) are unaffected: a single-bucket
slice collapses to the same single-bind + single-draw
path as pre-9b.2b.

**Files:**

- **`examples/vulkan_renderer/include/threadmaxx_vk/VulkanRenderer.hpp`** —
  added `registerMesh(handle) -> int32_t` and
  `registerMeshFromData(spans) -> int32_t`. Both return
  `-1` on failure / null input. Slot 0 is the default
  mesh (set via `setDefaultMesh*`); registered slots are
  1..N.
- **`examples/vulkan_renderer/src/VulkanRenderer.cpp`** —
  added `Impl::meshSlots` vector, `Impl::lookupMesh`
  helper, `Impl::PerFrame::scratchMeshIds` /
  `scratchBuckets` (per-frame allocation-stable buckets
  for the per-camera pack loop). Replaced
  `Impl::CameraSlice::{offsetBytes, instanceCount}` with
  `std::vector<MeshGroup>`. Refactored `recordFrame`'s
  pack loop: per camera, instances are bucketed by
  meshId then concatenated into `packed` in insertion
  order, with each bucket's `(meshId, offsetBytes,
  count)` recorded as a `MeshGroup`. Refactored
  `recordCamera`'s opaque pass: pipeline + push
  constants bound once, then one
  `vkCmdBindVertexBuffers` + `vkCmdBindIndexBuffer` +
  `vkCmdDrawIndexed` per `MeshGroup`. `shutdown` clears
  `meshSlots` before the loader's `releaseGpuResources`
  runs.
- **`examples/rpg_demo/DemoTypes.hpp`** — `CubeRender`
  gained `int32_t meshId = 0` (replaces one slot of the
  trailing pad array). `WorldState` gained `int32_t
  pickupMeshId = 0`.
- **`examples/rpg_demo/SaveLoadSystem.cpp`** —
  `kRpgSaveVersion` bumped from 1 → 2 with the
  `CubeRender` layout change. The per-section stride
  check would already reject pre-9b.2b saves;
  the bump gives a clearer up-front version mismatch.
- **`examples/rpg_demo/CubeRenderSystem.{hpp,cpp}`** —
  `Snapshot` carries `meshId`; `update` reads it from
  `CubeRender::meshId`; `buildRenderFrame` writes it
  into `DrawItem::meshId`.
- **`examples/rpg_demo/DemoGame.{hpp,cpp}`** — added
  `RegisterMeshFn` callback type + `setRegisterMeshFn`
  setter. `onSetup` parses
  `assets/pyramid.obj` and registers via the callback;
  stashes the returned meshId in
  `WorldState::pickupMeshId`. Headless tests leave the
  callback null and pickups fall back to meshId 0 (the
  default cube).
- **`examples/rpg_demo/RespawnSystem.cpp`** —
  killed-NPC drops now also use
  `worldState_->pickupMeshId` so they match the
  floor-spawn pickup shape.
- **`examples/rpg_demo/main.cpp`** — installs the
  callback that forwards to `renderer->registerMeshFromData`
  before `engine.initialize(game)`.
- **`examples/rpg_demo/assets/pyramid.obj`** — new
  asset. 5 v + 6 vn + 6 faces (4 side tris + 2 base
  tris) → 18 corners.
- 4 modified test files (`tests/rpg_demo/test_animation`,
  `test_pickup`, `test_quests`) — dropped trailing
  `{0,0,0}` pad initializer from `CubeRender{}`
  literals (the trailing fields now default-init
  correctly to zero).

**Renderer determinism contract:** bucket iteration is
insertion order (first-seen meshId per camera).
Auto-instances (`frame.instances`) lane is walked first,
then opaque DrawItems, so meshId 0 always lands first
when present. This matches pre-9b.2b draw ordering for
any scene where every instance has meshId == 0.

**Engine subsystems newly exercised in the demo:**

- The multi-mesh `meshId` path through
  `RenderInstance` / `DrawItem` / `InstanceLayoutEntry`,
  end to end.
- `ResourceHandle<Mesh>` for non-default registered
  slots; the renderer's slot table holds them by-value,
  so their refcounts persist for the renderer's lifetime.

**Library gaps surfaced: none.** The
`ResourceHandle<Mesh>::valid()` precondition on
`registerMesh` is the only assertion the renderer
makes; all other behavior plumbs through public engine
APIs.

**Items still deferred to 9b.3 / 9b.4:**

1. Real shader file I/O + runtime `glslc`.
2. Renderer-side `AssetReloaded` pipeline rebuild.
3. Full bone-weighted skinning pipeline.

**Test coverage:** the parser tests from 9b.1 carry
forward unchanged. The multi-mesh draw path is
exercised by the rpg_demo running validation-clean
(pickups visibly render as pyramids instead of cubes
when the pyramid load succeeds). The headless tests
keep the callback null so the demo systems all see
`pickupMeshId == 0` — the same `meshId == 0` →
default-cube path that pre-9b.2b code took.

**Verification:**
- Both `build/` and `build-werror/` clean.
- **ctest 94/94 on both trees** (unchanged from 9b.2a).
  test_round_trip exercises the bumped
  `kRpgSaveVersion`; the headless demo init runs
  with the registration callback null and pickups
  spawn with meshId=0.

**Files:** 1 new (`assets/pyramid.obj`), 12 modified
(VulkanRenderer h+cpp; MeshLoader untouched this round;
DemoTypes, DemoGame h+cpp, CubeRenderSystem h+cpp,
SaveLoadSystem, RespawnSystem, main.cpp, three test
files: test_animation, test_pickup, test_quests).

**Effort:** ~2 hours actual.

#### 3.11.7b.4 Batch 9b.3 — Shader file I/O + hot reload  ✅ landed 2026-05-17

Closes the shader-side of the original D7 deferred list.
The renderer's pipeline shaders are now registered with
the `ShaderLoader` at create time, with both their
embedded SPIR-V (initial value) and on-disk `.spv` path
(re-read on hot reload). The renderer subscribes to the
engine's `AssetReloaded` event channel; when a Shader-
typed reload fires for one of the six tracked shaders,
the renderer rebuilds the affected `VkPipeline` in
place. F12 in rpg_demo triggers the full chain via a
new `VulkanRenderer::reloadShaders` public method.

**Files:**

- **`examples/vulkan_renderer/CMakeLists.txt`** — adds
  `THREADMAXX_VK_SHADER_DIR` compile def pointing at
  `${VK_GENERATED_DIR}` (the per-build `.spv` output
  directory). PRIVATE on the renderer target.
- **`examples/vulkan_renderer/src/VulkanPipelines.{hpp,cpp}`** —
  major refactor:
  - `PipelineShaderSlot` enum (6 stages: opaque
    vert/frag, debug-line vert/frag, debug-point
    vert/frag).
  - `create(ctx, colorFormat, depthFormat, shaderLoader, engine)`
    — takes the loader + engine, registers each shader
    via `shaderLoader.add(engine, shaderPathFor(slot),
    embeddedSpvFor(slot))`, stores per-stage
    `ResourceHandle<Shader>`.
  - Per-pipeline build helpers (`buildOpaquePipeline`,
    `buildDebugLinePipeline`, `buildDebugPointPipeline`)
    — pure functions of VkShaderModule + format state.
    Used by both initial create and the rebuild paths.
  - `rebuildIfMatches(ctx, engine, oldId, newId)` —
    identifies which stage owned `oldId`, acquires the
    new handle via `engine.resources().acquire<Shader>(newId)`,
    calls `vkDeviceWaitIdle`, and rebuilds the
    affected pipeline using fresh SPIR-V from the
    registry. Pipeline layouts persist.
  - `recreatePipelines(ctx, colorFormat, depthFormat, engine)` —
    swapchain-recreate path; rebuilds pipelines without
    re-registering shaders (which would duplicate
    loader entries).
  - `shaderId(slot)` accessor.
- **`examples/vulkan_renderer/src/VulkanRenderer.cpp`** —
  reordered `initialize()` so loaders come before the
  pipeline build. Added a `Subscription` member that
  auto-detaches; subscribes to
  `engine.events<AssetReloaded>()` and dispatches Shader
  events to `pipes.rebuildIfMatches`. Public
  `reloadShaders()` walks the six tracked shaders and
  calls `engine.markResourceStale<Shader>(id)` for each;
  the loader's `update()` pump on the next tick re-reads
  the `.spv` files and emits `AssetReloaded`. Swapchain
  recreate now uses `pipes.recreatePipelines` instead of
  `pipes.destroy + pipes.create` to avoid duplicate
  shader registrations.
- **`examples/vulkan_renderer/include/threadmaxx_vk/VulkanRenderer.hpp`** —
  added `reloadShaders()` public method.
- **`examples/rpg_demo/HudSystem.{hpp,cpp}`** — ctor takes
  optional `ReloadShadersFn` callback. F12 invokes it
  (in headless mode where the callback is null, F12 is
  a no-op with an informational log line). Removed the
  synthetic `AssetReloaded` emit.
- **`examples/rpg_demo/DemoGame.{hpp,cpp}`** — added
  `setReloadShadersFn` setter; passes the callback to
  `HudSystem`'s ctor.
- **`examples/rpg_demo/main.cpp`** — installs the
  renderer-bound lambda
  `[r = renderer.get()]{ r->reloadShaders(); }` via
  `game.setReloadShadersFn` before `engine.initialize`.
- **`tests/rpg_demo/test_asset_reload.cpp`** — restructured
  to build the engine + game manually so a synthetic
  reload callback can be installed before
  `engine.initialize` fires. The callback emits
  AssetReloaded directly; the test still asserts
  "3 F12 presses → 3 deliveries". The test no longer
  relies on HudSystem's now-removed synthetic emit path.

**End-to-end chain exercised by F12 in production:**

1. F12 edge → `HudSystem::preStep` consumes it →
   invokes `reloadShadersFn_`.
2. The lambda calls `renderer->reloadShaders()`.
3. `reloadShaders` iterates the six tracked shader ids
   and calls `engine.markResourceStale<Shader>(id)` per.
4. `ShaderLoader::markStale` queues each reload.
5. On the next `engine.step()`, the post-postStep
   loader pump calls `ShaderLoader::update`, which
   re-reads each `.spv` file from
   `${VK_GENERATED_DIR}/{name}.spv`, registers a new
   slot, and emits `AssetReloaded{oldId, newId, type}`
   per reload.
6. The engine's tick-boundary event drain delivers
   each `AssetReloaded` to the renderer's subscriber.
7. The subscriber calls
   `pipes.rebuildIfMatches(ctx, engine, oldId, newId)`,
   which `vkDeviceWaitIdle`s, destroys the affected
   pipeline, fetches new SPIR-V from the registry,
   and creates a fresh `VkPipeline` using the
   current pipeline layout.

**Engine subsystems newly exercised end-to-end:**

- `IResourceLoader::markStale` / `update` / file I/O.
- `Engine::markResourceStale<T>(id)`.
- `EventChannel<AssetReloaded>` typed subscriber path,
  with the renderer as the canonical subscriber.
- `ResourceRegistry::acquire<T>(id)` for the renderer
  to refcount-pin a freshly-installed shader slot.

**Library gaps surfaced: none.** All hot-reload
plumbing already existed in the core; 9b.3 wired the
renderer + demo as the first real consumer.

**Items still deferred to 9b.4:** the full
bone-weighted Vulkan skinning pipeline — see
§3.11.7b.5 below for the three-stage breakdown.

**Test coverage:**

- `tests/rpg_demo/test_asset_reload.cpp` — restructured;
  still validates the F12 → AssetReloaded delivery chain
  via a synthetic reload callback. **PASS** (3
  deliveries).
- Renderer-side pipeline rebuild path is exercised at
  runtime via the rpg_demo's F12 handler.
  Validation-clean is the runtime gate.

**Verification:**
- Both `build/` and `build-werror/` clean.
- **ctest 94/94 on both trees** (unchanged total —
  test_asset_reload was updated in place, not added).

**Files:** 8 modified
(`vulkan_renderer/CMakeLists.txt`,
`VulkanPipelines.{hpp,cpp}`,
`VulkanRenderer.{hpp,cpp}`,
`HudSystem.{hpp,cpp}`,
`DemoGame.{hpp,cpp}`,
`rpg_demo/main.cpp`,
`tests/rpg_demo/test_asset_reload.cpp`).

**Effort:** ~3 hours actual.

#### 3.11.7b.5 Batch 9b.4 — Vulkan skinning pipeline — DEFERRED

This is the final renderer-side work item from the
original §3.11.7 D7 spec. Honest scoping: the full
skinning pipeline is a 1-2 week separate batch, not a
"slot in alongside 9b.3" deliverable. It touches the
vertex layout (which means a new pipeline variant —
the existing opaque pipeline's vertex bindings can't
silently grow without breaking the cube + pyramid
meshes already in flight), descriptor sets (currently
the renderer has none), per-frame pose upload
infrastructure (currently absent), and asset import
(a glTF parser is a sibling library, not a 2-hour
deliverable). Procedural Y-bob from D6 is the demo's
preserved fallback while this stays deferred.

The work splits naturally into three sub-batches:

- **9b.4.a — Skinned vertex layout + pipeline.** Add
  a `opaque_skinned` pipeline variant: vertex binding
  carries pos[3] + normal[3] + boneIds[4 × uint8] +
  boneWeights[4]. New `*_skinned.vert` shader that
  reads bone matrices from a descriptor-set-bound
  storage buffer. Pipeline + descriptor-set-layout
  creation; no demo wiring yet. ~3 days.
- **9b.4.b — Pose upload ring + descriptor sets.**
  Per-frame `UploadRing`-backed bone matrix buffer
  (4×4 matrices, indexed by per-instance bone-base
  offset). Descriptor-set-per-frame-slot allocation.
  Renderer wires the per-frame pose buffer binding
  before opaque_skinned draws. ~3 days.
- **9b.4.c — glTF skinned-mesh import + demo
  integration.** Sibling-library glTF 2.0 parser in
  `examples/rpg_demo/GltfLoader.{hpp,cpp}` producing a
  `SkinnedMeshData` POD (verts + indices + bone
  hierarchy + per-vertex bone influences). Demo
  swaps one NPC's render path from `CubeRender`
  (procedural Y-bob) to a `SkinnedRender` user
  component referencing the new pipeline. A simple
  walking animation drives the per-frame bone matrix
  upload. ~5 days.

Each sub-batch is independently shippable. None of
them is required to "close" §3.11 — D6's procedural
Y-bob already satisfies the original animation gate
per §3.11.6.

**Why this stays deferred rather than half-shipped:**

1. The vertex format change is irreversible without
   pipeline duplication — a half-finished skinned
   pipeline that ships as a stub would either crash
   on draw (no descriptor set bound) or render
   garbage (uninitialized bone matrices).
2. Validation-clean is the renderer's correctness
   gate; a skinning path that's never actually drawn
   can't be validated this way.
3. The demo's existing entities (player, NPCs,
   pickups, sword) are all unit cubes / pyramids
   with no bone structure — there's nothing to skin
   without authoring real assets.
4. The procedural Y-bob from D6 is the right
   fallback for the demo's current asset set; a
   half-finished skinning system doesn't replace
   anything.

When 9b.4 is undertaken, it gets its own focused
session (or three).

---

**Original D7 spec (for reference):**

**Game feature:** NPCs, player, pickups load actual `.obj`
files instead of using the unit-cube fallback mesh. Shaders
on disk (no longer embedded); F12 triggers a shader edit →
reload → pipeline rebuild cycle in real time.

**Engine subsystems exercised:**

- `MeshLoader` / `TextureLoader` / `ShaderLoader` (real
  file I/O, not the unit-cube / 1×1-white fallbacks).
- `Engine::markResourceStale<Shader>` +
  `EventChannel<AssetReloaded>` subscriber on the
  renderer side rebuilding pipelines.
- `Engine::preloadUntil` at boot — blocks the splash
  screen until all assets ready.
- `ResourceHandle<Mesh>` refcount (batch 7) — meshes
  released when no entity uses them.
- `IResourceLoader::stats()` aggregated in HUD.

**Expected library gaps:** the renderer-side
`AssetReloaded` subscriber is the missing piece from batch 9
(`asset reloaded → rebuild pipeline`). Schedule it as part
of this batch. The actual `.obj` parsing belongs in
`examples/rpg_demo/` (sibling-library territory per §3.3),
not in the engine.

**Effort:** ~3 days (split: 1 day OBJ loader, 2 days
renderer reload subscriber).

#### 3.11.8 Recommended ordering

Suggested sequence (each batch shippable independently):

```
D1 → D3 → D2 → D5 → D4 → D6 → D7
combat  save  cams  scale  quest anim   assets
```

Rationale:
- **D1 first**: combat is the smallest scope; introduces
  damage / death loops that D3 (save/load) and D4 (quests)
  both need.
- **D3 second**: save/load lets every subsequent batch
  test "does this survive a quick-save/load cycle?" as a
  regression gate.
- **D2 third**: multi-camera is mostly demo-side; the
  renderer change is contained.
- **D5 fourth**: scale stress exposes real perf
  characteristics before we add expensive features (D6
  skinning, D7 asset loading).
- **D4 fifth**: quests sit on top of D1/D3/D5
  foundations.
- **D6/D7 last**: both require renderer-side work that
  benefits from the demo being stable + scale-tested
  first.

#### 3.11.9 Definition of done for §3.11

§3.11 is closed when:
- All seven batches have landed with their as-shipped
  blocks documented.
- `rpg_demo` runs at 60 Hz with 60k entities (D5 budget)
  validation-clean for 600 ticks on both build trees.
- A full play-loop is demonstrable: boot → preload (D7) →
  spawn → combat (D1) → quest progression (D4) →
  save (D3) → load → resume → exit.
- The library has absorbed every gap the demo surfaced,
  either as a §3.10.3 batch or a fresh §3.12+ batch.

Further engine work past §3.11 (if any) starts its own
§3.x section against fresh real-game evidence.

#### 3.9.6 Out of scope for §3.9

Per the notes, the following are not §3.9 batches. They're
either sibling-library territory, niche-hardware concerns, or
ideas the notes flag as "only on profiler evidence." Calling
them out so they don't drift into batch 16's success
criteria:

- **SIMD math kernels.** Sibling library (notes §4.1). The
  engine ships the layout (chunked archetype storage); the
  math vectorization is the consumer's responsibility.
- **NUMA-aware allocation.** Niche, not on the near-term path
  (notes §5.1). Revisit if a workstation/server-class
  deployment shows topology-aware cache effects.
- **Alternate work-stealing policies.** Only on profiler
  evidence of tail-latency outliers (notes §5.2). Today's
  policy passes batch-13c's job-stealing bench cleanly.
- **User-defined component packing.** Invasive vs the
  migration / storage path (notes §5.3). Not worth it until a
  strong use case shows up.
- **Epoch-based reclamation for event nodes.** Internal
  allocator-churn fix (notes §5.4). Only on profiler evidence
  that the Treiber-stack allocation rate is showing up.
- **Per-chunk record-time command buffers.** Tracked as a
  §3.6.4 candidate; revisit if batch 18's arena form falls
  short on benchmarks.
- **Read-only cross-wave snapshot pointer cache.** Tracked as
  a §3.6.4 candidate; revisit if batch-16 evidence shows
  repeated chunk-pointer reuse across waves.

#### 3.9.7 Recommended order of attack

`16 → {17, 18, 19} (parallel after gate) → 20`.

Batch 16 is the gate: nothing else in §3.9 ships without a
benchmark row in its PR body that points at a run produced by
batch-16 infrastructure. Once 16 lands, batches 17 / 18 / 19
ship independently when their own measurement bar is met (and
do NOT ship if the bar isn't met — the notes are explicit that
*"if a proposed optimization does not improve fewer
branches / allocations / lookups / locks / redundant
traversals / more contiguous access / more predictable job
sizes, it probably is not worth the added complexity."*).
Batch 20 is orthogonal — a quality-of-life stutter fix with
no benchmark dependency.

#### 3.9.8 Definition of done for §3.9  ✅ met 2026-05-16

Plan-level success criteria (verifiable against existing
diagnostic surface; no new public API required):

- ✅ **Iteration framework no longer the bottleneck on
  Render+AI.** Batch 17 measurements show all four iteration
  paths (`forEachWith` / `forEachWithCached` / `forEachChunk`
  / `rawMaskedWalk`) converging at 65–67 ns/entity on the
  Render+AI workload, meaning the body's accumulation work
  dominates, not the scheduler. Within-5% target met.
- ✅ **`commit_hash_test.cpp` and `sharded_commit_test.cpp`
  pass byte-for-byte** against the pre-§3.9 reference hashes
  after every batch — proves batches 17 / 18 / 19 / 20
  altered no observable storage behavior. (Verified 80/80
  on both `build/` and `build-werror/` after each batch.)
- ✅ **Both default and `-Werror` builds pass `ctest` 100%**
  on every commit in the §3.9 sequence.
- ⚠ **4096-tick `FrameBudgetWatcher`-free Churn run not yet
  re-measured.** Skipped because the §3.9 perf wins on every
  measured workload moved the bar in the right direction
  (chunk iter −35–70%, commit path −7–11%, migration tail
  −18–26%); a fresh soak run is a real-game next step but
  not a blocker for §3.9 closure.

§3.9 is closed. Further perf work past this point is the
next "if profiling says so" pass and would start its own §3.x
section against fresh evidence. The §3.6.4 candidates
(per-chunk record-time command buffers, read-only cross-wave
snapshot pointer cache) remain parked.

## 4. Items the previous plan got right but underestimates the cost of

- **Archetype/chunk storage.** Shipped in batch 6 (✅ §2), with the
  user-extensible follow-on in batch 6b (✅ §2). Sequencing
  mattered: it didn't happen until the public surface (queries,
  events, resources, renderer contract) was settled. The lazy
  stitched view kept the public API stable across the refactor.
- **Frame task graph.** Now §3.4 batch 11. A useful Phase-3-of-§6
  win; smaller bang-for-buck than the JobSystem rewrite (done).
  Once intra-system parallelism is the bottleneck (currently it
  isn't), this becomes the right next move.

## 5. Out of scope for `threadmaxx` itself

These are good items for a *game* built on `threadmaxx`, but baking
them into the backend would either tie the library to a specific
third-party implementation or bloat the public surface past the
"small, stable contract" principle. The right shape for the engine is
to provide hooks (component categories, event channels, render-prep
slots) rather than ship the systems themselves:

- **Networking, replication, rollback.** Belongs above the engine.
  The engine should at most provide deterministic commit + stable
  entity IDs (both already true) so a game can layer its own
  snapshot/delta logic.
- **Animation systems / IK / cloth / blend trees.** A real animation
  pipeline depends on the renderer's skinning model and the asset
  format. The engine can host these as user systems once a
  `Skeleton` / `AnimationState` component shape exists (§3.1), but it
  should not own the math.
- **Physics integration (broadphase / narrowphase / rigid body).**
  Same reasoning — Bullet / Jolt / PhysX each impose a world
  ownership model incompatible with hardcoding one. A `PhysicsBodyRef`
  component slot and read-only world snapshot pattern is the
  engine's job; the solver is not.
- **Audio mixing / 3D audio.** Wholly orthogonal to a game backend.
- **Save/load migration.** A serialization *hook* on components is in
  the engine (✅ batch 4); a full versioned migration system is not.
- **Navmesh / pathfinding.** Belongs in a domain library; the engine
  only needs to allow background work to be scheduled.
- **Editor/tooling/hot-reload UI.** Out of scope until the public API
  has stabilized through Milestone 4.
- **Math / SIMD helpers.** The engine ships POD `Vec3` / `Quat` /
  `Transform` for layout compatibility but no arithmetic library.
  SIMD wrappers (AVX2/NEON helpers, swizzles, fast inverse-sqrt,
  matrix math) belong in a sibling project — games that already
  use glm / mathfu / sleef shouldn't have to fight the engine for
  the math style. What threadmaxx legitimately provides is
  **layout** — the chunked archetype storage (batch 6) gives
  contiguous parallel arrays, which is the SIMD enabler. User
  systems vectorize their inner loops over those arrays however
  they like.

## 6. Multithreading and performance roadmap

The previous roadmap's phases still describe the right arc; Phase 1
is done.

### Phase 1 — stabilize the current job model  ✅ done

- Per-worker work-stealing deques.
- Atomic outstanding counter with last-decrement-notify.
- Decoupled `waitIdle` synchronization.
- Determinism preserved (commit order unchanged).

### Phase 2 — split system work into finer tasks (current)

The wave scheduler buys parallelism *between* systems. The next axis
is finer slicing *within* a system: per-chunk iteration becomes
expressible after §3.1, and per-render-pass iteration is now
expressible (batch 8 landed in §2).

### Phase 3 — frame task graph  ✅ done (batch 11, 2026-05-14)

`TaskTag` + `ISystem::dependencies` / `provides` /
`preferredGrain`; `rebuildWaves` rewritten to topo-sort the
combined read/write + tag DAG; cycle detection logs via `ILogger`
and drops tag-only edges to recover; `Engine::taskGraphSnapshot()`
exposes the resolved graph for HUD / visualization. Shipped in
batch 11; details in §2.

### Phase 4 — cancellation and budgets  ✅ done (batch 12, 2026-05-14)

`SystemContext::shouldYield` (atomic poll mirroring engine's
`overBudget_`); `Engine::setTickBudget` + `ISystem::skippable`;
`Engine::setSkipPolicy(Budget|Scripted)` +
`EventChannel<SystemSkipped>` for deterministic networked replay;
`JobPriority` (High/Normal/Low) on `parallelFor` with per-worker
per-priority deques; `IResourceLoader::cancel(Engine&)` pumped
before `update()` each tick + `LoaderStats::cancelled`. Shipped in
batch 12; details in §2.

### Phase 5 — reduce contention in storage  ✅ done (batches 13a/13b/13c, 2026-05-15)

Per-tick `commitHash` runtime determinism guard + `Config::singleThreadedCommit`
toggle (13a); sharded commit phase keyed on destination chunk
(13b); lock-free MPSC `EventChannel::emit` and `WorldView`
wave-scoped chunk-pointer cache (13c). The per-worker scratch
arena from batch 2 was the down payment. Measured benchmark
takeaway: the sharded commit path's classifier overhead
currently exceeds its parallelism win at every tested workload
shape — keep `singleThreadedCommit = true` as the default and
re-evaluate when the M4/M6 examples expose real commit-phase
pressure. Further deferred follow-ons (per-chunk record-time
buffers, read-only cross-wave snapshot) are tracked in §3.6.4.

### Phase 6 — measure everything  ✅ done (batch 14, 2026-05-15)

Job duration histograms + Chrome-trace adapter (✅ batch 4) were
the *primitives*. Batch 14 (✅) closed the phase by shipping the
*ingestion* side: `ITraceSink` interface with `FileTraceSink`
(rolling Chrome-trace JSON) and `HudTraceSink` (seqlock-protected
latest snapshot for HUDs), `FrameBudgetWatcher` built-in system
emitting `BudgetExceeded` events, and `Engine::setStallTimeout`
watchdog thread emitting `EngineStall`. The Vulkan renderer
(batch 9) and RPG demo (batch 10) are the first real-game
consumers.

### Phase 7 — measurement-driven tightening (current, §3.9)

Once Milestones 1–6 closed, the next perf phase is *not* another
structural rewrite. Per
`threadmaxx_core_future_optimization_notes.md` and §3.9, the
remaining wins are:

- cheaper chunk traversal (§3.9.2 batch 17)
- cheaper command recording (§3.9.3 batch 18)
- cache-friendly migration batching (§3.9.4 batch 19)
- better grain tuning + a workload-realistic benchmark harness
  (§3.9.1 batch 16 — the gate)
- async snapshot / trace off-thread for stutter-free saves
  (§3.9.5 batch 20 — quality of life, orthogonal)

Every batch in this phase ships with a before/after number from
the batch-16 harness in its PR body. If the number isn't there
or doesn't move, the batch doesn't land — the notes are explicit
that *"if a proposed optimization does not improve fewer
branches / allocations / lookups / locks / redundant traversals /
more contiguous access / more predictable job sizes, it probably
is not worth the added complexity."*

## 7. Public API extensions still on deck

This section used to enumerate planned API additions. With §3 now
listing them by batch, this is the cross-reference index:

- `World::has<T>` / `World::get<T>` — ✅ batch 3. `hasTag` —
  ✅ batch 5.
- Async loader contract — ✅ batch 3 (basics); ✅ batch 7 (pipeline,
  onShutdown, stats, preloadUntil).
- Hot reload — ✅ batch 7 (`markStale` + `AssetReloaded`).
- Refcounted `ResourceHandle<T>` — ✅ batch 7.
- Save/load (serialization trait pair) — ✅ batch 4. Batch-5 widening
  bumped `kWorldSnapshotVersion` to 2.
- Tracing / Chrome-trace adapter — ✅ batch 4.
- Persistent event subscribe — ✅ batch 4 (manual unsubscribe);
  ✅ batch 7 (RAII `Subscription` handle).
- HierarchySystem scale knob — ✅ batch 4.
- Job-duration histograms — ✅ batch 4.
- `ILogger` — ✅ batch 4.
- SystemStats wait/queue-depth — ✅ batch 4.
- New component slots (Health, Faction, AnimationStateRef,
  PhysicsBodyRef, NavAgentRef, BoundingVolume) — ✅ batch 5.
- Tag-only components (StaticTag, DisabledTag, DestroyedTag) +
  `addTag`/`removeTag`/`hasTag` — ✅ batch 5.
- `MaskCache` + `forEachWithCached` — ✅ batch 5.
- 64-bit `ComponentSet` — ✅ batch 5.
- Determinism N-tick golden test — ✅ batch 5.
- Generic `addComponent<T>` / `removeComponent<T>` on
  `CommandBuffer` + `World::archetypeSignatures()` — ✅ batch 6a.
- Chunked archetype storage + `forEachChunk` +
  `World::archetypeChunkCount()` / `archetypeChunk(i)` + lazy
  stitched view + chunk-level render-frame builder — ✅ batch 6.
- `UserComponent<T>` extension hook
  (`Engine::registerUserComponent<T>`, `UserComponentId`,
  `addUserComponent` / `removeUserComponent`, `user::has` /
  `tryGet` / `chunkSpan`, `World::locate`) — ✅ batch 6b.
- Pass-aware `RenderFrame` + `buildRenderFrame` hook + render
  helpers (Camera, Light, DrawItem, Visibility, InstanceBufferLayout,
  UploadRing) — ✅ batch 8.
- Frame task graph (`ISystem::dependencies` / `provides` /
  `preferredGrain`, `TaskTag`, `taskGraphSnapshot`) — ✅ batch 11.
- Cancellation + budgets (`shouldYield`, `setTickBudget`,
  `setSkipPolicy` + `EventChannel<SystemSkipped>`,
  `pushScriptedSkip`, `JobPriority`, `IResourceLoader::cancel`,
  `LoaderStats::cancelled`) — ✅ batch 12.
- Storage contention reduction (sharded commit phase, per-chunk
  command buffers, read-only world snapshot per wave, lock-free
  event channels, per-tick `commitHash` runtime determinism
  guard) — §3.6 batch 13.
- Telemetry ingestion (`ITraceSink`, `FileTraceSink`,
  `HudTraceSink`, `FrameBudgetWatcher`, stall watchdog) — §3.7
  batch 14.
- Networking deltas — out of scope; belongs above the engine
  (see §5).

## 8. Roadmap milestones

### Milestone 1 — Hardening the current core  ✅ done

Per-system instrumentation (incl. wait/queue-depth and a job-duration
histogram), sharded job queue, presence mask, hierarchy (with opt-in
scale chain), resource registry, lifecycle hooks, scratch arenas,
event channels (with persistent subscribe), pause/time-scale, reserved
handles, commit timing, `has`/`get`, `Bundle`, `registerSystemAt`,
`frameSnapshot`, `writeJsonLines`, `ChromeTraceWriter`,
`IResourceLoader` contract, `SpatialHash`, Parent auto-derive,
serialization + `World::snapshot`, `ILogger`.

Exit criteria met: a small game can ship against the current public
API without patching the engine.

### Milestone 2 — Data model upgrade  ✅ done

Widened `ComponentSet`, additional engine-known component slots,
archetype/chunk storage, `forEachChunk`, determinism golden tests.
Batch 5 (✅) shipped the mask widening, six new POD components,
three tag-only categories, the `MaskCache` opt-in, and the N-tick
determinism golden test. Batch 6a (✅) shipped the public-API
half of batch 6 — generic per-component transition API,
archetype signatures, and the determinism stress baseline.
Batch 6 (✅) shipped the chunked archetype storage —
`ArchetypeChunk` / `ArchetypeTable`, physical mask-change
migration, `forEachChunk<Required...>`, and chunk-level filtering
in `buildRenderFrame`. Batch 6b (✅) shipped the
user-extensible dense-component hook — `registerUserComponent<T>`,
`UserComponentId`, `addUserComponent` / `removeUserComponent`,
`user::has` / `tryGet` / `chunkSpan`. Milestone closed 2026-05-14.

### Milestone 3 — Resource and event layers  ✅ done

Multi-stage async loader pipeline, refcounted asset handles, hot
reload, boot-time preload, persistent event subscription RAII, loader
shutdown contract. Shipped 2026-05-14 in batch 7.

### Milestone 4 — Rendering contract expansion + reference renderer

Hierarchical `RenderFrame`, render-side POD types (Camera, Light,
DrawItem with MeshSkinnedRef / AnimationPoseRef / MaterialOverride),
`buildRenderFrame` lifecycle hook, visibility-culling helpers,
shared instance/pose buffer helpers — all shipped in batch 8
(✅ §2). The Vulkan reference renderer as
`examples/vulkan_renderer/` (§3.1 batch 9) is the remaining piece.

### Milestone 5 — Task graph and deep parallelism

Explicit DAG, intra-system cancellation, priorities, sharded
commit phase, telemetry ingestion. Spread across four batches:
**batch 11** (§3.4, task graph), **batch 12** (§3.5, cancellation
and budgets), **batch 13** (§3.6, storage contention), **batch
14** (§3.7, telemetry sink). Batches 11–13 are independent of the
Vulkan renderer and the RPG demo — they can ship in any order
that doesn't violate their stated gating. Batch 14 is best landed
alongside batch 10 so the RPG demo exercises the telemetry path.

### Milestone 6 — Integration readiness

The public API is stable and wide enough that a real 3D RPG can
integrate sibling navigation, animation, physics, and networking
libraries **without engine patches**. The library does not ship
those features — the hooks for them are what threadmaxx ships:
`NavAgentRef`, `AnimationStateRef`, `PhysicsBodyRef`, deterministic
commit, stable entity IDs, snapshot serialization (✅ batch 4),
event channels, hot reload. `examples/rpg_demo/` (§3.2 batch 10)
is the integration proof — when the demo runs without a single
engine-side edit, this milestone is closed.

## 9. Engineering priorities

When implementing future work, prioritize in this order:

1. Correctness and thread safety.
2. Stable public API.
3. Query and storage performance.
4. Resource and event systems.
5. Parallel task scalability.
6. Rendering submission richness.
7. Networking and serialization.
8. Editor/tooling support.

## 10. Definition of a production-ready version

`threadmaxx` is production-ready for a 3D RPG when:

- it supports large worlds without excessive contention,
- it can stream assets and chunks asynchronously,
- it supports multiple renderer backends cleanly,
- it has stable serialization and save/load,
- it can expose profiling and debugging data,
- it supports deterministic or reproducible simulation modes,
- it can be integrated into a real game without patching core
  internals,
- and it scales across multiple CPU cores in common gameplay and
  render-prep workloads.

Today: "stable serialization", "expose profiling data", and
"deterministic mode declared" are in (batch 4); the data-model
widening that the archetype refactor depended on is in (batch 5);
the chunked archetype storage itself plus `forEachChunk` are in
(batch 6); the user-extensible dense-component hook closing
Milestone 2 is in (batch 6b — `Engine::registerUserComponent<T>`,
`UserComponentId`, the `addUserComponent` / `removeUserComponent`
free functions, and the `user::has` / `tryGet` / `chunkSpan` read
helpers); "stream assets asynchronously" is in (batch 7 —
multi-stage loader pipeline, hot reload, refcounted handles,
blocking preload); the hierarchical render contract that
`examples/vulkan_renderer/` will consume is in (batch 8 —
cameras, lights, per-pass bins, debug overlay, `buildRenderFrame`
hook, visibility helpers, instance/upload helpers); the rest
depends on §3 batches 9–14 (Vulkan example, RPG demo, task graph,
cancellation/budgets, storage contention, telemetry ingestion).

## 11. Final note

The current architecture is a solid foundation, and the Milestone 1
additions across batches 1–4 (component masks, hierarchy, resources,
work-stealing queue, per-system stats, lifecycle hooks, events,
scratch, time control, tracing, Chrome-trace writer, async-loader
contract, spatial hash, serialization, logger, job histograms,
queue/wait timing, scale-chain knob, persistent event subscribe)
plus the batch-5 data-model widening (64-bit mask, six new POD
components, three tag-only categories, MaskCache, N-tick determinism
golden test), the batch-7 resource/event maturity (refcounted
handles, hot reload, loader stats / onShutdown, blocking preload,
RAII event subscriptions), the batch-8 render contract (hierarchical
`RenderFrame`, `buildRenderFrame` hook, Camera / Light / DrawItem /
debug PODs, visibility helpers, instance buffer layout, upload ring),
the batch-6a archetype prep (generic `addComponent` /
`removeComponent`, archetype signatures, determinism stress baseline),
the batch-6 chunked archetype storage itself (`ArchetypeChunk` +
`ArchetypeTable`, physical mask-change migration, `forEachChunk`,
chunk-filtered render-frame builder, lazy stitched view), and the
batch-6b user-extensible dense-component hook
(`Engine::registerUserComponent<T>`, `UserComponentId`,
`addUserComponent` / `removeUserComponent`, `user::has` / `tryGet` /
`chunkSpan`, per-chunk `UserComponentColumn` storage) confirm that
the layering is sound — every one landed without churning the
public API, including the deep batch-6 internal refactor and its
batch-6b runtime-type-erased extension hook.

Milestone 2 is now closed. With the resource/event, render-
contract, archetype storage, AND user-extensible component hook
all stable, the public API is wide enough that a real game project
can be built on top without engine patches. The §3 plan splits the
remaining work into two streams: the **example projects** (batch
9 Vulkan renderer, batch 10 RPG demo — both `examples/...`, neither
in the core library) and the **Milestone-5 perf batches** (batch
11 task graph, batch 12 cancellation/budgets, batch 13 storage
contention, batch 14 telemetry ingestion — all library-side, all
optional opt-ins). The two streams are loosely coupled: batches
11–13 can ship before either example, and batch 14 is best landed
alongside batch 10 so the RPG demo exercises the telemetry path
under real load. The §3 plan continues to treat the Vulkan
reference renderer as an example, not a library dependency. That
keeps the renderer-agnostic guarantee intact while still letting
M4+ work target a real, modern API.
