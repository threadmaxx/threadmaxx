# threadmaxx

A renderer-agnostic C++20 game backend with a fixed-step simulation loop and
a worker-thread job system. Game code registers systems (movement, physics,
AI, …) that read the world in parallel and emit commands; the engine
commits them deterministically and hands a flat `RenderFrame` to whatever
renderer you plug in.

Status: early but functional. The public API is small and intentionally
minimal; the internals are PImpl'd so they can change. 52 tests pin the
documented invariants.

## Highlights

- **Fixed-step simulation** with optional wall-clock pacing and
  interpolation alpha for render-side smoothing.
- **Wave scheduler** — systems declare their read/write component sets;
  the engine packs non-conflicting systems into the same wave and runs
  them concurrently.
- **Three-phase tick** — `preStep` (serial), `update` (parallel waves),
  `postStep` (serial). Pump input queues, snapshot for HUD, finalize
  per-tick aggregates.
- **Per-worker work-stealing job queue.** No single hot mutex, no
  central producer-consumer queue.
- **Per-job scratch arena.** Bump allocator paired with the command
  buffer in the new three-arg `parallelFor` / `single` overloads —
  zero-allocation steady-state scratch.
- **Typed event channels.** `Engine::events<T>()` returns a
  double-buffered queue; safe to emit from worker jobs, drained on
  tick boundary. Persistent `subscribe`/`unsubscribe` callbacks plus
  an RAII `Subscription` handle (`subscribeScoped`) that
  auto-detaches on destruction and survives the channel safely.
- **Pause + time-scale.** `setPaused(true)` freezes the simulation
  without freezing render-frame submission; `setTimeScale(s)` scales
  `dt` while leaving `tick()` an integer.
- **Reserved spawn handles.** `reserveHandle()` lets a single job
  spawn a parent and a child with the parent's handle wired into the
  child's `Parent` field. `reserveHandles(count, span)` is the
  one-mutex batch form.
- **`Bundle` + `spawnBundle`.** Variadic `bundle(Cs...)` factory yields
  a `Bundle` with a compile-time-derived presence mask; feed to
  `cb.spawnBundle`.
- **`World::has<T>` / `World::get<T>`.** Header-only presence-aware
  accessors over the existing component-presence mask.
- **Async resource loader contract.** `IResourceLoader` pumped once
  per tick on the sim thread; the engine never spawns I/O threads
  itself. `onShutdown` hook for graceful cancellation; `stats()` +
  `Engine::aggregateLoaderStats()` for HUD progress and memory
  accounting; `Engine::preloadUntil(predicate, timeout)` for
  splash-screen / boot-time blocking preloads.
- **Refcounted resource handles.** `ResourceRegistry::addRefCounted`
  + `acquire` return a `ResourceHandle<T>` whose destruction frees
  the slot on last drop. The legacy `add` / `remove` pair stays as
  the un-managed path.
- **Hot reload protocol.** `Engine::markResourceStale<T>(id)`
  dispatches to every loader; the loader replaces the asset and
  emits `AssetReloaded{oldId, newId}` on the typed event channel
  for subscribers to rewire cached ids.
- **Spatial hash helper.** `SpatialHash<Payload>` uniform-grid index
  for neighbor lookups, broadphase, AOI streaming.
- **Deterministic commit phase.** Workers emit commands into per-job
  buffers; the engine applies them on the sim thread in submission
  order. Same inputs → same world.
- **Per-entity component-presence mask.** 64-bit `ComponentSet` over
  16 built-in categories (Transform, Velocity, Acceleration, RenderTag,
  UserData, Parent, Health, Faction, AnimationStateRef, PhysicsBodyRef,
  NavAgentRef, BoundingVolume + three tag-only flags StaticTag /
  DisabledTag / DestroyedTag); 48 spare bits. Renderers and queries can
  skip entities that don't carry a given component without sentinel
  checks.
- **`MaskCache` + `forEachWithCached`.** Opt-in: rebuild a cached
  index list in `preStep`, iterate it in `update` without re-testing
  the mask. Pure perf win on stable-shape queries.
- **Built-in hierarchy.** `Parent` component + a `HierarchySystem`
  factory that propagates world transforms in one DFS pass.
- **Typed resource registry.** `ResourceId<T>` + a thread-safe
  `ResourceRegistry` for meshes, textures, audio clips, anything.
- **Per-tick instrumentation.** `EngineStats`, `SystemStats`, and
  `JobSystemStats` are populated every step — no opt-in cost.
  Per-system `waitSeconds` / `peakQueueDepth` plus a 16-bin job-
  duration histogram on `JobSystemStats`. `Engine::frameSnapshot()`
  bundles them; `writeJsonLines` spools one newline-terminated JSON
  object per tick; `ChromeTraceWriter` streams a Chrome Trace Event
  Format file you can open in `chrome://tracing`.
- **Save/load snapshot.** `World::snapshot()` copies the engine's
  dense arrays into a `WorldSnapshot`. Header-only `serialize` /
  `deserialize` trait pair in `<threadmaxx/Serialization.hpp>` round-
  trips it through any `std::ostream` / `std::istream`.
- **Pluggable log sink.** `ILogger` for engine lifecycle / registration
  / loader-error messages. Default sink writes warnings to `std::cerr`;
  install your own to route to spdlog / a HUD / a file.
- **Pluggable renderer.** Implement `IRenderer::submitFrame` against a
  flat `RenderFrame`. Null renderer = headless.
- **Hierarchical render contract.** `RenderFrame` exposes cameras,
  lights, per-pass draw-item bins (Opaque / Transparent / ShadowCasters
  / Overlay), and debug geometry alongside the legacy flat instances
  list. User systems populate it via `ISystem::buildRenderFrame`; the
  engine merges every system's slice in registration order on the sim
  thread.
- **Render-side helpers.** `include/threadmaxx/render/` ships
  renderer-neutral POD types (Camera, Light, DrawItem, MeshSkinnedRef,
  AnimationPoseRef, MaterialOverride) plus frustum extraction +
  AABB-vs-frustum + `cullByFrustum` for visibility, a 128-byte
  std140-friendly `InstanceLayoutEntry` for GPU upload, and an
  `UploadRing` per-frame staging arena. Vulkan/D3D12/WebGPU/software
  backends consume the same surface without translation.

## Requirements

- C++20 (`std::span`, `std::latch`, `std::function`).
- CMake ≥ 3.20.
- A threading-capable libc (linked via `Threads::Threads`).

Tested with GCC 16.1 on Linux. No third-party dependencies in the library
itself. The `examples/boids` target additionally requires SDL2 (skipped if
not found).

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build -j
```

Run the bundled headless example:

```sh
./build/examples/minimal/threadmaxx_minimal       # Ctrl-C to quit
./build/examples/minimal/threadmaxx_minimal 600   # run exactly 600 ticks
```

Expected output: `[frame]` lines every ~60 ticks, instance counts growing
as the spawner system fires, clean shutdown.

Run the test suite:

```sh
cd build && ctest --output-on-failure
```

CMake options:

| Option | Default | Effect |
| --- | --- | --- |
| `THREADMAXX_BUILD_EXAMPLES` | `ON` | Builds `examples/minimal` and (if SDL2 is found) `examples/boids`. |
| `THREADMAXX_BUILD_TESTS` | `ON` | Builds and registers the CTest suite under `tests/`. |
| `THREADMAXX_WARNINGS_AS_ERRORS` | `OFF` | Promotes the project's warning set (incl. `-Wsign-conversion`, `-Wconversion`, `-Wshadow`, `-Wold-style-cast`) to errors. The library compiles clean under it; keep it that way. |

If Doxygen is installed, the configure step registers an optional `doc`
target:

```sh
cmake --build build --target doc
# Output lands in doc/generated/html/index.html
```

The generated site includes this README, the user guide, the architecture
overview, and the full API reference.

## Using it

Link against the `threadmaxx::threadmaxx` target:

```cmake
add_subdirectory(threadmaxx)
target_link_libraries(my_game PRIVATE threadmaxx::threadmaxx)
```

A complete minimal game:

```cpp
#include <threadmaxx/threadmaxx.hpp>

class MovementSystem : public threadmaxx::ISystem {
public:
    const char* name() const noexcept override { return "movement"; }

    threadmaxx::ComponentSet reads()  const noexcept override {
        return threadmaxx::Component::Transform | threadmaxx::Component::Velocity;
    }
    threadmaxx::ComponentSet writes() const noexcept override {
        return threadmaxx::Component::Transform;
    }

    void update(threadmaxx::SystemContext& ctx) override {
        const auto dt = static_cast<float>(ctx.dt());
        threadmaxx::forEach<threadmaxx::Transform, threadmaxx::Velocity>(ctx,
            [dt](threadmaxx::EntityHandle e,
                 const threadmaxx::Transform& t,
                 const threadmaxx::Velocity& v,
                 threadmaxx::CommandBuffer& cb) {
                threadmaxx::Transform next = t;
                next.position = t.position + v.linear * dt;
                cb.setTransform(e, next);
            });
    }
};

class MyGame : public threadmaxx::IGame {
public:
    void onSetup(threadmaxx::Engine& engine, threadmaxx::World&,
                 threadmaxx::CommandBuffer& seed) override {
        engine.registerSystem(std::make_unique<MovementSystem>());
        // engine.setRenderer(&myRenderer);  // optional; null = headless
        seed.spawn(threadmaxx::Transform{},
                   threadmaxx::Velocity{{1, 0, 0}, {}});
    }
};

int main() {
    threadmaxx::Engine engine(threadmaxx::Config{});
    MyGame game;
    if (!engine.initialize(game)) return 1;
    engine.run();          // blocks until requestQuit()
    engine.shutdown();
}
```

To plug in a renderer, implement `threadmaxx::IRenderer::submitFrame` —
you get a `RenderFrame` with a `std::span<const RenderInstance>`. See
`examples/minimal/ConsoleRenderer.{hpp,cpp}` for the smallest possible
implementation and `examples/boids/SDLRenderer.{hpp,cpp}` for a real
SDL2-backed one.

## Architecture, briefly

```
sim thread:  ┌─ for each wave:
             │     each system's update() runs concurrently on helper threads
             │     within each: ctx.parallelFor → JobSystem dispatches
             │                                    (per-worker work-stealing deques)
             │     std::latch waits for the wave
             │     commit each system's buffers in registration order ← deterministic
             └─ build RenderFrame → atomic publish → renderer.submitFrame
```

Key invariants:

- Workers never mutate live world state; they emit commands into per-job
  `CommandBuffer`s. All mutations are applied on the sim thread, single-
  threaded, in submission order — same inputs → same world state.
- Systems declare `reads()` / `writes()` over a fixed set of component
  categories. The engine groups non-conflicting systems into waves and
  runs the wave's systems concurrently; defaults reduce cleanly to
  strict registration-order serial.
- The renderer sees only the published `RenderFrame`; the engine owns
  the storage and double-buffers it.
- Gameplay code never touches a mutex.

Full design + threading diagram: [`ARCHITECTURE.md`](ARCHITECTURE.md).
Roadmap and intentional gaps: [`FUTURE_WORK.md`](FUTURE_WORK.md).

## Documentation

- [User guide](doc/index.md) — multi-page walk-through covering core
  concepts, components, systems, command buffers, hierarchy, resources,
  renderer integration, configuration, and profiling.
- [Architecture overview](ARCHITECTURE.md) — design rationale and
  threading diagram.
- [Future work](FUTURE_WORK.md) — what's done, what's next, what's
  deliberately out of scope.
- Generated API reference: run `cmake --build build --target doc` and
  open `doc/generated/html/index.html`.

## Repository layout

```
include/threadmaxx/    public API (20 headers)
src/                   private implementation (PImpl)
examples/minimal/      headless console example
examples/boids/        SDL2 boids simulation
tests/                 52 no-dependency tests under CTest
doc/                   user guide (Markdown, also ingested by Doxygen)
Doxyfile               Doxygen config (optional `doc` target)
CMakeLists.txt
ARCHITECTURE.md        design + threading diagram
FUTURE_WORK.md         roadmap
CLAUDE.md              guidance for Claude Code sessions in this repo
```
