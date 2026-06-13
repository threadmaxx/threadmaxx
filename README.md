# threadmaxx

A renderer-agnostic C++20 game backend with a fixed-step simulation loop
and a work-stealing job system. Game code registers `ISystem`s (movement,
physics, AI, render-prep, ...) that read the world in parallel and emit
commands; the engine commits them deterministically and hands a flat
`RenderFrame` to whatever renderer you plug in.

**Status: core v1.2.1; full suite shipped.** The core engine sealed at
v1.0.0 on 2026-05-18 and has shipped two additive minor bumps since.
Thirteen sibling libraries — `simd`, `reflect`, `editor`, `assets`,
`input`, `audio`, `animation`, `navmesh`, `physics`, `ui`, `network`,
`migration`, `studio` — are all at their own v1.0.0. Each has its own
semver line and its own headless test suite; ASAN / UBSAN / TSAN trees
pass clean across the whole repository.

## Documentation map

| Document | When to read it |
|---|---|
| **`README.md`** (this file) | Top-level overview, build/run, the minimal example |
| [`ARCHITECTURE.md`](ARCHITECTURE.md) | Deep implementation details — subsystems, threading model, invariants |
| [`CHANGELOG.md`](CHANGELOG.md) | Per-release notes back to 0.1.0 (core only; siblings have their own) |
| [`CLAUDE.md`](CLAUDE.md) | Contributor playbook — recipes, gotchas, "what to grep when" |
| [`doc/index.md`](doc/index.md) | Multi-page user guide for the core engine (also ingested by Doxygen) |
| [`FUTURE_WORK.md`](FUTURE_WORK.md) | What's deferred / out of scope |
| [`tests/COVERAGE_AUDIT.md`](tests/COVERAGE_AUDIT.md) | Public-API coverage record |

Each sibling library has its own README + CHANGELOG under
`include/threadmaxx_<name>/`. The [Sibling libraries](#sibling-libraries)
section below indexes every one.

The Doxygen API reference is buildable via `cmake --build build --target doc`
→ `doc/generated/html/index.html`.

## Requirements

- A C++20 compiler (`std::span`, `std::variant`, `std::atomic<…>`,
  `std::function`). Tested with GCC 16.1 on Linux.
- CMake ≥ 3.20.
- A threading-capable libc (linked via `Threads::Threads`).
- **No third-party dependencies in the core library.**

Optional, only for the renderer examples:

- `examples/boids` — SDL2 (skipped if not found).
- `examples/vulkan_renderer` — Vulkan 1.3 SDK, GLFW3, `glslc` (silently
  skipped if any of the three is missing).
- `examples/rpg_demo` — same Vulkan toolchain as above.
- `examples/tou2d` — Vulkan + GLFW + `glslc` (2D arena combat demo).

Sibling-library demos under `examples/` (`assets_demo`, `audio_demo`,
`editor_demo`, `input_demo`, `navmesh_bake`, `physics_demo`,
`reflect_demo`, `studio_demo`, `ui_demo`, `minimal`) auto-skip when
their sibling target isn't built.

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build -j
cd build && ctest --output-on-failure          # run the test suite
```

`-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` is what clangd needs — without it
you'll see spurious include-path / `cxx_std_20` errors in editors.

### Run the headless smoke

```sh
./build/examples/minimal/threadmaxx_minimal           # Ctrl-C to quit
./build/examples/minimal/threadmaxx_minimal 600       # bounded: 600 ticks then exit
```

Successful runs print `[frame]` lines with monotonically increasing
ticks and end with `[ConsoleRenderer] shutdown after N frames`.

### Run a real renderer

If the Vulkan SDK + GLFW + `glslc` are installed, both Vulkan-based
examples are built:

```sh
./build/examples/vulkan_renderer/threadmaxx_vulkan_smoke           # spinning cube smoke
THREADMAXX_VK_VALIDATE=1 ./build/examples/vulkan_renderer/threadmaxx_vulkan_smoke 300
./build/examples/rpg_demo/threadmaxx_rpg_demo                      # terrain, player, NPCs, pickups, HUD
./build/examples/rpg_demo/threadmaxx_rpg_demo 600                  # bounded: 600 ticks
```

`rpg_demo` controls: WASD move, arrow keys rotate camera, Q/E zoom,
F1 toggle Chrome-trace capture, F5 quick-save, F9 diagnose save.

### CMake options

| Option | Default | Effect |
|---|---|---|
| `THREADMAXX_BUILD_EXAMPLES` | `ON` | Builds `examples/minimal` plus the renderer examples whose dependencies are found |
| `THREADMAXX_BUILD_TESTS` | `ON` | Builds + registers the CTest suite |
| `THREADMAXX_BUILD_BENCHMARKS` | `OFF` | Builds `bench/` binaries (run by hand; not in CTest) |
| `THREADMAXX_BUILD_LONG_SOAK` | `OFF` | Builds `tests/concurrency_soak_long.cpp` (~5-6 min runtime) |
| `THREADMAXX_WARNINGS_AS_ERRORS` | `OFF` | Promotes `-Wsign-conversion -Wconversion -Wshadow -Wold-style-cast` to errors. The project compiles clean under it — keep it that way. |
| `THREADMAXX_BUILD_SIMD` | `ON` | Builds the header-only `threadmaxx::simd` sibling library and includes it in the install set. |
| `THREADMAXX_BUILD_<SIBLING>` | `ON` | One per sibling library (`REFLECT`, `EDITOR`, `ASSETS`, `INPUT`, `AUDIO`, `ANIMATION`, `NAVMESH`, `PHYSICS`, `UI`, `NETWORK`, `MIGRATION`, `STUDIO`). Each silently no-ops if an external dependency it needs is missing. |
| `THREADMAXX_INSTALL` | `ON` when top-level, `OFF` when `add_subdirectory`'d | Generates the `install(...)` rules (headers, static lib, package config). |

## Install

After building, install to the system prefix or a custom location:

```sh
cmake --install build                              # default prefix (/usr/local on Linux)
cmake --install build --prefix /opt/threadmaxx     # custom prefix
```

What lands in the install tree:

- `<prefix>/include/threadmaxx/…` — every public core header.
- `<prefix>/include/threadmaxx_simd/…` — sibling SIMD headers (when
  `THREADMAXX_BUILD_SIMD=ON`, the default).
- `<prefix>/lib/libthreadmaxx.a` — the static core library.
- `<prefix>/lib/cmake/threadmaxx/{threadmaxxConfig,threadmaxxConfigVersion,threadmaxxTargets}.cmake`
  — package config + exported targets.

The SIMD library is header-only, so the install only adds its headers and
exports its `INTERFACE` target — no extra `.a` file.

### Consume from another project

After installing, downstream `CMakeLists.txt` finds and links:

```cmake
find_package(threadmaxx 1.2 CONFIG REQUIRED)
target_link_libraries(my_game PRIVATE threadmaxx::threadmaxx)

# Optional sibling SIMD library — present when SIMD was built+installed:
if (TARGET threadmaxx::simd)
    target_link_libraries(my_game PRIVATE threadmaxx::simd)
endif()
```

If you installed to a non-default prefix:

```sh
cmake -DCMAKE_PREFIX_PATH=/opt/threadmaxx -S . -B build
# or, more targeted:
cmake -Dthreadmaxx_DIR=/opt/threadmaxx/lib/cmake/threadmaxx -S . -B build
```

The package config re-finds the one external dependency (`Threads`) for
you, so consumers don't have to.

## A minimal game

Drop the engine into your CMake project — either install it and
`find_package(threadmaxx CONFIG REQUIRED)` (see [Install](#install)) or
vendor the source and use `add_subdirectory`:

```cmake
add_subdirectory(threadmaxx)
target_link_libraries(my_game PRIVATE threadmaxx::threadmaxx)
```

Then:

```cpp
#include <threadmaxx/threadmaxx.hpp>

using namespace threadmaxx;

class MovementSystem : public ISystem {
public:
    const char*  name()   const noexcept override { return "movement"; }
    ComponentSet reads()  const noexcept override {
        return Component::Transform | Component::Velocity;
    }
    ComponentSet writes() const noexcept override { return Component::Transform; }

    void update(SystemContext& ctx) override {
        const auto dt = static_cast<float>(ctx.dt());
        forEachChunk<Transform, Velocity>(ctx,
            [dt](std::span<const EntityHandle> es,
                 std::span<const Transform>    ts,
                 std::span<const Velocity>     vs,
                 CommandBuffer&                cb) {
                for (std::size_t i = 0; i < es.size(); ++i) {
                    Transform next = ts[i];
                    next.position = ts[i].position + vs[i].linear * dt;
                    cb.setTransform(es[i], next);
                }
            });
    }
};

class MyGame : public IGame {
public:
    void onSetup(Engine& engine, World&, CommandBuffer& seed) override {
        engine.registerSystem(std::make_unique<MovementSystem>());
        // engine.setRenderer(&myRenderer);   // optional; null = headless
        seed.spawnBundle(bundle(Transform{}, Velocity{{1, 0, 0}, {}}));
    }
};

int main() {
    Engine engine(Config{});
    MyGame game;
    if (!engine.initialize(game)) return 1;
    engine.run();                            // blocks until requestQuit()
    engine.shutdown();
}
```

To plug in a renderer, implement `IRenderer::submitFrame(const RenderFrame&)`.
See `examples/minimal/ConsoleRenderer.{hpp,cpp}` for the smallest possible
implementation, `examples/boids/SDLRenderer.{hpp,cpp}` for an SDL2 path,
and `examples/vulkan_renderer/` for a Vulkan 1.3 reference renderer
shipped as a reusable static library.

## What's inside

The full design — every subsystem, every invariant, every load-bearing
implementation detail — lives in [`ARCHITECTURE.md`](ARCHITECTURE.md).
This section is the index.

### Simulation loop

- **Fixed-step `Engine::step()`.** Phase order: `applyPendingTuningPatch`
  → `preStep` (serial, registration order) → waves (parallel update +
  per-system commit) → `postStep` (serial) → resource loader pumps →
  reservation reap → event drain → `buildRenderFrame` hooks → engine
  render frame build + atomic publish → `submitFrame`. Pause / time-scale
  are first-class (`setPaused`, `setTimeScale`).
- **Wave scheduler.** Systems declare `reads()` / `writes()`
  `ComponentSet`s; the engine packs non-conflicting systems into the same
  wave. Optional task-graph edges via `ISystem::dependencies()` /
  `provides()` + `TaskTag` push producer/consumer pairs into different
  waves even when their masks don't conflict. Cycles are detected, logged,
  and broken — never crash. `Engine::taskGraphSnapshot()` exposes the
  DAG for HUD overlays or Graphviz export.
- **Three-phase tick.** `preStep` for input/scratch reset, `update` for
  parallel work, `postStep` for per-tick aggregates and event publishes.
  `buildRenderFrame` is a separate render-prep hook in registration order.
- **Tick budget + cooperative skip.** `Engine::setTickBudget(s)` caps
  per-tick wall-clock; opt-in `ISystem::skippable()` systems drop their
  `update()` when the budget is blown. `SkipPolicy::Scripted` replays a
  captured skip log for deterministic lockstep networking. Every skip
  publishes a `SystemSkipped` event.

### Work-stealing job system

- Per-worker priority deques (`High` / `Normal` / `Low`); cross-worker
  steals use `try_lock` so an active producer can't block.
- **Missed-wakeup fix (v1.2.1).** A monotonic `wakeSeq_` atomic + a gated
  helper-notify guarantee that work pushed onto a busy worker's queue is
  picked up by parked siblings. Pinned by
  `tests/job_system_missed_wakeup_test.cpp`. See
  [`ARCHITECTURE.md`](ARCHITECTURE.md) § 7.2 for the full wake contract.
- **`JobLatch` spin-before-block** (`Config::jobLatchSpinIters`,
  default 4096) skips the kernel sleep + wakeup IPI when workers finish
  within ≈10–40 µs; mutex is re-acquired on the win path so
  stack-allocated latches are safe.
- 16-bin log2-µs `jobDurationHistogram` on `JobSystemStats` for spotting
  long-tail jobs.

### Storage and queries

- **Chunked archetype storage.** Entities live in `ArchetypeChunk`
  instances keyed by per-entity `ComponentSet`. Mask edits swap-and-pop
  the entity between chunks during commit; runs of same-`(srcArch,
  dstMask)` mutations dispatch through a batched migrate path when
  ≥ `Config::batchMigrateThreshold` (default 16).
- **Wave-scoped `WorldView`.** `SystemContext::worldView()` exposes a
  flat span of chunk pointers, cached at wave start, shared across every
  system in the wave. Capture into worker lambdas without paying repeated
  `archetypeChunk(i)` indirection.
- **Query helpers.** `forEachChunk<T...>` (recommended for hot paths —
  no stitched-view rebuild, no per-entity mask test, contiguous
  per-archetype layout), `forEachWith<T...>`, `forEach<T...>`, and a
  `MaskCache + forEachWithCached` opt-in for stable-shape queries that
  pre-compute matching indices in `preStep`.
- **Per-entity component-presence mask.** 64-bit `ComponentSet` over 16
  built-in categories (Transform, Velocity, Acceleration, RenderTag,
  UserData, Parent, Health, Faction, AnimationStateRef, PhysicsBodyRef,
  NavAgentRef, BoundingVolume + three tag-only flags StaticTag /
  DisabledTag / DestroyedTag). 48 spare bits.
- **`Bundle` + `spawnBundle` / `spawnBundleN`.** Variadic `bundle(Cs...)`
  factory yields a compile-time-derived presence mask;
  `cb.spawnBundleN(reservedSpan, bundlesSpan)` is the bulk path.
- **Reserved spawn handles.** `Engine::reserveEntityHandle()` /
  `Engine::reserveEntityHandles(count, span)` (one-mutex batch form) so a
  single job can spawn a parent and a child with the parent's handle
  wired into the child's `Parent` field.
- **User components.** `Engine::registerUserComponent<T>()` returns a
  `UserComponentId` for game-side dense columns; same migration
  semantics as built-ins via `addUserComponent<T>(cb, id, e, v)` /
  `user::has` / `user::tryGet<T>` / `user::chunkSpan<T>`. Trivially-
  copyable PODs only.
- **Built-in hierarchy.** `Parent` component + `makeHierarchySystem()`
  factory; one DFS-with-memoization pass per tick over multi-level
  chains. `HierarchyConfig::propagateScale` opts in to chained scale.
- **`SpatialHash<Payload>`** header-only uniform-grid index for
  neighbour lookups, broadphase, AOI streaming.
- **Save/load.** `World::snapshot()` copies dense arrays into a
  `WorldSnapshot`; header-only `serialize` / `deserialize` round-trip
  through any `std::ostream` / `std::istream`.

### Commit and determinism

- **Workers never mutate live world state.** Each job receives a
  `const World&` plus a private `CommandBuffer`. Mutations apply on the
  sim thread (single-threaded path) or on workers writing to disjoint
  chunks (sharded path) — never both.
- **Sharded commit** (`Config::singleThreadedCommit = false`) fans
  value-only setters into per-destination-chunk bins for worker apply.
  Record-time per-chunk routing (S8), inline-largest-bin (S9), spin
  latch (S11), and workload-aware fallthrough (S16) keep the wins
  consistent across workload shapes. The reference single-threaded
  path remains the default; same-input → same world state on both.
- **Per-archetype hash determinism (v1.3 default).** Every chunk carries
  a `cachedHash` + `hashDirty` flag; end-of-step rollup re-hashes dirty
  chunks (parallel when ≥ 2) and folds them sorted by `mask.bits()` into
  `EngineStats::commitHash`. Two clients on the same seed catch
  divergence on the first wrong tick. `Config::legacyCommitHash = true`
  preserves the v1.x byte-per-command FNV-1a-64 mix.

### Rendering

- **Pluggable renderer.** Implement `IRenderer::submitFrame` against a
  flat `RenderFrame`; null renderer = headless. `IRenderer::onResize`
  receives window-resize notifications forwarded via
  `Engine::notifyResize(w, h)`.
- **Hierarchical render contract.** `RenderFrame` exposes cameras, lights,
  per-pass draw-item bins (Opaque / Transparent / ShadowCasters / Overlay),
  debug geometry, and a `prevTransforms` span paired with `instances` so
  renderers can `lerp(prev, current, alpha)` without maintaining their
  own history. Systems populate the hierarchical fields via the
  `ISystem::buildRenderFrame(RenderFrameBuilder&)` hook; the engine
  merges every system's slice in registration order on the sim thread.
- **Renderer-neutral PODs** in `include/threadmaxx/render/`:
  `Camera`, `Light`, `DrawItem`, `DebugLine` / `DebugPoint` / `DebugText`,
  frustum extraction + `cullByFrustum`, the 128-byte std140-friendly
  `InstanceLayoutEntry`, and an `UploadRing` per-frame staging arena.

### Resources

- **Typed `ResourceRegistry`.** `ResourceId<T>` (index + generation;
  stale handles never alias new slots) under a single internal mutex.
- **Refcounted handles.** `addRefCounted<T>` + `acquire` return a
  `ResourceHandle<T>` whose destruction frees the slot on last drop.
  The legacy `add` / `remove` pair stays as the un-managed path.
- **`IResourceLoader`.** Pumped once per tick on the sim thread after
  the last `postStep`. `cancel(engine)` is called before `update()` so
  loaders can drop newly-stale requests in the same tick.
  `aggregateLoaderStats()` sums per-loader stats for HUD progress.
- **Hot reload.** `Engine::markResourceStale<T>(id)` dispatches to every
  loader; the loader replaces the asset and emits an `AssetReloaded`
  event for subscribers to rewire cached ids.
- **Boot-time preload.** `Engine::preloadUntil(predicate, timeout)`
  yields-pumps every loader until a named asset set is ready —
  simulation does not advance.

### Events

- **Typed `EventChannel<T>`.** `Engine::events<T>()` returns the
  engine-owned channel; same instance across calls and threads.
- **Lock-free MPSC emit.** Treiber-stack CAS prepend; safe from any
  thread including worker jobs. Subscriber callbacks fire on the sim
  thread during the tick-boundary drain; a callback that re-emits lands
  on the next tick safely.
- **`subscribeScoped(fn) → Subscription`.** RAII handle, move-only,
  type-erased, auto-detaches on destruction, safe to outlive the channel
  (holds a `weak_ptr` to the subscriber list).

### Telemetry

- **Per-tick instrumentation.** `EngineStats` (step/commit timings,
  `commitHash`, EWMA), `SystemStats` (per-system update/wait/peakQueue/
  avgSubJobMicros), `JobSystemStats` (own-pops, steals, histogram).
  No opt-in cost.
- **`FrameSnapshot`** bundles the three consistently. `writeJsonLines`
  emits one newline-terminated JSON object per tick; `ChromeTraceWriter`
  streams a Chrome trace JSON loadable in `chrome://tracing` / Perfetto.
- **`ITraceSink`** for streaming per-tick consumption. Built-in
  `FileTraceSink` writes rolling Chrome trace JSON with optional async
  background-thread writer (`setAsync(true)`); `HudTraceSink` exposes a
  seqlock-protected `LatestTelemetry` POD that a HUD thread polls
  lock-free.
- **`FrameBudgetWatcher`** built-in system emits `BudgetExceeded` events
  when a tick exceeds a target. **`Engine::setStallTimeout(seconds)`**
  installs a background watchdog that emits `EngineStall` events when a
  tick runs too long.
- **Async snapshot.** `Engine::snapshotAsync(callback)` captures
  `world().snapshot()` synchronously on the sim thread, then runs the
  user's I/O callback on an engine-owned writer thread.
- **`Engine::lastCommitBreakdown()`** exposes the per-step Pass A/B/C
  wall-clock + counters of the sharded commit path.

### Adaptive tuning (opt-in)

- **`ITuningPolicy`** observes `EngineStats` / `SystemStats` /
  `JobSystemStats` once per `step()` and may stage a `TuningPatch`
  applied at the top of the next step (before `preStep`, never mid-wave).
- **`AdaptiveGrainPolicy`** is the built-in policy — per-system EWMA
  drives `preferredGrain` toward a target sub-job microsecond band.
- **`TuningMode::{Off, Active, Scripted}`.** Scripted mode replays a
  captured `TuningTrace`, so two clients on the same input + same
  scripted patch stream produce bit-identical `commitHash` streams.

### Logging

- **`ILogger`** for engine lifecycle / registration / loader-error
  messages. Default sink writes warnings to `std::cerr`; install your
  own to route to spdlog / a HUD / a file.

## Architecture, briefly

```
sim thread
  step()
    preStep   (serial, registration order; commits flush immediately)
    for each wave:
      WorldView rebuilt at wave start
      sibling systems dispatched via JobSystem (size − 1 jobs); tail runs on sim
      each system: ctx.parallelFor → JobSystem dispatches across workers
        (per-worker priority deques, work-stealing, missed-wakeup safe)
      JobLatch barrier (spin-then-block)
      commit each system's buffers in registration order
        single-threaded   → serial apply on sim
        sharded           → Pass A (migrating bitmap) →
                            Pass B (classify; global lane; bin value-only) →
                            Pass C (workers + sim apply per-chunk bins)
    postStep                 (serial)
    resource loaders pump    (cancel + update, in registration order)
    reservation reap, event drain
    buildRenderFrame hooks   (serial)
    engine renders frame back-buffer; atomic publish; renderer.submitFrame(front)
```

Key invariants:

- Workers read `const World&` / `WorldView`, write to private
  `CommandBuffer`. The only exception is `reserveHandle` (its own
  mutex; slot-allocator only).
- Commit order is submission order, never execution order.
- The renderer sees only the published `RenderFrame`; the engine owns
  the storage and double-buffers it.
- Same command stream → same final per-archetype state, byte-for-byte,
  across runs and machines. `EngineStats::commitHash` is the runtime
  safety net.

Full design + diagram: [`ARCHITECTURE.md`](ARCHITECTURE.md).

## Sibling libraries

Each sibling lives under `include/threadmaxx_<name>/` and `src/threadmaxx_<name>/`,
ships its own README + CHANGELOG, and tracks its own semver line. The
core engine never depends on a sibling; siblings depend on the core
(and occasionally on each other, e.g. `studio → editor → reflect`).

| Library | Status | What it does |
|---|---|---|
| [`threadmaxx_simd`](include/threadmaxx_simd/README.md) | v1.0.0 | Header-only AVX2 batch kernels over engine PODs |
| [`threadmaxx_reflect`](include/threadmaxx_reflect/README.md) | v1.0.0 | Lightweight runtime reflection / type registry |
| [`threadmaxx_editor`](include/threadmaxx_editor/README.md) | v1.0.0 | Renderer-neutral editor primitives — `IEditorBackend`, `CommandStack`, selection, hierarchy view, property editor, console |
| [`threadmaxx_assets`](include/threadmaxx_assets/README.md) | v1.0.0 | Asset pipeline — IDs, manifests, dependency graph, hot-reload bridge |
| [`threadmaxx_input`](include/threadmaxx_input/README.md) | v1.0.0 | Action / binding system, device adapters, replay log |
| [`threadmaxx_audio`](include/threadmaxx_audio/README.md) | v1.0.0 | Voice mixer + 3D audio bus |
| [`threadmaxx_animation`](include/threadmaxx_animation/README.md) | v1.0.0 | Skeleton, clip blending, IK helpers, crowd evaluation |
| [`threadmaxx_navmesh`](include/threadmaxx_navmesh/README.md) | v1.0.0 | Navmesh build + path query (batched A\* solver) |
| [`threadmaxx_physics`](include/threadmaxx_physics/README.md) | v1.0.0 | Jolt-backed rigid-body integration (gated on Jolt availability) |
| [`threadmaxx_ui`](include/threadmaxx_ui/README.md) | v1.0.0 | Retained-mode UI tree + frame builder |
| [`threadmaxx_network`](include/threadmaxx_network/README.md) | v1.0.0 | Transport abstraction + RPC hub (loopback + remote) |
| [`threadmaxx_migration`](include/threadmaxx_migration/README.md) | v1.0.0 | Save-file versioning, schema evolution, offline converter |
| [`threadmaxx_studio`](include/threadmaxx_studio/README.md) | v1.0.0 | Panel host + attach environment (in-process or remote) for every sibling |

The umbrella `threadmaxx::threadmaxx` target still has zero
third-party dependencies — every sibling that needs one (Jolt for
physics, GLFW for the input demo, etc.) gates on `find_package`.

## Repository layout

```
include/threadmaxx/             core public API (~30 headers + internal/, render/)
include/threadmaxx_simd/        sibling SIMD library (independent semver)
include/threadmaxx_reflect/     sibling reflection
include/threadmaxx_editor/      sibling editor primitives
include/threadmaxx_assets/      sibling asset pipeline
include/threadmaxx_input/       sibling input mapping
include/threadmaxx_audio/       sibling audio mixer
include/threadmaxx_animation/   sibling animation
include/threadmaxx_navmesh/     sibling navmesh
include/threadmaxx_physics/     sibling physics
include/threadmaxx_ui/          sibling UI
include/threadmaxx_network/     sibling networking
include/threadmaxx_migration/   sibling save-file migration
include/threadmaxx_studio/      sibling studio (panel host)
src/                            private engine implementation + per-sibling private cpp
examples/minimal/               headless console example (integration smoke)
examples/boids/                 SDL2 boids example
examples/vulkan_renderer/       Vulkan 1.3 reference renderer (static lib + smoke)
examples/rpg_demo/              3D RPG demo on top of the Vulkan renderer
examples/tou2d/                 2D arena combat demo (Vulkan)
examples/studio_demo/           end-to-end studio drive (Shape A + Shape B)
examples/{assets,audio,editor,input,navmesh_bake,physics,reflect,ui}_demo/
                                per-sibling demos (auto-skipped if sibling off)
tools/migration_convert/        offline save-file converter executable
bench/                          standalone microbenchmarks (opt-in)
tests/                          no-dependency tests under CTest (250+ executables)
doc/                            multi-page user guide for the core (Markdown + Doxygen)
ARCHITECTURE.md                 deep implementation overview (core engine)
CHANGELOG.md                    per-release notes (core engine)
CLAUDE.md                       contributor playbook (core engine)
FUTURE_WORK.md                  roadmap / deliberate gaps (core engine)
```

## Versioning

Every published library follows [Semantic Versioning](https://semver.org/).
Bump rules for the core engine (documented in
[`include/threadmaxx/version.hpp`](include/threadmaxx/version.hpp)):

- **MAJOR** — breaking public API change.
- **MINOR** — additive change (new method / header / component / event /
  feature flag); source- and binary-compatible.
- **PATCH** — bug fix or doc improvement; no public API change.

Each sibling tracks its own semver line under
`include/threadmaxx_<name>/CHANGELOG.md`. They release independently —
a core PATCH bump never forces a sibling rebuild.
