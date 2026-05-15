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
| 10    | 3D RPG demo example                | M6 lead-in — example, not library |
| ~~11~~ | ~~Frame task graph (§6 phase 3)~~ | ✅ landed 2026-05-14 — see §2 |
| ~~12~~ | ~~Cancellation, budgets, priorities (§6 phase 4)~~ | ✅ landed 2026-05-14 — see §2 |
| ~~13~~ | ~~Storage contention reduction (§6 phase 5)~~ | ✅ landed 2026-05-15 — see §2 |
| ~~14~~ | ~~Telemetry ingestion (§6 phase 6 close-out)~~ | ✅ landed 2026-05-15 — see §2 |
| 15    | Audit-driven hygiene + pre-batch-9 API polish | M5 close-out, blocking batch 9 |

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
(batch 9) and RPG demo (batch 10) will be the first real-game
consumers.

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
