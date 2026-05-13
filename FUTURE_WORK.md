# threadmaxx — Future Work

This document is a planning guide for extending `threadmaxx` from an early renderer-agnostic backend into a production-ready library suitable for a 3D RPG. It is written in a Claude Code-friendly style: practical, phased, and implementation-oriented.

## 1. Target outcome

`threadmaxx` should evolve into a reusable C++20 backend that can support:

* large world simulation,
* streamed 3D environments,
* many AI agents,
* animation and physics at scale,
* networked gameplay,
* multiple renderer backends,
* deterministic or semi-deterministic simulation modes,
* and a stable public API that game projects can depend on for years.

The current project already has the right high-level shape: fixed-step simulation, worker pool, command buffering, renderer abstraction, and PImpl isolation. The missing work is mostly about breadth, scalability, and production-level ergonomics.

## Review — current state and scope (added post-draft)

The original draft was written without close reference to the code. Before
acting on it, a few corrections and a scope narrowing.

### Already implemented (in whole or in part)

These appear in the "gaps" list below but are at least partially present
today; the list below is therefore a useful planning frame, not an accurate
diff against `master`:

* **Typed queries.** `include/threadmaxx/Query.hpp` exposes
  `forEach<Components...>(ctx, fn)` and `forEachSerial<...>` over the existing
  dense built-in components. It is not yet variadic across user-defined
  components (the library has none), but the "richer query API" item is
  partially answered.
* **Engine profiling counters.** `Stats.hpp` defines `EngineStats` (step
  duration, EWMA, jobs submitted, commands committed, alive entities,
  lifetime totals) and `Engine::stats()` already returns it. The missing
  layer is *per-system* breakdown — covered as Item 1 below.
* **Deterministic mode flag.** `Config::deterministic` exists and the commit
  phase is already submission-ordered and single-threaded. The doc lists
  determinism as a future goal; it is already an invariant of the current
  code (the `determinism_test` pins it).
* **System-level scheduling from R/W sets.** Section 5's "task dependencies
  and continuations" is a much larger lift than the doc implies — the wave
  scheduler in `EngineImpl::rebuildWaves` already derives a topological order
  from declared `reads()`/`writes()`. A true task graph is a worthwhile next
  step but it should be additive, not a redesign.
* **Frame interpolation alpha.** `RenderFrame::alpha` and the second
  `submitInterpolatedFrame` pass already deliver wall-clock interpolation to
  the renderer between sim ticks.
* **A non-headless renderer example.** `examples/boids` demonstrates an
  `IRenderer` against SDL2 and exercises wave scheduling — useful as the
  reference for further renderer-side work.

### Out of scope for `threadmaxx` itself

These are good items for a *game* built on `threadmaxx`, but baking them
into the backend would either (a) tie the library to a specific third-party
implementation, or (b) bloat the public surface past the "small, stable
contract" principle in `ARCHITECTURE.md`. The right shape for the engine is
to provide *hooks* (component categories, event channels, frame-late
callbacks) rather than ship the systems themselves:

* **Networking, replication, rollback.** Belongs above the engine. The
  engine should at most provide deterministic commit + stable entity IDs
  (both already true) so a game can layer its own snapshot/delta logic.
* **Animation systems / IK / cloth / blend trees.** A real animation
  pipeline depends on the renderer's skinning model and the asset format.
  The engine can host these as user systems once a `Skeleton` /
  `AnimationState` component shape exists, but it should not own the math.
* **Physics integration (broadphase / narrowphase / rigid body).** Same
  reasoning — Bullet / Jolt / PhysX each impose a world ownership model
  incompatible with hardcoding one. A `PhysicsBody` component category and
  read-only world snapshot pattern is the engine's job; the solver is not.
* **Audio mixing / 3D audio.** Wholly orthogonal to a game backend.
* **Save/load + migration.** A serialization *hook* on components is in
  scope (a single trait function pair); a full versioned migration system
  is not.
* **Navmesh / pathfinding.** Belongs in a domain library; the engine only
  needs to allow background work to be scheduled.
* **Editor/tooling/hot-reload.** Out of scope until the public API has
  stabilized.

### Items the doc gets right but underestimates the cost of

* **Archetype/chunk storage.** Listed as Milestone 2. This is a deep
  refactor of `EntityStorage` and changes the meaning of every dense span
  the engine currently hands out. It should not happen until the public
  surface (queries, events, resources) is settled — otherwise the API will
  churn twice. Moved to a later phase below.
* **Frame task graph.** Listed as Phase 3 of the perf roadmap. Useful, but
  the bigger near-term win is retiring the single-mutex job queue in
  `JobSystem` (a sharded or work-stealing queue), not a graph executor.

### Items the doc misses

* **Per-entity component presence.** Today every live entity has every
  built-in component (parallel dense arrays). The renderer filters by
  `meshId < 0`. A small `ComponentMask` per entity unlocks real component
  filtering without forcing a full archetype rework.
* **Parent / hierarchy component.** Required for any non-trivial 3D scene
  and a perfect test case for the wave scheduler (a hierarchy system reads
  `Parent`+local `Transform` and writes world `Transform`, conflicting on
  `Transform` with movement/physics).
* **Per-job scratch arena.** The library promises lock-free per-job
  state but provides no allocator for it — gameplay code currently has to
  bring its own. A thread-local bump arena reset between waves is small
  and high-leverage.
* **System lifetime hooks beyond `onRegister`/`onUnregister`.** A
  `preStep`/`postStep` would let systems own setup/teardown that doesn't
  belong inside a `parallelFor`.

### Concrete near-term plan (replaces the doc's Section 10)

In priority order, smallest and most useful first. These are achievable
without breaking the existing public contract.

1. **Per-system timing and command stats.** Extend `EngineStats` with a
   per-system snapshot list. Time each system's `update` and attribute
   committed commands and submitted jobs back to it. Foundational for any
   later profiling overlay or budget enforcement.
2. **Sharded / work-stealing job queue.** Replace `JobSystem`'s single
   mutex+condvar pair with per-worker deques. The current queue is the
   single biggest scaling bottleneck under the wave scheduler.
3. **Per-entity `ComponentMask` and presence-aware queries.** A bitset
   per entity over the existing `Component` enum, set by spawn/destroy/`set*`
   commands. Lets the renderer (and `forEach`) skip entities that don't
   carry a given component without scanning sentinel values like
   `meshId < 0`.
4. **`Parent` component + hierarchy update system.** A built-in system
   that reads local `Transform` + `Parent` and writes world `Transform`.
   Exercises wave scheduling end-to-end with a real 3D requirement.
5. **Typed `ResourceId<T>` and minimal `ResourceRegistry`.** Just the ID
   type and a thread-safe registry — no async loader yet. Decoupled from
   any actual asset pipeline, but gives renderers and gameplay a stable
   way to refer to assets instead of raw `int32_t meshId`.

Items 1–3 are pure additions; items 4–5 require small public-API
extensions (a new component category and a new namespace) but no
breaking changes.

## 2. What the library still lacks

### Core engine gaps

* No mature entity/component model for complex games.
* No robust asset/resource system.
* No scene/world streaming.
* No save/load or serialization pipeline.
* No replication/network prediction/reconciliation layer.
* No animation system.
* No physics integration contract.
* No navigation/pathfinding system.
* No gameplay event bus or messaging layer.
* No time-scaling, pausing, or substep support.
* No tooling/debug overlay hooks.
* No profiling/tracing built into the public API.
  > Partially false: `EngineStats` and `Engine::stats()` already exist; the
  > missing piece is per-system breakdown — see Item 1 of the near-term plan.
* No clear support for editor tooling or hot reload.

### Public API gaps

The current public API is enough for a minimal backend, but a feature-complete 3D RPG will likely need additional stable abstractions:

* `EntityHandle` is not enough on its own; stable component access and queries are needed.
* `World` needs richer query APIs.
  > Partially false: `Query.hpp::forEach<Components...>` exists today. The
  > real next step is presence filtering (see "Items the doc misses").
* `SystemContext` needs explicit scheduling helpers and better access to timing, event, and resource services.
* `RenderFrame` is too flat for modern 3D rendering unless it carries more structured sub-passes.
* There is no formal resource handle API.
* There is no event/message API.
* There is no way to express parallel job dependencies more clearly than read/write sets.

### Multithreading gaps

The current wave scheduler is a good start, but production 3D workloads need more:

* work stealing or better load balancing,
* task dependencies and continuations,
* parallelism inside systems without forcing each system to reinvent it,
* async resource loading,
* async animation evaluation,
* async culling and visibility building,
* async streaming and decompression,
* job priorities,
* per-frame task budgets,
* cancellation when a frame becomes obsolete,
* and scalable synchronization primitives.

### 3D RPG-specific gaps

A 3D RPG typically needs parallel systems for:

* transform hierarchy updates,
* animation pose evaluation,
* skeleton skinning,
* physics stepping,
* broadphase / narrowphase collision work,
* navmesh queries and pathfinding,
* AI decision making,
* crowd simulation,
* visibility culling,
* LOD selection,
* streaming terrain/chunks,
* audio spatialization,
* particle simulation,
* effects spawning,
* UI layout in some cases,
* network snapshot creation,
* save-game generation,
* and background asset decompression.

The library should plan for these explicitly instead of assuming only basic movement or AI systems.

## 3. Design principles for the next phase

1. Keep the public API small, but add the minimum abstractions a real game needs.
2. Keep all heavy implementation details private.
3. Preserve renderer independence.
4. Preserve deterministic commit semantics where possible.
5. Prefer data-oriented batches over object-oriented per-entity work.
6. Prefer explicit ownership and explicit data flow over hidden global state.
7. Make parallelism a library feature, not a game-specific pattern.
8. Build for profiling and debugging from the start.
9. Design for streaming and large worlds early.
10. Treat serialization, networking, and tooling as first-class requirements.

## 4. Public API extensions to consider

### 4.1 World and entity querying

Add richer query and view APIs:

* `World::view<Transform, Velocity>()`
* `World::query(...)`
* `World::has<T>(EntityHandle)`
* `World::get<T>(EntityHandle)`
* `World::tryGet<T>(EntityHandle)`
* `World::forEach<T...>(fn)`

These should be read-only or explicitly staged for mutation through command buffers or scoped writable views.

### 4.2 Component and archetype model

Introduce a more explicit component model:

* stable `ComponentId`
* component registration
* archetype/chunk-based storage
* optional sparse component fallback for irregular data
* component metadata for tooling and serialization

This is needed for efficient large-scale scenes and for minimizing cache misses.

### 4.3 Resource/asset system

Add a public resource layer:

* `ResourceHandle<T>`
* async load requests
* reference counting or lifetime tokens
* hot-reload hooks
* dependency tracking
* streaming state reporting

This is crucial for 3D RPGs with large texture, mesh, animation, and audio sets.

### 4.4 Event and message system

Add a typed event bus or message queue:

* frame-local events
* deferred events
* persistent event subscriptions
* cross-system notifications
* network/gameplay/event bridging

Use this for combat hits, quest triggers, animation events, audio cues, and UI updates.

### 4.5 Job and scheduling API

Expose a public task interface that is more powerful than `parallelFor`:

* `Engine::dispatch(...)`
* `SystemContext::spawnTask(...)`
* task dependencies/futures
* continuation support
* cancellation tokens
* priority hints
* frame groups or barriers

This helps systems express more complex parallel work without tightly coupling to the internal scheduler.

### 4.6 Timing and simulation controls

Add:

* pause/resume
* time scale control
* fixed-step and semi-fixed-step modes
* substepping support for physics
* frame interpolation factor access
* per-system time budgets

This is especially useful for debugging, replay, slow motion, and physics stability.

### 4.7 Render abstraction expansion

A flat `RenderFrame` is fine for the minimal backend, but a 3D engine usually needs more structure:

* visibility lists
* draw-item bins by pass
* depth prepass / opaque / transparent / shadow passes
* light lists
* camera data
* reflection probes or environment probes
* post-processing inputs
* debug overlay layer

A possible direction is to keep `RenderFrame` public, but make it hierarchical and pass-oriented rather than just a single flat instance list.

### 4.8 Serialization and save/load

Add stable serialization hooks for:

* world state
* player state
* quest state
* inventory
* transforms and hierarchy
* component versioning
* migration support

This is non-optional for a production RPG.

### 4.9 Networking support

Eventually expose a networking layer or at least networking-friendly APIs:

* authoritative state snapshots
* delta compression hooks
* prediction-friendly command streams
* rollback-friendly state cloning if needed
* replication filters by entity/component

Even if network code stays external, the engine should be designed with replication in mind.

### 4.10 Debug/profiling API

Expose:

* scoped profiling markers
* job timing stats
* per-system timing stats
* queue depth stats
* frame breakdowns
* memory stats
* deadlock/watchdog diagnostics

A production library is much easier to trust when it explains where time goes.

## 5. Multithreading and performance roadmap

### Phase 1: stabilize the current job model

* Keep the worker pool, but replace ad hoc queue usage with a more scalable task system.
* Add per-thread local queues or a work-stealing design.
* Avoid a single central mutex as the main bottleneck.
* Preserve deterministic commit ordering.

### Phase 2: split system work into finer tasks

Current system-level parallelism is useful, but not enough for big 3D workloads.
Extend the engine so a single system can produce many independent tasks:

* per-chunk iteration,
* per-archetype iteration,
* per-region iteration,
* per-animation-graph iteration,
* per-visible-set iteration.

This is the largest scalability win for large worlds.

### Phase 3: add frame task graphs

Introduce a task graph or dependency graph:

* build tasks from systems,
* add dependencies explicitly,
* allow async completion,
* support continuations,
* allow the engine to overlap unrelated work.

This enables better CPU utilization and opens the door to streaming, async animation, and async culling.

### Phase 4: add cancellation and budgets

Production games need to stop stale work:

* frame cancellation,
* streaming cancellation,
* budget-based task scheduling,
* job prioritization.

Example: if a new camera position invalidates an old culling job, the engine should be able to drop it early.

### Phase 5: reduce contention in storage

Move toward data layouts that minimize locking and cache misses:

* chunked component storage,
* read-only snapshots,
* double/triple buffering where useful,
* append-only command streams,
* per-worker scratch arenas,
* reduced false sharing.

### Phase 6: measure everything

Add built-in profiling so performance work is guided by data:

* job duration histograms,
* hot system ranking,
* lock contention stats,
* cache-friendly batch sizes,
* allocation counters,
* frame hitches,
* streaming stalls.

## 6. 3D RPG parallelism map

The following are high-value systems that should be considered for parallel execution:

### Simulation and gameplay

* movement
* combat resolution
* status effects
* AI planning
* quest state evaluation
* inventory changes
* character attribute updates
* faction or reputation changes

### World and scene

* transform propagation
* scene graph updates
* streaming chunk activation/deactivation
* environment state changes
* destructible world updates

### Rendering preparation

* visibility culling
* LOD selection
* shadow caster extraction
* draw call binning
* light assignment
* reflection probe updates
* animation-to-render data conversion

### Animation and characters

* animation graph evaluation
* blend tree processing
* inverse kinematics
* skeleton pose generation
* skinning preparation
* cloth or secondary motion

### Physics and navigation

* broadphase collision
* narrowphase collision batches
* rigid body integration
* character controller resolution
* navmesh pathfinding
* crowd movement
* obstacle avoidance

### Background / asynchronous work

Background work should be treated as a first-class subsystem rather than an ad hoc collection of helper threads. In a production 3D RPG, the engine should be able to keep the simulation thread responsive while expensive non-immediate work happens in parallel.

Recommended categories:

* asset loading

  * async file I/O
  * dependency resolution
  * content-ready notifications
  * streaming priorities based on camera proximity and gameplay importance

* mesh decoding

  * background decompression
  * mesh format conversion
  * tangent/normal preparation
  * collision mesh extraction

* texture preparation

  * mip generation where needed
  * transcoding or format conversion
  * staging-buffer preparation for the render backend

* audio decode and mixing prep

  * background decode of streamed audio
  * spatial audio source preparation
  * voice prioritization when many sounds compete

* save-game serialization

  * snapshotting state without blocking the main loop for too long
  * incremental save support
  * versioned save data and migration hooks

* network packet processing

  * packet decode/encode
  * replication filtering
  * prediction/reconciliation support data
  * batching for lower overhead

* decompression and patching

  * archive unpacking
  * hotfix asset replacement
  * patch validation
  * safe swap-in of updated content

* shader and pipeline preparation

  * offline where possible, async where supported by the backend
  * cache management
  * compile-failure reporting and fallback handling

* terrain / world streaming preparation

  * chunk generation
  * navmesh tile loading
  * foliage or prop population
  * impostor/LOD asset selection

Implementation notes:

* background jobs should run on the same task system when possible, but on lower priority lanes than gameplay-critical work;
* long-running jobs should be cancelable or restartable if the camera, level, or game state changes;
* background systems should publish results through the same command/result model used elsewhere, so the commit phase stays predictable;
* I/O-heavy operations should expose progress and failure states to the game layer;
* the engine should keep a bounded queue of background work to avoid unbounded memory growth under streaming pressure.

A production-ready library should have at least one dedicated async pipeline for asset streaming, one for save/load, and one for network-related processing. Those three are usually the first places where a small engine becomes a real game-ready backend.

## 7. Proposed roadmap

### Milestone 1 — Hardening the current core

Goals:

* stabilize public headers,
* add tests for threading correctness,
* add profiling hooks,
* improve error handling,
* document invariants clearly.

Exit criteria:

* minimal API is stable enough for external use,
* no data races in common paths,
* deterministic mode is verified by tests.

### Milestone 2 — Data model upgrade

Goals:

* introduce archetype/chunk storage,
* add typed queries,
* improve component access patterns,
* reduce per-entity overhead.

Exit criteria:

* iteration over large worlds is cache-friendly,
* query performance is materially improved,
* game code can express common operations without manual index loops.

### Milestone 3 — Resource and event layers

Goals:

* add handles for resources,
* implement async loading APIs,
* add event/message queues,
* wire in hot reload hooks.

Exit criteria:

* a 3D game can stream assets and react to gameplay events without custom engine patches.

### Milestone 4 — Rendering contract expansion

Goals:

* evolve `RenderFrame` into a structured render submission format,
* add camera/light/pass data,
* support multi-pass rendering preparation,
* support debug overlays.

Exit criteria:

* a real 3D renderer can be plugged in without engine changes,
* render preparation is efficient and parallel-friendly.

### Milestone 5 — Task graph and deep parallelism

Goals:

* move from basic worker queue to task graph execution,
* enable intra-system parallelism with cancellation,
* support priorities and frame budgets,
* reduce contention.

Exit criteria:

* large scenes utilize many cores efficiently,
* the engine scales beyond simple system-level parallelism.

### Milestone 6 — RPG feature readiness

Goals:

* add serialization and save/load,
* add navigation/pathfinding support,
* add animation support hooks,
* add physics integration points,
* add network-friendly state APIs.

Exit criteria:

* the library can support a full 3D RPG vertical slice.

## 8. Suggested API evolution strategy

Do not break the current API abruptly.

Recommended approach:

1. Keep existing minimal interfaces working.
2. Add new capabilities as optional extensions.
3. Provide compatibility wrappers where possible.
4. Version major API changes carefully.
5. Avoid exposing storage internals even when new features are added.

A good rule is: public API should describe intent, not storage layout.

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

## 10. Recommended next concrete tasks

1. Write tests for deterministic commit order and race-free command buffers.
2. Add profiling counters for jobs, systems, and render preparation.
3. Design and implement a typed query API for `World`.
4. Introduce a resource handle abstraction.
5. Expand `RenderFrame` into a pass-aware submission structure.
6. Add a typed event queue.
7. Prototype a task-graph scheduler or work-stealing worker pool.
8. Add save/load serialization hooks.
9. Add animation and navigation extension points.
10. Validate the whole stack with a small 3D RPG-style prototype.

## 11. Definition of a production-ready version

`threadmaxx` is production-ready for a 3D RPG when:

* it supports large worlds without excessive contention,
* it can stream assets and chunks asynchronously,
* it supports multiple renderer backends cleanly,
* it has stable serialization and save/load,
* it can expose profiling and debugging data,
* it supports deterministic or reproducible simulation modes,
* it can be integrated into a real game without patching core internals,
* and it scales across multiple CPU cores in common gameplay and render-prep workloads.

## 12. Final note

The current architecture is already a solid foundation. The future work is mostly about turning a good internal backend into a fully usable engine library: richer APIs, more scalable tasking, better world representation, and support for the systems a real 3D RPG actually needs.
