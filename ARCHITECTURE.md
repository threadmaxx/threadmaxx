# threadmaxx — Architecture Overview

`threadmaxx` is a renderer-agnostic C++20 game backend. Game code links against
it and registers systems (gameplay, physics, AI, ...). The engine drives a
fixed-step simulation loop, parallelizes per-system work across a worker pool,
and hands a snapshot of the world to a pluggable renderer each frame.

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
   │   for each wave (group of systems with non-conflicting read/write sets):
   │       sibling systems in the wave run concurrently on helper threads;
   │       within each system:
   │       1. System schedules work via SystemContext::parallelFor(N, grain, fn)
   │       2. Engine splits [0, N) into chunks and submits one Job per chunk
   │
   │                            │  enqueue
   │                            ▼
   │                  ┌──────────────────────┐
   │                  │     Job queue        │
   │                  │   (mutex + condvar)  │
   │                  └─────────┬────────────┘
   │                            │ workers pop
   │                            ▼
   │           ┌────────┐  ┌────────┐  ┌────────┐
   │           │worker 0│  │worker 1│  │worker N│   (std::thread pool)
   │           └───┬────┘  └───┬────┘  └───┬────┘
   │               │           │           │
   │               │ each job receives:
   │               │   • const World&   (read-only)
   │               │   • Range          (index slice)
   │               │   • CommandBuffer& (its own — no sharing)
   │               │
   │       3. Engine waits on a per-batch counter (latch-style)
   │       4. Engine commits command buffers in deterministic job order
   │          (mutations applied here, on the sim thread, single-threaded)
   │
   ├─ all systems done
   │
   │   5. Engine builds a RenderFrame from the current world state
   │   6. RenderFrame swapped into the back render buffer
   │   7. Renderer::submitFrame(front) — caller-controlled
   │
   └─ tick ends
```

Key invariants:

- Workers never touch live world state. They read it and emit commands.
- `CommandBuffer` is per-job, never shared. No locks in user gameplay code.
- The commit phase is single-threaded and ordered by job index, so the same
  inputs always produce the same world state — that is the basis of the
  optional deterministic mode.
- The renderer reads from the most recently published `RenderFrame`. If a
  caller drives `step()` and `submitFrame()` themselves they can also choose
  to read the front buffer from a different thread; the engine publishes via
  an atomic pointer swap.

## Public surface

```
threadmaxx::Engine        // top-level: init / step / run / shutdown
threadmaxx::World         // read-only handle-based view of state
threadmaxx::ISystem       // user-implemented gameplay/physics/AI
threadmaxx::IRenderer     // user-implemented backend (Vulkan/SDL/...)
threadmaxx::IGame         // bundle: registers systems + renderer at startup
threadmaxx::CommandBuffer // record mutations from worker jobs
threadmaxx::RenderFrame   // flat data the renderer consumes
threadmaxx::RenderInstance
threadmaxx::EntityHandle
threadmaxx::Config
threadmaxx::EngineStats   // per-step instrumentation snapshot
threadmaxx::SystemStats   // per-system instrumentation snapshot
```

Everything else (entity storage, job system, commit machinery, render-frame
construction) lives in `src/` behind PImpl. The header set above is what game
code includes.

## Layout

```
threadmaxx/
├── CMakeLists.txt
├── ARCHITECTURE.md
├── include/threadmaxx/         ← public, stable
│   ├── threadmaxx.hpp          ← umbrella header
│   ├── Config.hpp
│   ├── Handles.hpp
│   ├── Components.hpp
│   ├── CommandBuffer.hpp
│   ├── RenderFrame.hpp
│   ├── System.hpp
│   ├── Renderer.hpp
│   ├── Game.hpp
│   ├── World.hpp
│   └── Engine.hpp
├── src/                        ← private, may change freely
│   ├── EntityStorage.hpp / .cpp
│   ├── JobSystem.hpp / .cpp
│   ├── WorldImpl.hpp / .cpp
│   ├── EngineImpl.hpp / .cpp
│   ├── World.cpp               ← PImpl forwarding
│   ├── Engine.cpp              ← PImpl forwarding
│   └── CommandBuffer.cpp
├── examples/minimal/           ← runnable headless example
│   ├── CMakeLists.txt
│   ├── main.cpp
│   ├── MyGame.hpp / .cpp
│   ├── MovementSystem.hpp / .cpp
│   └── ConsoleRenderer.hpp / .cpp
└── examples/boids/             ← non-headless example (SDL2)
    ├── CMakeLists.txt
    ├── main.cpp
    ├── BoidsGame.hpp / .cpp
    ├── BoidsSystem.hpp / .cpp   ← steer (R: T,V → W: V)
    ├── MoveSystem.hpp / .cpp    ← integrate + wrap (R: V,T → W: T)
    └── SDLRenderer.hpp / .cpp   ← IRenderer over SDL2
```

The boids example is built only when `find_package(SDL2)` succeeds — it
exercises the wave scheduler (steer and integrate land in distinct waves
because their read/write sets conflict on Velocity and Transform) and
shows a concrete `IRenderer` implementation against a real windowing
backend.

## System-level scheduling

`ISystem` exposes `reads()` and `writes()` returning `ComponentSet` (a
bitset over `Component::{Transform, Velocity, Acceleration, RenderTag,
UserData, EntityStructural}`). At `registerSystem` time the engine greedy-packs
systems into waves: within a wave every pair of systems is non-conflicting
under the rule

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

## Extensibility notes

- More built-in components: extend `EntityStorage` and `World`'s read accessors,
  and add a corresponding `Component::Foo` enum value (plus update
  `ComponentSet::all()`'s mask) so the scheduler can distinguish it. The
  built-in `Acceleration` component shows the full recipe end-to-end.
- Custom renderers: implement `IRenderer` — no engine changes required.
- Finer-grained system scheduling: today the wave grouping is based on
  declared component sets. A future DAG with explicit `depends_on(name)`
  edges would slot in beside `reads()`/`writes()` — the per-job contract
  (read world, write to own command buffer) already supports any topology.
- Replacing the job system: `JobSystem` is private; only `EngineImpl` calls it.
