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

Last refreshed: 2026-05-13 (Milestone 1 complete; chapter 3 rewritten
into a forward plan toward Milestones 2–4).

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

Three batches landed on 2026-05-13, bringing **Milestone 1** to
completion. All three were pure additions to the public API; the only
removal was an internal dead field on `CmdSpawn`. Detailed per-feature
notes live in `doc/` and `CLAUDE.md`.

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
else extended existing headers. 28 tests pin the documented
invariants.

## 3. Planned batches — the road to Milestones 2–4

This is the forward plan. The goal at the end of §3 is "a 3D RPG
example can be developed on top of threadmaxx without patching engine
internals." That requires three things the library does not yet have:

- a wider data model (more component slots, archetype-style storage),
- a richer rendering contract (passes, cameras, lights, skinned poses),
- a real renderer to prove the contract — Vulkan, per §3.9.

The batches are sized like prior batches (5–9 additive items each),
sequenced so each one is shippable on its own and the public API
grows monotonically. The mapping to milestones (§8):

| Batch | Theme                              | Milestone(s)          |
|-------|------------------------------------|-----------------------|
| 4     | Observability + small Milestone-1 polish | M1 polish + M3 lead-in |
| 5     | Data model widening                | M2 prep               |
| 6     | Archetype/chunk storage            | M2                    |
| 7     | Resource & event maturity          | M3                    |
| 8     | Render contract expansion          | M4 prep               |
| 9     | Vulkan reference renderer (example) | M4                    |
| 10    | 3D RPG demo example                | M6 lead-in            |

§3.9 covers the Vulkan defaulting strategy across batches 8–10.

### 3.1 Batch 4 — Observability and small wins

Pure-additive, sized like batches 1–3. Closes out Milestone 1 polish
and seeds the tracing/event maturity that batch 7 builds on.

- **Serialization trait hook (§7.8).** A trait pair
  `serialize(Component&)` / `deserialize(Component&)` per built-in
  component, plus `World::snapshot()` that captures the dense arrays.
  Header-only sugar on top of the storage the engine already exposes;
  migration support stays game-side.
- **Per-job-duration histogram in `JobSystemStats`.** 8–16 log-spaced
  bins of individual job durations. Per-worker accumulators merged on
  read. Detects grain mis-tuning and "is one job dominating?".
- **Chrome-trace adapter.** A second serializer alongside
  `writeJsonLines` (one `{ph:"X", ...}` record per system per tick).
  Built on `frameSnapshot()`; no new instrumentation surface.
- **`events<T>().subscribe(fn)`.** Persistent subscription helper on
  top of the existing channels: keep a callback list, invoked at
  drain time. Sugar over today's manual `drainTick()`-in-postStep
  pattern.
- **HierarchySystem scale-chain knob.** Optional config so the user
  can choose whether scale propagates (today: never). Default off
  preserves current semantics.
- **Queue-depth and wait-time fields on `SystemStats`.** Already
  computable from existing primitives; expose them.
- **`ILogger` interface.** Tiny `virtual log(level, message)`; engine
  routes startup/shutdown warnings, system registration messages, and
  loader errors through it. Default is `std::cerr`. Lets games plug
  in their own log sink.

### 3.2 Batch 5 — Data-model widening (Milestone 2 prep)

The current `Component` enum has 7 values. A 3D RPG needs more (health,
faction, animation pose, physics body, AI state, …). Widening the
mask is cheap; it has to happen before §3.3's archetype refactor so
the storage change only happens once.

- **Widen `ComponentSet` to 64 bits.** Switch `Component` underlying
  type to `std::uint64_t`; `ComponentSet::all()` mask widens. Pure
  internal change behind the existing API.
- **Add engine-known component slots as POD storage** (no engine-side
  systems — the engine just hosts the dense arrays):
  - `Health { float current, max; }`
  - `Faction { std::uint32_t id; }`
  - `AnimationStateRef { ResourceId<AnimationGraph> graph; std::uint32_t state; float t; }`
  - `PhysicsBodyRef { std::uint64_t handle; }` (opaque to engine)
  - `NavAgentRef { std::uint64_t handle; }`
  - `BoundingVolume { Vec3 min, max; }` (used by visibility culling in batch 8)
  Each slot follows the same recipe (`CLAUDE.md` §"Adding a new
  built-in component"). They are *categories*, not implementations:
  the engine never integrates them; user systems do.
- **`UserComponent<T>` extension hook.** A header-only `template<class T>`
  mechanism that lets game code declare additional dense arrays under
  engine-managed life cycle, parallel to the built-ins, without
  patching the storage. Trade-off: discoverability vs. lock-in. If
  this turns out to be too invasive, gate it on the archetype refactor
  in §3.3.
- **Tag-only components.** `StaticTag`, `DisabledTag`, `DestroyedTag`
  — presence-only, no data; renderer/systems skip on `DisabledTag`.
  Sized like adding any normal component, just without a dense array.
- **Determinism golden-output test.** `tests/determinism_test.cpp`
  runs a fixed seed scenario for N ticks, hashes the world state, and
  compares to a baseline. Cheap regression guard for the archetype
  refactor (§3.3) and the renderer expansion (§3.4).
- **`forEachWith` mask cache.** Precompute the matching index span
  once at preStep when masks are stable, reuse in `update`. Cheap
  perf win; opt-in flag on the system.

### 3.3 Batch 6 — Archetype / chunk storage (Milestone 2)

Single big refactor; should not happen before §3.2 widens the mask and
§3.1 lands the determinism harness. Effort sized at ~3 weeks.

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
  determinism drift against §3.2's baseline.

This is the most disruptive batch in the plan. It is intentionally
deferred until the renderer-side and resource-side contracts (§3.4,
§3.5) have stabilized so the API doesn't churn twice.

### 3.4 Batch 7 — Resource & event maturity (Milestone 3)

`IResourceLoader` (batch 3) is the contract; this batch adds the
pipeline shape an actual asset stream needs.

- **Multi-stage loader pipeline.** Express load → decode → upload as
  separate stages a loader can advance per tick. The engine still
  pumps `update()` once; the loader chains internally.
- **Reference-counted asset handles.** `ResourceHandle<T>` wraps a
  `ResourceId<T>` with refcount semantics; the registry frees when
  the last handle drops. Hot reload makes a new id; the handle
  redirects.
- **Hot-reload protocol.** `IResourceLoader::markStale(ResourceId)`
  queues a reload; an internal `AssetReloaded` event channel
  publishes the swap so user systems can update render data.
- **Boot-time blocking preload.** `Engine::preloadResources(...)` for
  the splash-screen case: pumps loaders synchronously until a named
  set is ready.
- **`IResourceLoader::onShutdown()`.** Engine calls before destroying
  the loader so in-flight uploads can cancel gracefully.
- **Configurable memory budget per loader.** Loaders surface a
  current/max footprint; the registry reports the aggregate.
- **Persistent event subscription cleanup.** A
  `Subscription` handle returned by `subscribe(fn)` that auto-
  unsubscribes on destruction.

### 3.5 Batch 8 — Render contract expansion (Milestone 4 prep)

Today's `RenderFrame` is a flat instance list. A 3D RPG renderer
needs structure: cameras, lights, draw bins by pass, skinned poses,
debug overlay. The shape is engine-owned, the population is
user-system-owned, the consumption is renderer-owned.

- **Hierarchical `RenderFrame`.** New types under
  `include/threadmaxx/render/`: `Camera`, `Light` (directional /
  point / spot), `DrawItem`, per-pass bins
  (`opaque` / `transparent` / `shadowCasters` / `overlay`), debug
  geometry layer (lines, points, text).
- **New built-in components.** `Camera` (perspective/ortho + view
  matrix), `Light`, `MeshSkinned` (mesh + skeleton handle),
  `AnimationPose` (per-bone transforms, ringbuffered),
  `MaterialOverride` (per-instance params).
- **Render-prep systems.** A new lifecycle hook
  `ISystem::buildRenderFrame(RenderFrameBuilder&)` that runs after
  `postStep` and writes into a per-system slice of the frame.
  Engine merges slices on the sim thread (deterministic, same shape
  as commit). Existing flat `RenderInstance` path stays as the
  default for headless / minimal renderers.
- **Visibility culling stage.** Built-in system that reads `Camera`
  + `BoundingVolume` (from §3.2), writes a per-camera visible set
  used by the renderer.
- **Stable instance-buffer layout helper.** Header-only struct that
  any renderer can copy into a GPU buffer — predictable alignment,
  shader-compatible field order. Lives in
  `include/threadmaxx/render/`; not Vulkan-specific.
- **Per-frame upload ring helpers.** Same idea: shared frame-to-frame
  allocator that any renderer can use without hard-coding a backend.

### 3.6 Batch 9 — Vulkan reference renderer (Milestone 4)

The first concrete renderer that exercises the full §3.5 contract.
**Lives in `examples/vulkan_renderer/`, NOT in the core library** —
that preserves the renderer-agnostic guarantee. The core lib's only
Vulkan-aware concession is the optional helpers in §3.5.

- **`examples/vulkan_renderer/`.** Vulkan 1.3 (dynamic rendering,
  timeline semaphores, sync2), GLFW for window/surface. Treated like
  `examples/boids` — opt-in via CMake, skipped if Vulkan SDK not found.
- **Implements `IRenderer`** against the hierarchical `RenderFrame`:
  multi-camera, per-pass binning, instanced mesh draw, skinned pose
  upload, depth + shadow pass, simple PBR-ish opaque shader, debug
  overlay.
- **Asset loaders.** A `MeshLoader` / `TextureLoader` /
  `ShaderLoader` (compiled SPIR-V at build time via `glslc`) that
  exercise the §3.4 multi-stage pipeline and refcounted handles.
- **Hot reload.** Shader edits trigger SPIR-V rebuild and pipeline
  rebuild via the `AssetReloaded` channel from §3.4.
- **Cross-platform CI.** Build on Linux + Windows runners. macOS via
  MoltenVK marked best-effort.
- **Smoke scene.** Animated character on a lit terrain plane, third-
  person camera, 1k crowd of instanced meshes — proves §3.5's
  contracts under load.

### 3.7 Batch 10 — 3D RPG demo example (Milestone 6 lead-in)

Closes the loop. Built on top of the Vulkan renderer; demonstrates
that a real game can be developed without engine patches.

- **`examples/rpg_demo/`.** A small open scene: terrain, day/night
  cycle, a player, ~50 NPCs with simple behavior trees,
  inventory pickups, save/load, profiling HUD.
- **Exercises every milestone.** Archetype storage (M2), multi-stage
  asset loading + hot reload (M3), pass-aware rendering with skinned
  characters (M4), serialization (batch 4), spatial-hash AOI for
  AI (batch 3), event subscriptions (batch 4), Chrome-trace
  capture (batch 4).
- **No engine patches.** The success criterion is that everything
  lives in `examples/rpg_demo/` plus the public threadmaxx headers
  — if the demo needs an engine change, it goes back through the
  next batch instead of into the example.

### 3.8 Items intentionally NOT in §3

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

### 3.9 Vulkan as the implicit default for M4+

A note on strategy, since the user-facing question came up.

**Yes — designing toward Vulkan as the default reference renderer
for M4+ is the right call, and it stays compatible with the
renderer-agnostic core.** Concretely:

- The `IRenderer` interface and the flat `RenderFrame` are
  renderer-agnostic by construction; Vulkan slots in cleanly as a
  consumer. The §3.5 hierarchical `RenderFrame` is still
  API-agnostic — it speaks in cameras, lights, draw items, and pose
  buffers, not in `VkCommandBuffer`.
- The Vulkan renderer lives in `examples/vulkan_renderer/`, NOT in
  the core library. It is opt-in via CMake and skipped when the
  Vulkan SDK isn't available — the same pattern `examples/boids` uses
  for SDL2 today. Core library users who never touch Vulkan pay zero
  cost.
- Optional shared helpers (instance buffer layout, frame allocator,
  upload-ring scaffolding) live in `include/threadmaxx/render/` and
  are renderer-neutral. Any backend (Vulkan, WebGPU via Dawn, D3D12,
  Metal via MoltenVK) can use them.
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
addition in §3.5 is justified by a non-Vulkan-specific use case
first.** If a feature only makes sense for Vulkan, it belongs in
`examples/vulkan_renderer/`, not in the core. That keeps the door
open for a WebGPU, D3D12, or even a pure-software reference renderer
later without API churn.

## 4. Items the previous plan got right but underestimates the cost of

- **Archetype/chunk storage.** Now §3.3 batch 6. Still a deep
  refactor of `EntityStorage`. Sequencing matters: it should not
  happen until the public surface (queries, events, resources,
  renderer contract) is settled — otherwise the API churns twice.
  Today the per-entity `ComponentMask` serves the immediate need
  (presence filtering); the archetype refactor is a perf play, not a
  correctness play.
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
  `Skeleton` / `AnimationState` component shape exists (§3.2), but it
  should not own the math.
- **Physics integration (broadphase / narrowphase / rigid body).**
  Same reasoning — Bullet / Jolt / PhysX each impose a world
  ownership model incompatible with hardcoding one. A `PhysicsBodyRef`
  component slot and read-only world snapshot pattern is the
  engine's job; the solver is not.
- **Audio mixing / 3D audio.** Wholly orthogonal to a game backend.
- **Save/load migration.** A serialization *hook* on components is in
  scope (§3.1); a full versioned migration system is not.
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
expressible after §3.3, and per-render-pass iteration becomes
expressible after §3.5.

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

Job duration histograms (§3.1), Chrome-trace adapter (§3.1), hot
system ranking, cache-friendly batch sizes, allocation counters,
frame hitches. Most of this is now *possible*; what's missing is
ingestion.

## 7. Public API extensions still on deck

This section used to enumerate planned API additions. With §3 now
listing them by batch, this is the cross-reference index:

- `World::has<T>` / `World::get<T>` — ✅ batch 3.
- Async loader contract — ✅ batch 3 (basics); §3.4 batch 7 (pipeline).
- Hot reload — §3.4 batch 7.
- Save/load (serialization trait pair) — §3.1 batch 4.
- Tracing / Chrome-trace adapter — §3.1 batch 4.
- Persistent event subscribe — §3.1 batch 4.
- New component slots (Health, Faction, …) — §3.2 batch 5.
- Archetype storage + `forEachChunk` — §3.3 batch 6.
- Pass-aware `RenderFrame` — §3.5 batch 8.
- Job-duration histograms — §3.1 batch 4.
- Determinism golden tests — §3.2 batch 5.
- Networking deltas, task graph, cancellation — deferred to Phase 3+
  of §6.

## 8. Roadmap milestones

### Milestone 1 — Hardening the current core  ✅ done

Per-system instrumentation, sharded job queue, presence mask,
hierarchy, resource registry, lifecycle hooks, scratch arenas, event
channels, pause/time-scale, reserved handles, commit timing,
`has`/`get`, `Bundle`, `registerSystemAt`, `frameSnapshot`,
`IResourceLoader` contract, `SpatialHash`, Parent auto-derive.

Exit criteria met: a small game can ship against the current public
API without patching the engine.

### Milestone 2 — Data model upgrade

Widened `ComponentSet`, additional engine-known component slots,
archetype/chunk storage, `forEachChunk`, determinism golden tests.
Covered by §3.2 batch 5 and §3.3 batch 6.

### Milestone 3 — Resource and event layers

Multi-stage async loader pipeline, refcounted asset handles, hot
reload, boot-time preload, persistent event subscriptions, loader
shutdown contract. Covered by §3.4 batch 7.

### Milestone 4 — Rendering contract expansion + reference renderer

Hierarchical `RenderFrame`, new render-side components (Camera,
Light, MeshSkinned, AnimationPose, MaterialOverride), render-prep
hooks, visibility culling, shared instance/pose buffer helpers.
Vulkan reference renderer as `examples/vulkan_renderer/`. Covered by
§3.5 batch 8 and §3.6 batch 9.

### Milestone 5 — Task graph and deep parallelism

Explicit DAG, intra-system cancellation, priorities. Deferred to
Phase 3 of the perf roadmap.

### Milestone 6 — RPG feature readiness

Serialization (already in M2-era), navigation, animation, physics,
networking. The "endgame" — each is a sibling sub-project on its
own. `examples/rpg_demo/` (§3.7 batch 10) is the integration proof.

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

Today: items "stable serialization" and "deterministic mode
declared" are in (or in batch 4); the rest depends on §3 batches 5–10.

## 11. Final note

The current architecture is a solid foundation, and the Milestone 1
additions (component masks, hierarchy, resources, work-stealing
queue, per-system stats, lifecycle hooks, events, scratch, time
control, tracing, async-loader contract, spatial hash) confirm that
the layering is sound — every one landed as a pure addition without
churning the public API.

The §3 plan deliberately defers the most disruptive refactor
(archetype storage) until the renderer-side and resource-side
contracts have settled, and treats the Vulkan reference renderer as
an example, not a library dependency. That keeps the
renderer-agnostic guarantee intact while still letting M4+ work
target a real, modern API.
