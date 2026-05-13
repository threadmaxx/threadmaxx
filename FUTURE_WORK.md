# threadmaxx — Future Work

This document is the planning guide for extending `threadmaxx` from an
early renderer-agnostic backend into a production-ready library suitable
for a 3D RPG. It is written in a Claude Code–friendly style: practical,
phased, and implementation-oriented.

The document tracks three things:

1. What's recently been built (so the roadmap stays honest).
2. The concrete next batch — small, additive, achievable.
3. The longer-term shape of the library and the items deliberately
   parked or scoped out.

Last refreshed: 2026-05-13 (third pass of the day; the §3 batch has
now also landed — see §2 third batch).

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
PImpl isolation. The missing work is mostly breadth, scalability, and
production-level ergonomics.

## 2. What landed recently (2026-05-13)

Two batches landed today. The earlier five-item plan (top of the list)
and a follow-up five-item plan plus the small-wins set (just below).

### First batch
- **Per-system timing and command stats.** `Stats.hpp` defines
  `SystemStats`; `Engine::systemStats()` returns one entry per system in
  registration order. Same 1/16 EWMA as `EngineStats::avgStepSeconds`.
  Documented in [`doc/stats_and_profiling.md`](doc/stats_and_profiling.md).
- **Sharded / work-stealing job queue.** `JobSystem` rebuilt around
  per-worker deques with `try_lock` victim selection. No more single hot
  mutex. Stress test in `tests/job_system_stress_test.cpp`.
- **Per-entity `ComponentMask` + presence-aware queries.**
  `ComponentSet` is both a scheduling-category bitset and a per-entity
  presence mask. `CommandBuffer::spawn` derives a default mask;
  `setRenderTag`/`setParent` keep it in sync. `forEachWith<...>` filters
  by mask without sentinel checks.
- **`Parent` component + hierarchy system.** Built-in
  `makeHierarchySystem()` propagates world transforms via DFS-with-
  memoization in one pass.
- **Typed `ResourceId<T>` + `ResourceRegistry`.** Type-erased internal
  store keyed by `std::type_index`; engine owns one registry; safe from
  any thread.

### Second batch (just landed)
- **`preStep` / `postStep` hooks.** Virtual on `ISystem`, called in
  registration order on the sim thread, serially. `preStep` commits
  before any wave runs (so wave systems see them); `postStep` runs
  after the last commit (next tick's `preStep` sees them). Documented
  in [`doc/lifecycle_hooks.md`](doc/lifecycle_hooks.md).
- **Per-job `ScratchArena`.** Bump allocator paired with `CommandBuffer`
  on the new three-arg `parallelFor` / `single` overloads. Chained
  slabs grow on demand; reset between waves keeps storage. Trivially-
  destructible types only. See
  [`doc/scratch_arenas.md`](doc/scratch_arenas.md).
- **Typed `EventChannel<T>`.** Engine-owned channels keyed by
  `std::type_index`; thread-safe `emit()` under a per-channel mutex,
  per-tick double-buffer flipped by the engine, `drainTick()` returns
  the previous tick's events. `Engine::events<T>()` is the lazy
  factory. See [`doc/events.md`](doc/events.md).
- **`setTimeScale` / `setPaused`.** Time-scale multiplies `dt` seen by
  systems; tick and `simulationTime` advance by the fixed step
  regardless. Pause makes `step()` a no-op while `run()` keeps
  re-submitting the current render frame. Negative scale clamps to
  zero. See [`doc/pause_and_time_scale.md`](doc/pause_and_time_scale.md).
- **Reserved spawn handles.** `Engine::reserveEntityHandle()` and
  `SystemContext::reserveHandle()` allocate a slot under a fast
  storage-side mutex; the matching `CommandBuffer::spawn(handle, ...)`
  overloads materialize the reservation during commit. Unconsumed
  reservations are reaped at step end. See
  [`doc/reserved_handles.md`](doc/reserved_handles.md).
- **Small wins.** `Query.hpp::forEach<Acceleration>` /
  `forEach<Parent>` now compile (extended `getSpan` / `componentBit`).
  `EngineStats::commitDurationSeconds` exposes commit-phase wall-clock.
  `JobSystemStats` (via `Engine::jobSystemStats()`) reports total
  jobs, own-pops, and steal counts for tuning grain.

All two batches were pure additions: no breaking changes to the public
API. (`CmdSpawn::outHandle` was dead code internally and was removed in
the second batch as part of replacing it with the cleaner `reserved`
field.)

### Third batch (just landed)
- **`World::has<T>` / `World::get<T>`.** Header-only sugar in
  `World.hpp`; consults the per-entity component-presence mask. Pure
  addition; type dispatch via the same `if constexpr` chain shape as
  `Query.hpp::detail::componentBit`. See
  [`doc/world_has_get.md`](doc/world_has_get.md).
- **`Bundle` + `cb.spawnBundle`.** Variadic `bundle(Cs...)` factory
  yields a `Bundle` with a compile-time-derived presence mask, fed to
  `CommandBuffer::spawnBundle`. Named distinctly from `spawn` so
  `cb.spawn({})` stays unambiguous. See
  [`doc/bundles.md`](doc/bundles.md).
- **`registerSystemAt(position, system)`.** Inserts at a specific
  registration index, clamped to the current count. Used by tests and
  mod loaders.
- **`reserveEntityHandles(count, span)` batch form.** Engine-level and
  `SystemContext`-level; one mutex acquisition, fills the supplied
  span.
- **Parent auto-derive in default `spawn`.** The default-mask
  `spawn(...)` and `spawn(handle, ...)` overloads now also derive the
  Parent presence bit from `p.parent.valid()` (mirroring the existing
  RenderTag derive). Both overloads take an optional trailing `Parent`
  parameter.
- **`frameSnapshot()` + JSON Lines `writeJsonLines`.** Bundled
  `FrameSnapshot{EngineStats, std::span<const SystemStats>,
  JobSystemStats}` and a header-only serializer in `Trace.hpp`. See
  [`doc/tracing.md`](doc/tracing.md).
- **`IResourceLoader` contract.** `Engine::addResourceLoader` registers
  a loader; the engine pumps `update(engine)` once per `step()` on the
  sim thread, after `postStep` commits and before the reservation
  reap. Loaders are torn down in reverse-registration order during
  `shutdown()`. See
  [`doc/resource_loaders.md`](doc/resource_loaders.md).
- **`SpatialHash<Payload>` helper.** Header-only uniform-grid index;
  not thread-safe; engine does not own one. See
  [`doc/spatial_hash.md`](doc/spatial_hash.md).

All three batches are pure additions. The public surface gained
`Trace.hpp` and `SpatialHash.hpp` headers; everything else extended
existing headers.

## 3. Concrete near-term plan

The §3 batch (3.1 – 3.6) below has fully landed (see §2 third batch).
The next batch picks up from §4 / §7 — items previously underestimated
or parked — plus the small wins that the third batch didn't sweep up.

### 3.7 Next batch (proposed)

Pure-additive items, sized like the previous batches. In priority
order:

- **Serialization trait hook (§7.8).** A trait pair
  `serialize(Component&)` / `deserialize(Component&)` per built-in
  component, plus a `World::snapshot()` that captures the dense arrays.
  Header-only sugar over the storage that the engine already exposes;
  migration support stays game-side.
- **Per-job-duration histogram in `JobSystemStats`.** Bucketed
  histogram (8–16 bins, log-spaced) of individual job durations. Use
  cases: detect grain mis-tuning, see "is one job dominating?". Cheap
  to maintain (per-worker accumulators, merged on read).
- **Chrome-trace adapter.** A second serializer alongside
  `writeJsonLines` that emits one `{ph:"X", ...}` record per system per
  tick. Build on top of `frameSnapshot()` — no new instrumentation
  surface.
- **`Engine::events<T>().subscribe(fn)`.** Persistent subscription
  helper on top of the existing event channels: keep a list of
  callbacks invoked at drain time. Today users call
  `events<T>().drainTick()` from a postStep hook themselves; a
  subscribe shortcut is sugar.
- **`HierarchySystem` configuration.** Expose a knob for whether scale
  chains (today: never; some games want it sometimes). Default off
  preserves current semantics.

§3.1 – §3.6 below are the prior-batch contents, kept for reference.

### 3.1 `World::has<T>` and `World::get<T>` ✅ done (third batch)

Header-only sugar on top of the existing `tryGet*` accessors and the
component-presence mask:

```cpp
template <typename T> bool      World::has(EntityHandle) const noexcept;
template <typename T> const T&  World::get(EntityHandle) const;
```

`has<T>` is a one-liner: check the mask, then check the type. `get<T>`
returns a reference (asserts on absent) — useful where a `nullptr`
check is noise. Pure addition; type dispatch via the same `if constexpr`
chain that lives in `Query.hpp::detail::componentBit`.

### 3.2 Frame snapshot for tracing ✅ done (third batch)

A `Engine::frameSnapshot()` (or a hook on `IRenderer`) that emits the
current tick's per-system durations and command counts as a tagged
record. Wire into a Chrome-trace or Tracy consumer with a one-line
adapter. The instrumentation hooks (`SystemStats`, `EngineStats`,
`JobSystemStats`) are already there; what's missing is a serializer
and a sink.

The right scope is small: emit JSON-lines records with the same field
names that the snapshots already have, leave aggregation to whatever
the user pipes them into.

### 3.3 Async resource loader contract ✅ done (third batch)

The existing `ResourceRegistry` is the in-memory side; the missing
piece is the loader. Concretely:

```cpp
class IResourceLoader {
public:
    virtual ~IResourceLoader() = default;
    virtual void update(Engine&) = 0;   // called once per tick during postStep
};

void Engine::addResourceLoader(std::unique_ptr<IResourceLoader>);
```

The engine doesn't spawn threads for the loader — it pumps `update()`
on a designated postStep slot. The loader implementation owns its own
I/O pool and calls `engine.resources().add(...)` when an asset is
ready. This keeps the engine renderer-agnostic and asset-format-
agnostic; it provides the *contract* and the tick-pump hook.

Hot-reload becomes the same contract with a second method
(`reload(ResourceId)` / `markStale(ResourceId)`).

### 3.4 Spatial-hash helper ✅ done (third batch)

Spatial queries (neighbor lookups, broadphase, AOI for streaming)
recur across every gameplay system. The engine can host a generic
uniform-grid + small fallback list, parameterized over component
type:

```cpp
template <typename T>
SpatialIndex<T> Engine::spatialIndex();  // engine-owned, rebuild per-tick
```

The implementation is straightforward — what makes this a library
concern (not a per-game concern) is consistent integration with the
wave scheduler and the per-job scratch arena. The grid rebuild can
land in a `preStep` hook on a built-in system, scoped by the
component the user is indexing.

This unblocks: spatial AI queries, navigation costing, range-based
combat targeting, distance culling.

### 3.5 `Bundle` / archetype-lite ✅ done (third batch)

Without the full archetype refactor, a smaller win is a `Bundle<...>`
helper that packages a set of components and their masks for spawning:

```cpp
auto enemy = bundle<Transform, Velocity, RenderTag>(t, v, r);
cb.spawn(enemy);
```

The compile-time component list yields a constexpr `initialMask`; the
runtime cost is zero compared to today's manual `seed.spawn(t, v, r,
...)`. Bundles are *recording-side* sugar — no engine internals
change.

### 3.6 The unglamorous wins ✅ done (third batch)

Smaller items that should land alongside the above:

- Tracy / Chrome-trace integration as a header-only adapter on top of
  the existing stats structs.
- A `Engine::registerSystemAt(position, ...)` for tests / mod loaders
  that need to inject a system at a specific point in the registration
  order.
- A `tryReserveHandles(count)` batch form to amortize the reservation
  mutex when a job spawns many entities at once.
- Promote `Parent` presence to be auto-derived in
  `CommandBuffer::spawn(...)` like `RenderTag` already is.

## 4. Items that the previous plan got right but underestimates the cost of

- **Archetype/chunk storage.** Still listed as a milestone. Still a
  deep refactor of `EntityStorage` that changes the meaning of every
  dense span the engine hands out. Sequencing: it should not happen
  until the public surface (queries, events, resources) is settled —
  otherwise the API churns twice. Today the per-entity `ComponentMask`
  serves the immediate need (presence filtering); the archetype refactor
  is a perf play, not a correctness play.
- **Frame task graph.** A useful Phase 3 win; smaller bang-for-buck
  than the JobSystem rewrite (now done). Once intra-system parallelism
  is the bottleneck (currently it isn't), this is the right next move.

## 5. Out of scope for `threadmaxx` itself

These are good items for a *game* built on `threadmaxx`, but baking
them into the backend would either (a) tie the library to a specific
third-party implementation, or (b) bloat the public surface past the
"small, stable contract" principle. The right shape for the engine is
to provide hooks (component categories, event channels, frame-late
callbacks) rather than ship the systems themselves:

- **Networking, replication, rollback.** Belongs above the engine. The
  engine should at most provide deterministic commit + stable entity
  IDs (both already true) so a game can layer its own snapshot/delta
  logic.
- **Animation systems / IK / cloth / blend trees.** A real animation
  pipeline depends on the renderer's skinning model and the asset
  format. The engine can host these as user systems once a `Skeleton` /
  `AnimationState` component shape exists, but it should not own the
  math.
- **Physics integration (broadphase / narrowphase / rigid body).** Same
  reasoning — Bullet / Jolt / PhysX each impose a world ownership model
  incompatible with hardcoding one. A `PhysicsBody` component category
  and read-only world snapshot pattern is the engine's job; the solver
  is not.
- **Audio mixing / 3D audio.** Wholly orthogonal to a game backend.
- **Save/load + migration.** A serialization *hook* on components is in
  scope (a single trait function pair); a full versioned migration
  system is not.
- **Navmesh / pathfinding.** Belongs in a domain library; the engine
  only needs to allow background work to be scheduled.
- **Editor/tooling/hot-reload.** Out of scope until the public API has
  stabilized.

## 6. Multithreading and performance roadmap

The previous roadmap's phases still describe the right arc; what
changed is that Phase 1 is done.

### Phase 1 — stabilize the current job model  ✅ done

- Per-worker work-stealing deques.
- Atomic outstanding counter with last-decrement-notify.
- Decoupled `waitIdle` synchronization.
- Determinism preserved (commit order unchanged).

### Phase 2 — split system work into finer tasks (current)

The wave scheduler buys parallelism *between* systems. The next axis
is finer slicing *within* a system: per-chunk iteration is already
expressible via `parallelFor`, but the engine doesn't yet help with:

- per-archetype iteration (waiting on the archetype refactor),
- per-region iteration (spatial hashing — game-side for now),
- per-animation-graph iteration (waiting on animation hooks),
- per-visible-set iteration (waiting on render-frame structure).

The near-term plan's items (scratch arena, event channels) move in this
direction without committing to a specific axis.

### Phase 3 — add frame task graphs

After Phase 2's primitives are in, layer an explicit DAG on top of the
wave scheduler: each system optionally declares `depends_on(name)` /
`provides(name)` so the engine can schedule producer-consumer pairs in
the same wave when reads/writes alone don't capture the order. Today
the wave scheduler's R/W rule already handles the common cases; the
graph is for the corners.

### Phase 4 — cancellation and budgets

Frame cancellation, streaming cancellation, budget-based task
scheduling, job prioritization. Example: if a new camera position
invalidates an old culling job, the engine should drop it early.
Useful but not blocking anyone today.

### Phase 5 — reduce contention in storage

The archetype/chunk refactor. Read-only snapshots. Double/triple
buffering where useful. Append-only command streams. Per-worker
scratch arenas (covered above).

### Phase 6 — measure everything

Job duration histograms, hot system ranking, cache-friendly batch
sizes, allocation counters, frame hitches. Most of this is now
*possible* (the instrumentation hooks exist); what's missing is
ingestion. A `tracy` hook would be the natural integration once
`EngineStats` extensions land.

## 7. Public API extensions still on deck

### 7.1 World and entity querying

The query helpers cover most needs; what's missing is sugar for the
"has T?" / "get T as optional" pattern:

- `World::has<T>(EntityHandle)` — wraps the `tryGet` / mask-check pair.
- `World::get<T>(EntityHandle)` — same but asserts on absent.

Both are header-only on top of the existing accessors. Pure addition.

### 7.2 Component and archetype model

Deferred. The per-entity mask covers presence without the refactor cost.

### 7.3 Resource/asset system

`ResourceRegistry` is the minimum useful surface. Future extensions:

- Async loader contract: the engine provides a `IResourceLoader`
  interface that game code can register against. The engine does not
  spawn threads for it — the loader uses its own pool and calls `add()`
  when ready.
- Hot reload: an `update()` method on the loader that the engine pumps
  during a designated postStep. Out of scope until 7.1 lands.

### 7.4 Event / message system

Covered in §3.3.

### 7.5 Job and scheduling API

A public task interface beyond `parallelFor` (futures, continuations,
cancellation tokens). Deferred until intra-system parallelism is the
proven bottleneck.

### 7.6 Timing and simulation controls

Pause / time-scale covered in §3.4. Substepping deferred.

### 7.7 Render abstraction expansion

A flat `RenderFrame` is fine for the minimal backend; a 3D engine
usually needs visibility lists, draw-item bins by pass, light lists,
camera data, post-processing inputs, debug overlay layer. The right
shape is probably a hierarchical `RenderFrame` with per-pass slots
that user-side "render prep" systems populate before the renderer is
called. Design problem more than implementation problem; deferred
until a real 3D renderer is targeting threadmaxx.

### 7.8 Serialization and save/load

A trait pair `serialize(Component&)` / `deserialize(Component&)` per
component, plus a `World::snapshot()` that captures the dense arrays.
Migration support stays game-side. Sized like 7.1 — additive header
helpers. Now listed as a §3.7 candidate; deferred again only if a
bigger refactor (archetype storage) ships first.

### 7.9 Networking support

The engine already provides deterministic commit and stable entity
IDs. The next step is a non-mandatory "publish per-tick delta"
extension point; deferred until a real networking client is
targeting threadmaxx.

### 7.10 Debug/profiling API

Scoped profiling markers, job-timing stats, per-system timing stats,
queue-depth stats, frame breakdowns. Per-system stats are in; queue-
depth and per-job histograms are §3.6.

## 8. Roadmap milestones

### Milestone 1 — Hardening the current core  ✅ done

Done:

- Per-system instrumentation + `JobSystemStats`.
- Sharded job queue.
- Per-entity component-presence mask.
- Hierarchy system.
- Typed resource registry.
- Lifecycle hooks (`preStep` / `postStep`).
- Scratch arenas.
- Typed event channels.
- Pause / time-scale.
- Reserved spawn handles + batch reserve.
- Commit-phase timing in `EngineStats`.
- `World::has<T>` / `World::get<T>`.
- `Bundle` + `cb.spawnBundle`.
- `Engine::registerSystemAt`.
- `frameSnapshot` + JSON-Lines tracing.
- `IResourceLoader` contract + per-tick pump.
- `SpatialHash<Payload>` helper.
- Parent auto-derive in default `spawn`.

Exit criteria met: a small game can ship against the current public
API without patching the engine. The next milestone is data-model
upgrades (archetypes) and the §3.7 next-batch items (serialization
hook, job-duration histograms, Chrome-trace adapter, event
subscriptions).

### Milestone 2 — Data model upgrade

Archetype/chunk storage, presence-typed queries, improved cache
locality. Deferred until Milestone 1 ships.

### Milestone 3 — Resource and event layers

Async loader contract, hot-reload, persistent event subscriptions.
Once Milestone 1's event channels exist, this is incremental.

### Milestone 4 — Rendering contract expansion

Pass-aware `RenderFrame`. Deferred; depends on a concrete renderer.

### Milestone 5 — Task graph and deep parallelism

Explicit DAG, intra-system cancellation, priorities. Deferred to
Phase 3 of the perf roadmap.

### Milestone 6 — RPG feature readiness

Serialization, navigation, animation, physics, networking. The
"endgame" — each is a sub-project on its own.

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

Today: items 5 and 7 are in; the rest are ahead. Milestone 1
completion gets us to "good enough for a 2D-or-light-3D game". The
heavier 3D RPG bar is Milestone 4+.

## 11. Final note

The current architecture is a solid foundation, and the recent additions
(component masks, hierarchy, resources, work-stealing queue, per-system
stats) confirm that the layering is sound — each landed as a pure
addition without churning the public API. The future work is mostly
about turning a good internal backend into a fully usable engine
library: richer APIs, more scalable tasking, better world
representation, and support for the systems a real 3D RPG actually
needs.
