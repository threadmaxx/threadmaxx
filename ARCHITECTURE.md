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
   │   for each registered System (in registration order):
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
└── examples/minimal/           ← runnable headless example
    ├── CMakeLists.txt
    ├── main.cpp
    ├── MyGame.hpp / .cpp
    ├── MovementSystem.hpp / .cpp
    └── ConsoleRenderer.hpp / .cpp
```

## Extensibility notes

- More built-in components: extend `EntityStorage` and `World`'s read accessors;
  the public surface only grows with new accessor methods.
- Custom renderers: implement `IRenderer` — no engine changes required.
- System-level parallelism: today systems run sequentially with parallelized
  internals. Adding a dependency DAG between systems is additive; the per-job
  contract (read world, write to own command buffer) already supports it.
- Replacing the job system: `JobSystem` is private; only `EngineImpl` calls it.
