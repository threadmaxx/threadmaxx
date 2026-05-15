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

Last refreshed: 2026-05-14 (Milestone 2 closed and the first two
Milestone-5 slices landed. Batch 6b shipped the user-extensible
dense-component hook. Batch 11 shipped the frame task graph
(`TaskTag`, `ISystem::dependencies` / `provides` /
`preferredGrain`, DAG-aware `rebuildWaves` with cycle detection,
`Engine::taskGraphSnapshot()`). Batch 12 shipped cancellation,
budgets, and priorities — `Engine::setTickBudget`,
`SystemContext::shouldYield()`, `ISystem::skippable()`,
`Engine::setSkipPolicy(Budget|Scripted)` +
`EventChannel<SystemSkipped>` for deterministic networked replay,
`JobPriority` (High/Normal/Low) on `parallelFor` with per-worker
per-priority deques, and `IResourceLoader::cancel(Engine&)` pumped
before `update()` each tick + `LoaderStats::cancelled`. All five
pieces are opt-in; defaults preserve existing behavior
bit-for-bit. Batches 5, 6, 6a, 7, 8 remain ✅ (see prior refresh
notes below). The §3 plan continues with batches 9 (Vulkan
reference renderer) and 10 (RPG demo) for Milestones 4 and 6, plus
the remaining perf batches 13 (storage contention) and 14
(telemetry ingestion) — see §3.6 and §3.7.).

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

Eleven batches plus a prep slice have landed, closing Milestones 1,
2, and 3, the M4 contract, and §6 phases 3+4 of M5. Batches 1–3
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
| 9     | Vulkan reference renderer (example) | M4 — example, not library |
| 10    | 3D RPG demo example                | M6 lead-in — example, not library |
| ~~11~~ | ~~Frame task graph (§6 phase 3)~~ | ✅ landed 2026-05-14 — see §2 |
| ~~12~~ | ~~Cancellation, budgets, priorities (§6 phase 4)~~ | ✅ landed 2026-05-14 — see §2 |
| 13    | Storage contention reduction (§6 phase 5) | M5             |
| 14    | Telemetry ingestion (§6 phase 6 close-out) | M5            |

§3.8 covers the Vulkan defaulting strategy across batches 8–10.

Batches 9 and 10 are **example projects**, not library batches —
they consume the existing public surface and prove a real game can
be built on top. The library-side perf batches 11–14 can ship
independently of the examples; in practice the natural ordering is
**11 → 9 → 12 → 13 → 10 → 14**, but the dependencies are loose
enough that 11/12/13 can land before either example, and 14 can
land alongside 10 because telemetry quality is best measured under
a real game's workload. See §3.4–§3.7 for the per-batch scopes and
the cross-batch sequencing rationale.

### 3.1 Batch 9 — Vulkan reference renderer (Milestone 4)

The first concrete renderer that exercises the full §2 batch-8
contract. **Lives in `examples/vulkan_renderer/`, NOT in the core
library** — that preserves the renderer-agnostic guarantee. The core
lib's only Vulkan-aware concession is the optional helpers shipped
in batch 8.

- **`examples/vulkan_renderer/`.** Vulkan 1.3 (dynamic rendering,
  timeline semaphores, sync2), GLFW for window/surface. Treated like
  `examples/boids` — opt-in via CMake, skipped if Vulkan SDK not found.
- **Implements `IRenderer`** against the hierarchical `RenderFrame`:
  multi-camera, per-pass binning, instanced mesh draw, skinned pose
  upload, depth + shadow pass, simple PBR-ish opaque shader, debug
  overlay.
- **Asset loaders.** A `MeshLoader` / `TextureLoader` /
  `ShaderLoader` (compiled SPIR-V at build time via `glslc`) that
  exercise the batch-7 multi-stage pipeline and refcounted handles.
- **Hot reload.** Shader edits trigger SPIR-V rebuild and pipeline
  rebuild via the `AssetReloaded` channel from batch 7.
- **Cross-platform CI.** Build on Linux + Windows runners. macOS via
  MoltenVK marked best-effort.
- **Smoke scene.** Animated character on a lit terrain plane, third-
  person camera, 1k crowd of instanced meshes — proves batch 8's
  contracts under load.

### 3.2 Batch 10 — 3D RPG demo example (Milestone 6 lead-in)

Closes the loop. Built on top of the Vulkan renderer; demonstrates
that a real game can be developed without engine patches.

- **`examples/rpg_demo/`.** A small open scene: terrain, day/night
  cycle, a player, ~50 NPCs with simple behavior trees,
  inventory pickups, save/load, profiling HUD.
- **Exercises every milestone.** Archetype storage (M2), multi-stage
  asset loading + hot reload (M3), pass-aware rendering with skinned
  characters (M4), serialization (batch 4), spatial-hash AOI for
  AI (batch 3), event subscriptions (batch 4), Chrome-trace
  capture (batch 4), Health / Faction / BoundingVolume / tag-only
  components (batch 5).
- **No engine patches.** The success criterion is that everything
  lives in `examples/rpg_demo/` plus the public threadmaxx headers
  — if the demo needs an engine change, it goes back through the
  next batch instead of into the example.

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
batch 6 (parallel commit needs per-chunk locks). Effort ~2 weeks.

#### Deliverables

- **Sharded commit phase.** Group commands by destination chunk;
  commit each group on its own helper thread. Spawn/destroy on
  disjoint chunks no longer serializes. The single-threaded path
  remains for tests / debugging via
  `Config::singleThreadedCommit = true`.
- **Per-chunk command buffers.** During wave execution, workers
  emit commands into a "by destination chunk" thread-local bucket
  (using the entity's current archetype as the routing key) rather
  than one global buffer per system. Cross-chunk commands (the
  rare case: a mask-change that migrates an entity) fall back to
  the global lane.
- **Read-only world snapshot per wave.** Systems read through an
  immutable snapshot pointer rather than the live world reference;
  the snapshot pointer is rebound between waves. Allows worker
  jobs to take stable references to chunk data without worrying
  about concurrent reallocation from another worker spawning.
- **Append-only event channels.** Replace the existing mutex-
  protected `emit` with a lock-free MPSC queue per channel. Drain
  is still on the sim thread at tick boundary.
- **Per-tick commit-stream hash.** A running FNV-1a-64 accumulator
  fed by every applied mutation (spawn / destroy / setX / migrate)
  in the commit phase. Exposed on `EngineStats::commitHash` and
  written to the trace sink. The single-threaded reference path
  produces the same hash for the same inputs by construction;
  the sharded path must match it tick-for-tick. Cheap (a few ns
  per command) and converts any sharding bug from a silent state
  divergence into a loud first-tick alarm.
- **`Config::logCommitHashEvery`.** Opt-in production diagnostic
  knob: when set to N > 0, the engine logs `commitHash` via
  `ILogger` every N ticks. Default 0 (off, zero cost). Catches
  divergence in shipped builds — game devs run two clients with
  the same seed, the first diverging hash points at the offending
  tick and reproduces the bug locally.
- **`tests/sharded_commit_test.cpp`** — **five seeded churn
  scenarios** (varying entity counts 1k/8k/32k, varying mask-flip
  patterns, with/without parent-hierarchy mutations), each running
  for **256 ticks with the sharded path on, hash-compared against
  the single-threaded reference path**. Both the per-tick
  `commitHash` AND the final `WorldSnapshot` FNV-1a hash must
  agree across every (scenario, tick) pair. The five scenarios
  catch interleaving bugs that a single fixed pattern would miss;
  the 256-tick window catches accumulation bugs the one-tick check
  would miss; and the `Config::logCommitHashEvery` knob above is
  the production-side equivalent if any divergence escapes CI.

#### Risks and mitigations

- **Determinism preservation.** Per-chunk parallel commits must
  produce a state byte-identical to the single-threaded reference.
  Mitigation in three layers: (1) commands within a chunk still
  apply in submission order; (2) cross-chunk commands fall back
  to deterministic registration-order serial commit; (3) the
  per-tick `commitHash` above provides a runtime safety net. If a
  divergence is ever discovered in production, `Config::singleThreadedCommit
  = true` is the documented immediate fallback — the sharded path
  is a pure performance opt-in.
- **False sharing.** Per-chunk buffers risk cache-line contention.
  Mitigation: pad the buffer headers to 64 bytes; standard
  alignas-based fix.

### 3.7 Batch 14 — Telemetry ingestion close-out (Milestone 5, §6 phase 6)

§6 phase 6 says "measure everything." Batch 4 shipped the
*primitives* — job-duration histograms, Chrome-trace writer,
`writeJsonLines`, system stats, frame snapshot. What's still
missing is the **ingestion side**: a way for game code to feed
this data into a HUD, an external monitor, or a CI gate without
re-implementing the IO pipeline.

Gating: this is the natural close-out batch for §6. Best landed
alongside batch 10 (the RPG demo) because the demo is what
actually exercises the telemetry end-to-end. Effort ~1 week.

#### Deliverables

- **`Engine::traceSink(ITraceSink&)`** — install a sink that the
  engine streams `FrameSnapshot` + per-system trace events to,
  once per tick, on the sim thread. The sink is responsible for
  buffering and I/O off-thread. Default sink is null (no cost).
- **`FileTraceSink`** — built-in implementation that writes a
  rolling Chrome-trace JSON to disk with automatic file rotation
  every N MB. Header-only.
- **`HudTraceSink`** — built-in implementation that exposes a
  read-only `LatestTelemetry` struct game code polls each frame
  for HUD rendering. Lock-free single-writer/single-reader.
- **`FrameBudgetWatcher`** — a built-in `ISystem` that watches
  `EngineStats::lastStepSeconds` against a target and emits
  `BudgetExceeded` events when the frame goes over. Pairs nicely
  with batch 12's budget hint.
- **`Engine::setStallTimeout(seconds)`** — install a watchdog that
  fires `EngineStall` events when a tick takes longer than the
  threshold. The watchdog runs on its own thread; the events
  drain like any other on the sim thread.
- **`tests/telemetry_sink_test.cpp`** — verify the file sink
  rotates, the HUD sink hands the latest snapshot to the reader
  without tearing, and the watchdog fires on a deliberately
  stalled tick.

#### Risks and mitigations

- **Sink ownership.** A user-installed sink must outlive the
  engine. Mitigation: sinks are pointer-based (engine doesn't take
  ownership), mirroring `setRenderer` / `setLogger`.
- **HUD-sink staleness.** A slow reader might see a stale
  snapshot. That's fine for a HUD; documented as the contract.

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

### Phase 5 — reduce contention in storage — §3.6 batch 13

Read-only snapshots per wave, sharded commit phase keyed on
destination chunk, append-only event channels, double/triple
buffering where useful, plus a per-tick `commitHash` runtime
determinism guard so any sharding bug surfaces on the first
divergent tick. The per-worker scratch arena from batch 2 is a
down payment. Tracked as **batch 13** in §3.6. Builds on batch
6's chunked storage; independent of 11/12.

### Phase 6 — measure everything — §3.7 batch 14

Job duration histograms (✅ batch 4), Chrome-trace adapter
(✅ batch 4) — the *primitives* are all in. **Batch 14** in §3.7
closes the phase by shipping the *ingestion* side: a pluggable
`ITraceSink` with a `FileTraceSink` and `HudTraceSink`, a
`FrameBudgetWatcher` built-in system, an
`Engine::setStallTimeout` watchdog. Best landed alongside batch
10 (the RPG demo) for an end-to-end shakedown.

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
