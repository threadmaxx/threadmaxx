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

Last refreshed: 2026-05-13.

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

The five-item near-term plan from the previous revision is complete.
Brief notes here so the next plan doesn't re-propose them:

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

These were pure additions: no breaking changes to the public API.

## 3. Concrete near-term plan

In priority order, smallest and most useful first. As before, these are
achievable without breaking the existing public contract.

### 3.1 System lifecycle: `preStep` / `postStep` hooks

Today `ISystem` has `onRegister` / `onUnregister` (one-shots) and
`update` (per-tick parallel). Several legitimate uses fall in the gap:

- Pumping a system's own input queue before the wave kicks off.
- Snapshotting per-tick state for a HUD or trace consumer.
- Resetting per-tick scratch state that `update` will fill.

Add two non-parallel hooks, both called on the simulation thread, both
optional with empty defaults:

```cpp
class ISystem {
    virtual void preStep(SystemContext&)  {}   // runs before any wave starts
    virtual void postStep(SystemContext&) {}   // runs after the last commit
    // ...
};
```

Order: all `preStep` in registration order, then waves, then all
`postStep` in registration order. No scheduling — these run serially.
The cost is one function-pointer call per system per hook per tick; for
systems that don't override, it's a no-op.

### 3.2 Per-job scratch arena

Today every parallel job that needs temporary memory allocates with
`std::vector` and friends — paid every tick. Add a thread-local bump
arena that the engine resets between waves and hands to each job
alongside the `CommandBuffer`:

```cpp
ctx.parallelFor(N, 0, [=](Range r, CommandBuffer& cb, ScratchArena& arena) {
    auto* tmp = arena.allocate<NeighborList>(r.size());
    // ... use tmp, no destructor needed: arena reclaims at wave end ...
});
```

Sizing: a few KiB per worker by default, growable via `Config` if a
system asks for more. The arena resets at wave boundaries so jobs in
later waves get a fresh slate; the same allocations don't survive across
ticks. This is small, isolated, and a real win for query-style systems
(neighbor lists, sort buffers).

### 3.3 Typed event channels

A small `EventChannel<T>` API for cross-system messaging that's
discoverable and lock-free for the common case. Shape:

```cpp
template <typename Ev>
class EventChannel {
public:
    void emit(const Ev&);                     // safe from worker jobs
    std::span<const Ev> drainTick() const;    // visible to systems next tick
};
```

Engine owns the channels (`engine.events<DamageEvent>()`). Per-tick
double-buffer: writers append to the back buffer during this tick,
readers see the previous tick's front buffer. Drain happens on tick
boundary. Lock-free producer via per-worker append-buffers that the
engine concatenates during commit. Sized like `ResourceRegistry`: small
public surface, internal complexity hidden.

This unblocks combat hits, quest triggers, animation events, audio
cues, UI updates — without each game inventing the same wheel.

### 3.4 Pause + time-scale

A trivial-looking knob that's surprisingly load-bearing for pause
menus, slow motion, and replay. Two additions:

- `Engine::setTimeScale(double)` — multiplies the fixed-step delta that
  systems see via `SystemContext::dt()`. Default `1.0`. The engine still
  advances by exactly `fixedStepSeconds` of wall-clock per `step()`;
  what changes is what game logic computes from `dt`. `tick()` always
  increments by 1.
- `Engine::setPaused(bool)` — when paused, `run()` still pumps but no
  `step()` is executed; `submitFrame` is called once per outer iteration
  with the current world so the renderer keeps drawing.

Substepping (multiple fixed steps per render frame for physics) is a
separate, larger lift — parked.

### 3.5 Reserved spawn handles

Today `cb.spawn(...)` doesn't tell the recording job what handle was
assigned — it's allocated during commit. So a system that wants to
spawn a parent and a child in the same job has to do it in two ticks or
inside one `ctx.single()` block.

Fix: a "reserve handle" call that allocates a generation-tagged slot up
front (under a single fast mutex on the storage), with commit later
materializing the spawn:

```cpp
EntityHandle parent = ctx.reserveHandle();
EntityHandle child  = ctx.reserveHandle();
cb.spawn(parent, Transform{}, ...);
cb.spawn(child,  Transform{}, ..., Parent{ parent, ... });
```

Reserved-but-not-spawned handles must be reapable so a job that bails
doesn't leak slots. The simplest implementation: reservations get
cleaned up at the end of commit if no `CmdSpawn` matched.

### 3.6 The unglamorous wins

Smaller items that should land alongside the above:

- Extend `Query.hpp::detail::getSpan` and `componentBit` to cover
  `Acceleration` and `Parent` (currently `forEach<Acceleration>` doesn't
  compile). One-liner each.
- A `commitDurationSeconds` field on `EngineStats` so users can see how
  much of the tick is wave-execution vs. commit.
- Per-worker steal/own-pop counters on `EngineStats` (or a dedicated
  `JobSystemStats`) — useful for tuning grain.

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
helpers. Deferred until the public API has settled.

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

### Milestone 1 — Hardening the current core  (in progress)

Done:

- Per-system instrumentation.
- Sharded job queue.
- Per-entity component-presence mask.
- Hierarchy system.
- Typed resource registry.

Remaining:

- Lifecycle hooks (preStep / postStep).
- Scratch arenas.
- Event channels.
- Pause / time-scale.
- Reserved spawn handles.

Exit criteria: every item in §3 above lands with tests; public API
stable enough that a small game can ship against it without patching
the engine.

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
