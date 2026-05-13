# Core Concepts

@page core_concepts Core Concepts

The whole engine fits in seven words and one diagram. Internalize this
page and the rest of the guide is detail.

## Vocabulary

- **World** — the single authoritative store of game state. Read-only to
  worker jobs.
- **Entity** — an `EntityHandle` (index + generation). Pointers to dense
  arrays; never a class.
- **Component** — a POD per entity. Today the built-ins are `Transform`,
  `Velocity`, `Acceleration`, `RenderTag`, `UserData`, and `Parent`. Each
  is stored in its own parallel dense array.
- **ComponentSet** — a 32-bit bitset over the `Component` enum. Used for
  two distinct things:
  - **Scheduling category**: a system declares which components it
    `reads()` and `writes()`. The engine derives wave grouping from that.
  - **Per-entity presence**: every live entity carries a `ComponentSet`
    of which components are logically attached. `setRenderTag` and
    `setParent` keep this in sync; `forEachWith<...>` filters by it.
- **System** — a `class : public ISystem`. The engine calls `update()`
  every tick. Systems schedule work via `SystemContext::parallelFor`.
- **CommandBuffer** — a per-job, lock-free recorder of mutations. Workers
  emit commands; the engine commits them on the simulation thread.
- **Renderer** — an `IRenderer` that receives a flat `RenderFrame` after
  each commit. Optional — null means headless.
- **Resource** — anything the engine doesn't own (meshes, textures, …).
  Stored in `ResourceRegistry`, referenced by typed `ResourceId<T>`.

## The tick

Every fixed step the engine does this:

```
┌─ tick begins
│
│  for each WAVE (group of systems with non-conflicting read/write sets):
│      run each system's update() concurrently on helper threads
│      └ each update() calls ctx.parallelFor → many worker jobs
│           each job:   reads const World&,  writes its own CommandBuffer
│      latch waits for all jobs in the wave
│      commit each system's command buffers in registration order
│
├─ all systems done
│
│  build RenderFrame from current world state
│  atomic-publish to the front buffer
│  renderer.submitFrame(front)
│
└─ tick ends
```

This shape is load-bearing. The invariants that fall out of it:

- **Worker jobs never mutate world state.** They get `const World&` and
  a private `CommandBuffer`. Period.
- **Commits run on the simulation thread, single-threaded, in submission
  order.** Same inputs → same world. That's the basis of the optional
  deterministic mode (`Config::deterministic`).
- **Gameplay code does not touch a mutex.** The job queue takes them; you
  don't.

## The data flow

```
   game code ──registers──▶  ISystem
                              │  ctx.parallelFor(...)
                              ▼
                          JobSystem ── dispatches chunks ──▶ worker threads
                                                              │ read World
                                                              │ write own CommandBuffer
                                                              ▼
                          Engine ◀── per-system CommandBuffer collection
                              │
                              │ commit (sim thread, in submission order)
                              ▼
                          EntityStorage  ── built into ──▶  RenderFrame ──▶ IRenderer
```

What flows where:

| Direction | Carries |
| --- | --- |
| `IGame::onSetup` → `Engine` | system registrations, optional renderer, seed entities |
| `Engine` → `ISystem::update` | `SystemContext` (world ref, dt, tick, parallelFor) |
| `ISystem::update` → workers | jobs (per-chunk lambdas) |
| workers → engine | command buffers (per-job, deferred) |
| engine → renderer | `RenderFrame` (post-commit snapshot) |

## What's mutable and what isn't

- `World` is **read-only** from every thread except the sim thread during
  the commit phase. Game code never gets a non-const reference to
  `World` in a hot path.
- `EntityStorage` lives behind `World`'s PImpl. Game code can't reach it.
- `CommandBuffer` is mutable by the worker that owns it. It is never
  shared.
- `ResourceRegistry` is internally locked and safe to touch from any
  thread.
- `EngineStats` and `SystemStats` are snapshots — you get a copy. The
  `SystemStats` span returned by `Engine::systemStats()` is invalidated
  by `registerSystem()` and `shutdown()`.

## Public surface, in one block

```cpp
namespace threadmaxx {
    class Engine;            // top-level lifecycle
    class World;             // read-only state view
    class ISystem;           // user gameplay/physics/AI
    class IRenderer;         // user backend
    class IGame;             // setup + teardown hooks
    class CommandBuffer;     // mutation recorder
    class SystemContext;     // parallelFor + single + world + dt + tick
    class ResourceRegistry;  // typed asset store
    struct Config;
    struct EntityHandle;
    template <typename T> struct ResourceId;

    // Components.
    struct Transform; struct Velocity; struct Acceleration;
    struct RenderTag; struct UserData; struct Parent;
    enum class Component : std::uint32_t;
    class ComponentSet;

    // Render-side.
    struct RenderFrame; struct RenderInstance;

    // Instrumentation.
    struct EngineStats; struct SystemStats;

    // Query helpers.
    template <class... Cs, class F> void forEach     (SystemContext&, F&&, std::uint32_t = 0);
    template <class... Cs, class F> void forEachWith (SystemContext&, F&&, std::uint32_t = 0);
    template <class... Cs, class F> void forEachSerial(SystemContext&, F&&);

    // Built-in systems.
    std::unique_ptr<ISystem> makeHierarchySystem();
}
```

Everything else in `src/` is private and may change between releases.

## Where to go next

- [Components & queries](components_and_queries.md) — the data side.
- [Systems & scheduling](systems_and_scheduling.md) — the code side.
