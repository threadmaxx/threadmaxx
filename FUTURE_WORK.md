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

Last refreshed: 2026-05-14 (Batches 5, 7, and 8 have landed; batch 6
— the archetype refactor — is the only remaining Milestone 2 item.
Batch 5 widened the data model; batch 7 shipped resource & event
maturity for Milestone 3 — refcounted handles, hot-reload protocol,
blocking preload, RAII event subscriptions, loader stats and
shutdown hook; batch 8 (2026-05-14) shipped the hierarchical
render contract for Milestone 4 prep — cameras, lights, per-pass
draw bins, debug geometry, `buildRenderFrame` lifecycle hook,
visibility culling helpers, instance buffer layout helper, per-
frame upload ring. With both the resource/event and render-contract
surfaces settled, the archetype refactor can land next without
churning the public API a third time.).

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

Seven batches have landed. Batches 1–3 brought **Milestone 1** to
completion on 2026-05-13; batch 4 (2026-05-14) closed out the M1
polish and seeded the tracing maturity batches 5+ build on; batch 5
(2026-05-14) widened the data model for Milestone 2 — six new POD
components, three tag-only categories, a 64-bit `ComponentSet`, and
the `MaskCache` opt-in fast path; batch 7 (2026-05-14) shipped
resource & event maturity for Milestone 3 — refcounted handles,
hot-reload protocol, loader shutdown hook + stats, blocking
preload, and an RAII event subscription handle; batch 8
(2026-05-14) shipped the hierarchical render contract for
Milestone 4 prep — cameras, lights, per-pass draw bins, debug
geometry, the `buildRenderFrame` lifecycle hook, visibility-
culling helpers, an instance-buffer layout helper, and a per-frame
upload ring. All seven were pure additions to the public API; the
only removal was an internal dead field on `CmdSpawn` (batch 3).
Detailed per-feature notes live in `doc/` and `CLAUDE.md`.

Note on numbering: batch 6 (archetype storage) is still planned in
§3.1. Batches 7 and 8 landed first because §3.1's own deferral
guidance ("intentionally deferred until the renderer-side and
resource-side contracts have stabilized") pointed at doing the
resource and render batches before the archetype refactor. The
batch numbers reflect the *planned* sequence, not the order they
shipped.

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
- a real renderer to prove the contract — Vulkan, per §3.5.

The batches are sized like prior batches (5–9 additive items each),
sequenced so each one is shippable on its own and the public API
grows monotonically. The mapping to milestones (§8):

| Batch | Theme                              | Milestone(s)          |
|-------|------------------------------------|-----------------------|
| ~~4~~ | ~~Observability + small Milestone-1 polish~~ | ✅ landed 2026-05-14 — see §2 |
| ~~5~~ | ~~Data model widening~~            | ✅ landed 2026-05-14 — see §2 |
| 6     | Archetype/chunk storage            | M2                    |
| ~~7~~ | ~~Resource & event maturity~~      | ✅ landed 2026-05-14 — see §2 |
| ~~8~~ | ~~Render contract expansion~~      | ✅ landed 2026-05-14 — see §2 |
| 9     | Vulkan reference renderer (example) | M4                    |
| 10    | 3D RPG demo example                | M6 lead-in            |

§3.5 covers the Vulkan defaulting strategy across batches 8–10.

### 3.1 Batch 6 — Archetype / chunk storage (Milestone 2)

Single big refactor; previously gated on batch 5's mask widening + the
N-tick determinism harness (both landed 2026-05-14), on the public
resource/event surface being stable (batch 7 landed 2026-05-14), and
on the render-contract surface being stable (batch 8 landed
2026-05-14). With all three clear, the archetype refactor is now
fully unblocked. Effort sized at ~3 weeks.

- **Chunk-based `EntityStorage`.** Replace the parallel `std::vector`
  per component with archetype chunks (e.g. 256 entities per chunk,
  one chunk per unique component-set). Within a chunk, dense arrays
  live contiguously per component for cache locality.
- **Preserve the public dense-span surface where possible.** The
  flattened `transforms()` / `velocities()` accessors stay (returning
  a view over a temporary stitched span only when callers ask), but
  add `forEachChunk<T...>(SystemContext&, fn)` for systems that want
  the fast path. Document the perf delta in
  `doc/components_and_queries.md`.
- **Archetype-aware spawn/destroy.** `CommandBuffer::spawn` derives
  the target archetype from the bundle's mask; `destroy` may swap to
  a different archetype (for component remove flows).
- **`addComponent<T>` / `removeComponent<T>`** on `CommandBuffer`
  — today the only way to "add" a component is to set it during
  spawn. Archetype storage makes the per-entity transition explicit.
- **Migrate `forEachWith<...>` to iterate chunks whose mask is a
  superset.** Drop the per-entity mask test inside the hot loop.
- **Stress test.** `tests/archetype_storage_stress_test.cpp` —
  spawn/destroy/component-flip 1M entities; assert no leaks, no
  determinism drift against batch 5's `determinism_golden_test`
  baseline.
- **`UserComponent<T>` extension hook** (deferred from batch 5).
  A header-only `template<class T>` mechanism that lets game code
  declare additional dense arrays under engine-managed life cycle,
  parallel to the built-ins. The archetype chunk shape is the
  right home for this — each archetype already needs a
  type-erased per-component array; user components extend the
  registry the chunks key into. Trade-off: discoverability vs.
  lock-in.

This is the most disruptive batch in the plan. The interleaving
question that was open in the prior revision — whether to do the
render-contract expansion first — was answered "yes": batch 8
shipped 2026-05-14 (see §2), settling the public render surface
ahead of this refactor. With both the resource/event and render
contracts stable, the archetype refactor is the only remaining
Milestone 2 work and can land without churning the public API
again.

### 3.2 Batch 9 — Vulkan reference renderer (Milestone 4)

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

### 3.3 Batch 10 — 3D RPG demo example (Milestone 6 lead-in)

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

### 3.4 Items intentionally NOT in §3

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

### 3.5 Vulkan as the implicit default for M4+

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

- **Archetype/chunk storage.** Now §3.1 batch 6. Still a deep
  refactor of `EntityStorage`. Sequencing matters: it should not
  happen until the public surface (queries, events, resources,
  renderer contract) is settled — otherwise the API churns twice.
  Today the per-entity `ComponentMask` (16 bits allocated, 48 spare
  after batch 5) serves the immediate need (presence filtering);
  the archetype refactor is a perf play, not a correctness play.
- **Frame task graph.** A useful Phase-3-of-§6 win; smaller
  bang-for-buck than the JobSystem rewrite (done). Once intra-system
  parallelism is the bottleneck (currently it isn't), this becomes
  the right next move.

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

### Phase 3 — frame task graph

After Phase 2's primitives are in, layer an explicit DAG on top of
the wave scheduler: systems optionally declare `depends_on(name)` /
`provides(name)` so the engine can schedule producer-consumer pairs
in the same wave when reads/writes alone don't capture the order.

### Phase 4 — cancellation and budgets

Frame cancellation, streaming cancellation, budget-based task
scheduling, job prioritization.

### Phase 5 — reduce contention in storage

Read-only snapshots, double/triple buffering where useful, append-
only command streams. The per-worker scratch arena from batch 2 is a
down payment.

### Phase 6 — measure everything

Job duration histograms (✅ batch 4), Chrome-trace adapter
(✅ batch 4), hot system ranking, cache-friendly batch sizes,
allocation counters, frame hitches. The core instrumentation primitives
are now all in place; what's missing is in-game ingestion.

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
- Archetype storage + `forEachChunk` + `UserComponent<T>` —
  §3.1 batch 6.
- Pass-aware `RenderFrame` + `buildRenderFrame` hook + render
  helpers (Camera, Light, DrawItem, Visibility, InstanceBufferLayout,
  UploadRing) — ✅ batch 8.
- Networking deltas, task graph, cancellation — deferred to Phase 3+
  of §6.

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

### Milestone 2 — Data model upgrade

Widened `ComponentSet`, additional engine-known component slots,
archetype/chunk storage, `forEachChunk`, determinism golden tests.
Batch 5 (✅) shipped the mask widening, six new POD components,
three tag-only categories, the `MaskCache` opt-in, and the N-tick
determinism golden test. §3.1 batch 6 (archetype storage,
`forEachChunk`, `UserComponent<T>`) closes the milestone.

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
`examples/vulkan_renderer/` (§3.2 batch 9) is the remaining piece.

### Milestone 5 — Task graph and deep parallelism

Explicit DAG, intra-system cancellation, priorities. Deferred to
Phase 3 of the perf roadmap.

### Milestone 6 — RPG feature readiness

Serialization (✅ batch 4), navigation, animation, physics,
networking. The "endgame" — each is a sibling sub-project on its
own. `examples/rpg_demo/` (§3.3 batch 10) is the integration proof.

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
widening that the §3.1 archetype refactor depends on is in
(batch 5); "stream assets asynchronously" is in (batch 7 —
multi-stage loader pipeline, hot reload, refcounted handles,
blocking preload); the hierarchical render contract that
`examples/vulkan_renderer/` will consume is in (batch 8 — cameras,
lights, per-pass bins, debug overlay, `buildRenderFrame` hook,
visibility helpers, instance/upload helpers); the rest depends on
§3 batches 6, 9–10.

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
RAII event subscriptions), and the batch-8 render contract
(hierarchical `RenderFrame`, `buildRenderFrame` hook, Camera /
Light / DrawItem / debug PODs, visibility helpers, instance
buffer layout, upload ring) confirm that the layering is sound —
every one landed as a pure addition without churning the public
API.

With the resource/event and render-contract surfaces now stable, the
most disruptive remaining refactor (archetype storage, §3.1 batch 6)
is unblocked. The §3 plan continues to treat the Vulkan reference
renderer as an example, not a library dependency. That keeps the
renderer-agnostic guarantee intact while still letting M4+ work
target a real, modern API.
