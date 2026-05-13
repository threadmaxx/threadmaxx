# threadmaxx — Architecture Overview

`threadmaxx` is a renderer-agnostic C++20 game backend. Game code links
against it and registers systems (gameplay, physics, AI, ...). The engine
drives a fixed-step simulation loop, parallelizes per-system work across a
worker pool, and hands a snapshot of the world to a pluggable renderer
each frame.

## Goals

- One authoritative world; gameplay code never races against itself.
- Parallel by default: workers process disjoint slices of entities.
- Renderer-agnostic: the renderer only sees a flat `RenderFrame`.
- Small, stable public surface; everything else hidden behind PImpl.
- Determinism is a configuration flag, not a rewrite.

## Threading model

```
                          main / simulation thread
                          ─────────────────────────
   ┌─ tick begins
   │
   │   preStep:  every registered ISystem.preStep() runs serially,
   │             in registration order. Commits flush immediately.
   │
   │   for each wave (group of systems with non-conflicting read/write sets):
   │       sibling systems in the wave run concurrently on helper threads;
   │       within each system:
   │       1. System schedules work via SystemContext::parallelFor(N, grain, fn)
   │       2. Engine splits [0, N) into chunks and submits one Job per chunk
   │
   │                            │  enqueue (round-robin push)
   │                            ▼
   │           ┌────────┐  ┌────────┐  ┌────────┐
   │           │ deque  │  │ deque  │  │ deque  │   (per-worker, own
   │           │ + mtx  │  │ + mtx  │  │ + mtx  │    mutex + CV)
   │           └───┬────┘  └───┬────┘  └───┬────┘
   │               │ pop own (front, FIFO)│
   │               │ or steal from sibling (back, LIFO)
   │               ▼           ▼           ▼
   │           ┌────────┐  ┌────────┐  ┌────────┐
   │           │worker 0│  │worker 1│  │worker N│   (std::thread pool)
   │           └───┬────┘  └───┬────┘  └───┬────┘
   │               │           │           │
   │               │ each job receives:
   │               │   • const World&   (read-only)
   │               │   • Range          (index slice)
   │               │   • CommandBuffer& (its own — no sharing)
   │               │   • optionally: ScratchArena& (per-job bump alloc)
   │               │
   │       3. Engine waits on a per-batch counter (latch-style)
   │       4. Engine commits command buffers in deterministic job order
   │          (mutations applied here, on the sim thread, single-threaded)
   │
   ├─ all waves done
   │
   │   postStep: every registered ISystem.postStep() runs serially,
   │             in registration order. Commits flush immediately.
   │
   │   Reservation reap: any reserveHandle() not consumed by a
   │     cb.spawn(handle, ...) is dropped here.
   │
   │   Event drain: each EventChannel<T> swaps front<->back so emits
   │     from this tick become visible on the next.
   │
   │   tick++; simulationTime += fixedStepSeconds
   │   Engine builds a RenderFrame from the current world state
   │   RenderFrame swapped into the back render buffer
   │   Renderer::submitFrame(front) — caller-controlled
   │
   └─ tick ends
```

Key invariants:

- Workers never touch live world state. They read it and emit commands.
- `CommandBuffer` is per-job, never shared. No locks in user gameplay code.
- `JobSystem` uses per-worker work-stealing deques rather than a single
  central queue, so submits and pops don't serialize on one hot mutex.
  Stealers `try_lock` victims so an active producer can't block them.
- The commit phase is single-threaded and ordered by job index, so the same
  inputs always produce the same world state — that is the basis of the
  optional deterministic mode.
- The renderer reads from the most recently published `RenderFrame`. If a
  caller drives `step()` and `submitFrame()` themselves they can also choose
  to read the front buffer from a different thread; the engine publishes via
  an atomic pointer swap.

## Public surface

```
threadmaxx::Engine             // top-level: init / step / run / shutdown
threadmaxx::World              // read-only handle-based view of state
threadmaxx::ISystem            // user-implemented gameplay/physics/AI
threadmaxx::SystemContext      // parallelFor / single / reserveHandle / world / dt / tick
threadmaxx::IRenderer          // user-implemented backend (Vulkan/SDL/...)
threadmaxx::IGame              // bundle: registers systems + renderer at startup
threadmaxx::CommandBuffer      // record mutations from worker jobs
threadmaxx::ScratchArena       // per-job bump allocator
threadmaxx::EventChannel<T>    // typed double-buffered cross-system queue
threadmaxx::RenderFrame        // flat data the renderer consumes
threadmaxx::RenderInstance
threadmaxx::EntityHandle
threadmaxx::Config
threadmaxx::EngineStats        // per-tick instrumentation
threadmaxx::SystemStats        // per-system instrumentation
threadmaxx::JobSystemStats     // per-worker steal/own-pop totals
threadmaxx::ResourceId<T>      // typed handle into ResourceRegistry
threadmaxx::ResourceRegistry   // engine-owned, thread-safe typed store
threadmaxx::Component          // scheduling category + per-entity presence bit
threadmaxx::ComponentSet       // 32-bit bitset over Component
threadmaxx::Transform          // position / orientation / scale
threadmaxx::Velocity           // linear / angular
threadmaxx::Acceleration       // linear / angular rate-of-change
threadmaxx::RenderTag          // mesh / material / flags
threadmaxx::UserData           // 64 user bits per entity
threadmaxx::Parent             // hierarchical-attachment component
threadmaxx::makeHierarchySystem // factory for the built-in hierarchy system
threadmaxx::forEach            // parallel query over chosen components
threadmaxx::forEachWith        // parallel query, filtered by component-presence mask
threadmaxx::forEachSerial      // single-threaded equivalent
```

Everything else (entity storage, job system, commit machinery, render-frame
construction) lives in `src/` behind PImpl. The header set above is what
game code includes.

## Layout

```
threadmaxx/
├── CMakeLists.txt
├── Doxyfile                       ← optional `doc` CMake target
├── ARCHITECTURE.md                ← this file
├── README.md
├── FUTURE_WORK.md
├── CLAUDE.md
├── include/threadmaxx/            ← public, stable
│   ├── threadmaxx.hpp             ← umbrella header
│   ├── Config.hpp
│   ├── Handles.hpp
│   ├── Components.hpp             ← built-in components + Component / ComponentSet
│   ├── CommandBuffer.hpp
│   ├── EventChannel.hpp           ← typed double-buffered queue
│   ├── Query.hpp                  ← forEach / forEachWith / forEachSerial
│   ├── RenderFrame.hpp
│   ├── Renderer.hpp
│   ├── Resource.hpp               ← ResourceId<T> + ResourceRegistry
│   ├── ScratchArena.hpp           ← per-job bump allocator
│   ├── Stats.hpp                  ← EngineStats / SystemStats / JobSystemStats
│   ├── System.hpp                 ← ISystem + SystemContext + makeHierarchySystem
│   ├── Game.hpp
│   ├── World.hpp
│   └── Engine.hpp
├── src/                           ← private, may change freely
│   ├── EntityStorage.hpp / .cpp
│   ├── JobSystem.hpp / .cpp       ← per-worker work-stealing deques
│   ├── ResourceRegistry.cpp       ← typed-store PImpl impl
│   ├── HierarchySystem.cpp        ← built-in hierarchy system
│   ├── ScratchArena.cpp           ← chained-slab bump allocator
│   ├── WorldImpl.hpp
│   ├── EngineImpl.hpp / .cpp
│   ├── World.cpp                  ← PImpl forwarding
│   ├── Engine.cpp                 ← PImpl forwarding
│   └── CommandBuffer.cpp
├── tests/                         ← 20 no-dependency tests
├── doc/                           ← user guide (Markdown, also Doxygen-ingested)
│   ├── index.md
│   ├── getting_started.md
│   ├── concepts.md
│   ├── components_and_queries.md
│   ├── systems_and_scheduling.md
│   ├── lifecycle_hooks.md
│   ├── command_buffers.md
│   ├── reserved_handles.md
│   ├── scratch_arenas.md
│   ├── hierarchy.md
│   ├── events.md
│   ├── resources.md
│   ├── renderer_integration.md
│   ├── configuration.md
│   ├── pause_and_time_scale.md
│   └── stats_and_profiling.md
├── examples/minimal/              ← runnable headless example
│   ├── CMakeLists.txt
│   ├── main.cpp
│   ├── MyGame.hpp / .cpp
│   ├── MovementSystem.hpp / .cpp
│   ├── SpawnerSystem.hpp / .cpp
│   └── ConsoleRenderer.hpp / .cpp
└── examples/boids/                ← non-headless example (SDL2)
    ├── CMakeLists.txt
    ├── main.cpp
    ├── BoidsGame.hpp / .cpp
    ├── BoidsSystem.hpp / .cpp     ← steer (R: T,V → W: V)
    ├── MoveSystem.hpp / .cpp      ← integrate + wrap (R: V,T → W: T)
    └── SDLRenderer.hpp / .cpp     ← IRenderer over SDL2
```

The boids example is built only when `find_package(SDL2)` succeeds — it
exercises the wave scheduler (steer and integrate land in distinct waves
because their read/write sets conflict on Velocity and Transform) and
shows a concrete `IRenderer` implementation against a real windowing
backend.

## System-level scheduling

`ISystem` exposes `reads()` and `writes()` returning `ComponentSet` (a
bitset over `Component::{Transform, Velocity, Acceleration, RenderTag,
UserData, EntityStructural, Parent}` — bits 0..6). At `registerSystem`
time the engine greedy-packs systems into waves: within a wave every
pair of systems is non-conflicting under the rule

```
conflict(A, B) ⇔ (A.writes ∩ B.writes) ∪
                 (A.writes ∩ B.reads)  ∪
                 (A.reads  ∩ B.writes) ≠ ∅
```

Each wave's systems are dispatched on (size-1) helper threads plus the
sim thread; after they all return, the engine commits their command
buffers in registration order on the sim thread. Defaults are
`ComponentSet::all()` for both reads and writes, which makes every pair
conflict and degrades cleanly to strict registration-order serial — the
historical behavior. Tests in `tests/parallel_systems_test.cpp` pin the
contract.

## Lifecycle hooks

`ISystem` has three call points per tick: `preStep`, `update`, and
`postStep`. `preStep` and `postStep` are serial (sim thread,
registration order, single-threaded); they default to no-ops. `update`
is the wave-scheduled hook described above. Commands recorded in
`preStep` flush immediately so wave-phase systems observe them;
commands recorded in `postStep` are visible to the next tick's
`preStep` and wave systems. This three-phase layout gives game code a
deterministic pre/post serial slot without sacrificing parallel update
execution — see `tests/lifecycle_hooks_test.cpp` for the contract.

## Scratch arenas

Per-job bump allocator paired with `CommandBuffer` in the new
three-arg `parallelFor` / `single` overloads. Chained-slab storage
(see `src/ScratchArena.cpp`): allocations never invalidate prior
pointers within the same epoch — only `reset()` does. The engine
allocates one arena per chunk in `SystemContextImpl::arenas_`,
parallel to `buffers_`; arenas are destroyed when the wave ends. For
steady-state usage the first-tick allocation is amortized away;
subsequent ticks reuse the slabs. `allocate<T>` static-asserts that
`T` is trivially destructible.

## Reserved spawn handles

`Engine::reserveEntityHandle()` and `SystemContext::reserveHandle()`
take a slot from `EntityStorage` under an internal mutex
(`reservationMtx_`), bump its generation, and mark it `reserved=true`.
The handle is valid for use in `CommandBuffer::spawn(handle, ...)`
overloads and as the target of a `Parent{handle, ...}`. During commit
`materializeReserved` finishes the spawn by populating dense arrays
and flipping `reserved → alive`. Reservations not consumed by any
spawn are reaped in `discardAllReservations` at step end (generation
bumped again so the user's handle stops validating).

This is the one explicit exception to the "workers don't mutate state"
invariant: reservation manipulates the slot allocator only, not dense
data — and it does so under its own mutex. Dense arrays still grow
single-threaded during commit. `tests/reserved_handle_test.cpp` pins
the contract.

## Event channels

`EventChannel<T>` (`include/threadmaxx/EventChannel.hpp`) is the
typed double-buffered queue surfaced via `Engine::events<T>()`.
Storage is type-erased on `EngineImpl::eventChannels_`
(`std::unordered_map<std::type_index, EventChannelEntry>`), with
function-pointer hooks for the deleter and the drainer. Per-channel
state is a `std::mutex`-guarded back buffer plus a stable front
buffer; `drain()` swaps them at tick boundary. `emit` is mutex-
protected so worker jobs can produce safely; `drainTick` returns a
span into the front buffer.

## Pause and time-scale

`Engine::setTimeScale(s)` scales the `dt` value seen by systems
(negative clamped to zero); `tick()` and `simulationTime()` still
advance by exactly one fixed step per `step()`. `setPaused(true)`
makes `step()` an early-return (per-tick stats zeroed); `run()` keeps
re-submitting the current front frame so the renderer doesn't freeze.

## Per-entity component presence

Every live entity carries a `ComponentSet` of which built-in components are
logically attached. `CommandBuffer::spawn()` derives the mask from the
supplied values (RenderTag bit set iff `meshId >= 0`; Parent bit iff
`parent.parent.valid()`); `setRenderTag()` keeps the bit in sync with the
new `meshId`, and `setParent()` keeps the Parent bit in sync. Game code
can override either via the explicit-mask `spawn` overload or
`CommandBuffer::setComponentMask`. The renderer skips entities without
`Component::RenderTag` in `buildRenderFrame` instead of scanning a sentinel
value. `forEachWith<Required...>` in `Query.hpp` filters by mask so systems
can express "iterate everyone with both Transform and RenderTag" without
sentinel checks.

`EntityStructural` does not appear in per-entity masks; it's purely a
scheduling category for systems that spawn/destroy entities. The storage
layout still keeps every component slot filled for every entity (parallel
dense arrays); the mask is a filter, not an archetype.

## Resource registry

`ResourceRegistry` is engine-owned (one per `Engine`, lifetime matches
the engine, never reseats). It stores resources type-erased via
`std::shared_ptr<void>` keyed by `std::type_index`, behind a single
internal mutex. `ResourceId<T>` is a 64-bit (index, generation) handle;
generation bumps on `remove()` so stale handles never alias new ones.

The registry is safe to touch from any thread, including worker jobs.
The single mutex is sized for setup-time registration and per-frame
lookups, not for thousands of concurrent inserts — async loaders should
do their I/O off-thread and call `add()` once each resource is ready.

The library does not load anything itself; the registry is the storage
contract between the game and its renderer / audio / animation pipeline.

## Hierarchy

The built-in `HierarchySystem` (factory: `makeHierarchySystem()`) reads
`Transform + Parent` and writes `Transform`. It runs single-threaded
inside `ctx.single()` and resolves multi-level parent chains in one pass
via DFS-with-memoization. World pose composition:

- `position = parent.position + rotate(parent.orientation, local.position)`
- `orientation = parent.orientation * local.orientation` (Hamilton product)
- `scale = local.scale` (scale does **not** chain — see `doc/hierarchy.md`)

Register the hierarchy system *after* any system that writes `Transform`
so it observes their commits in a later wave.

## Render frame lifetime

`EngineImpl::buildRenderFrame()` writes into the back of a double-
buffered pair (`renderInstanceBuffers_[0/1]` and `renderFrames_[0/1]`)
and publishes via `frontIndex_.store(back, release)`. The
`RenderFrame::instances` span points into the engine-owned vector —
renderers must finish using it before `submitFrame` returns or copy what
they need. Today `submitFrame` is called synchronously from the sim
thread immediately after the swap, so single-threaded renderers don't
need to worry; if a future renderer reads from another thread, the
atomic swap is the synchronization point.

`RenderFrame::alpha` is the wall-clock fraction (0..1) past the last
committed tick. `step()` always submits the post-tick frame with
`alpha=0`. In `run()`, after the inner step loop, the engine
additionally calls `submitInterpolatedFrame(alpha)` which mutates
`alpha` on the current front frame and re-submits — there's no rebuild,
since world state is unchanged between ticks. Bypassing this and re-
running `buildRenderFrame()` per interp submit would be wasted work;
just don't.

## Instrumentation

`EngineStats` (one struct) and `SystemStats` (one per registered system)
are refreshed at the end of every `step()`. Both are POD snapshots —
`engine.stats()` returns a copy, `engine.systemStats()` returns an
engine-owned `std::span` valid until the next `registerSystem()` or
`shutdown()`. Measured fields are documented in
[`doc/stats_and_profiling.md`](doc/stats_and_profiling.md); the
instrumentation has no opt-in cost and the EWMA decay is fixed at 1/16
(~16-step horizon).

## Extensibility notes

- More built-in components: extend `EntityStorage` and `World`'s read
  accessors, and add a corresponding `Component::Foo` enum value (plus
  update `ComponentSet::all()`'s mask) so the scheduler can distinguish
  it. See [`CLAUDE.md`](CLAUDE.md)'s "Adding a new built-in component"
  recipe for the full cross-layer checklist; `Parent` and
  `Acceleration` show the full recipe end-to-end.
- Custom renderers: implement `IRenderer` — no engine changes required.
- Finer-grained system scheduling: today the wave grouping is based on
  declared component sets. A future DAG with explicit
  `depends_on(name)` edges would slot in beside `reads()`/`writes()` —
  the per-job contract (read world, write to own command buffer)
  already supports any topology.
- Replacing the job system: `JobSystem` is private; only `EngineImpl`
  calls it.

## Where to read next

- [User guide](doc/index.md) — full walkthrough of public concepts.
- [Future work](FUTURE_WORK.md) — what's done, what's next, what's out
  of scope.
- [Claude Code playbook](CLAUDE.md) — invariants, recipes, gotchas for
  contributors.
