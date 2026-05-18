# Changelog

All notable changes to `threadmaxx` are recorded here. The format
follows [Keep a Changelog](https://keepachangelog.com/); the project
adheres to [Semantic Versioning](https://semver.org/). Bump rules
are documented in `include/threadmaxx/version.hpp`.

The sibling `threadmaxx_simd` library has its own independent
changelog at `include/threadmaxx_simd/CHANGELOG.md`.

## [1.0.0] — 2026-05-18 — Production-ready close-out

### Added

- **`include/threadmaxx/version.hpp`** — `THREADMAXX_VERSION_MAJOR/MINOR/PATCH`
  macros, packed `THREADMAXX_VERSION` integer, and `version_string()`
  for runtime logging.
- **`CHANGELOG.md`** — this file. Distills the batch history from
  `FUTURE_WORK.md` into per-release notes.
- **`cmake/threadmaxxConfig.cmake.in`** — install package config
  template (was referenced by CMakeLists.txt but missing on disk;
  `find_package(threadmaxx)` now works after install).
- **`tests/version_test.cpp`** — gates the version macros + helper.

### Changed

- **`CMakeLists.txt` `project()` VERSION**: 0.1.0 → 1.0.0.
- **README.md**: status flipped from "early but functional" to
  "production-ready (v1.0.0)"; doc index added; test count updated
  to 108.

### Batch summary up to 1.0.0

Every prior batch (1 → 24) landed under the 0.x line and is captured
in detail in `FUTURE_WORK.md`. The 1.0.0 release seals the public API.

---

## [0.24.0] — 2026-05-18 — Batch 24 (close §3.10.3 rolling items)

### Added

- **F13 — `World::forEachChunkOf(ComponentSet, fn)`** introspection
  helper. Iterates non-empty archetype chunks satisfying a mask
  predicate. Skips the engine's startup-allocated empty
  `all()`-masked chunk automatically.
- **F8 — `Engine::events<T>()` thread_local cache** with per-engine
  serial. Replaces ~30 ns mutex acquisition with a single
  atomic-load + compare on the hot path. UAF-safe across
  engine recreation at recycled addresses.
- **`Engine::engineSerial()`** `@internal`-tagged public accessor —
  returns the unique non-zero serial assigned at construction.
- 2 new tests: `tests/world_for_each_chunk_of_test.cpp` and
  `tests/event_channel_cache_test.cpp`.

---

## [0.23.0] — 2026-05-17 — Batch 23 (ergonomics rolling)

### Added

- **F11 — `Engine::userComponent<T>()`** — lazy lookup of a
  registered `UserComponentId` by `typeid(T)`.
- **F12 — `CommandBuffer::spawnBundleN(spans)`** — bulk-spawn
  helper pairing reserved handles with Bundles.
- **F10 — `Bundle::with<T>(value)`** — chainable builder that sets
  the field AND the matching presence bit.

---

## [0.22.0] — 2026-05-16 — Batch 22 (audit-driven hygiene)

### Changed

- Wave-parallel system updates route through `jobs_->submit` +
  `std::latch` instead of spawning raw `std::thread`s.
- `HierarchySystem` scratch state is now system-member state,
  reused across ticks.
- `prevTransformByIndex_` flat vector replaces the old
  `prevTransformMap_` `unordered_map`.

### Reverted

- F8 (event-channel `thread_local` cache) — initial attempt UAFed
  on engine recreation at recycled addresses; deferred to batch 24
  with a per-engine serial fix.

---

## [0.21.0] — 2026-05-16 — Batch 21 (sharded-commit fast paths)

### Added

- Pre-check fast-fallback in `commitBuffersSharded`: total commands
  < 256, no value-only commands, or chunk count < 2 → falls
  through to single-threaded `commitBuffer`.
- Engine-owned `shardMigratingBitmap_` + `shardChunkBins_` reused
  across calls.

### Documented

- Bench data showing sharded never beats single-threaded on the
  measured workloads. `singleThreadedCommit = true` remains the
  default.

---

## [0.20.0] — 2026-05-16 — Batches 16–20 (post-Milestone-6 perf)

### Added

- **Batch 16**: workload-realistic bench harness (`bench/`).
  4 scene-shaped end-to-end benches over 3 canonical workloads
  (AI-only, Render+AI, Churn).
- **Batch 17**: `forEachWith` rewritten to walk chunks internally
  (70% improvement on AI workload).
- **Batch 18**: `CmdSpawn` + `CmdAddUserComponent` moved behind
  `unique_ptr` in the variant — `Command` variant shrank from 256 B
  to 64 B.
- **Batch 19**: migration-batching hint in `EngineImpl::commitBuffer`.
- **Batch 20**: `FileTraceSink::setAsync(true)` + `Engine::snapshotAsync`
  for off-thread I/O.

---

## [0.15.0] — 2026-05-15 — Batch 15 (pre-batch-9 API polish + tests)

### Added

- **15a**: `IRenderer::onResize(w, h)` + `Engine::notifyResize`,
  `Engine::workerCount()`, `SystemStats::buildRenderFrameSeconds`,
  `RenderFrame::prevTransforms`,
  `RenderFrame::cameraIndexById`, owning-string `DebugText`
  overload, `ResourceHandle<T>::get()` / `->` / `*`.
- **15b**: industrial-grade test additions (concurrency soak,
  stitched-view concurrency, render-pass ordering, frame
  interpolation, file-trace rotation, visibility 32-camera cap,
  resize routing, resource-handle indirection, debug-text owning,
  per-system build_render_frame timing).

---

## [0.14.0] — 2026-05-15 — Batch 14 (telemetry ingestion)

### Added

- `ITraceSink` engine-streamed per-tick consumer.
- `FileTraceSink` (rolling Chrome-trace JSON).
- `HudTraceSink` (seqlock-protected latest snapshot).
- `FrameBudgetWatcher` system + `BudgetExceeded` event.
- Stall watchdog via `Engine::setStallTimeout` + `EngineStall` event.

---

## [0.13.0] — 2026-05-15 — Batches 13a/b/c (storage contention)

### Added

- **13a**: per-tick `commitHash` (FNV-1a-64) determinism safety net.
  `Config::singleThreadedCommit` toggle + `logCommitHashEvery`.
- **13b**: sharded commit path (`commitBuffersSharded`) — opt-in
  parallel apply when `singleThreadedCommit = false`.
- **13c**: lock-free MPSC event channels (Treiber-stack `emit`),
  `WorldView` wave-scoped chunk-pointer cache, opt-in benchmarks.

---

## [0.12.0] — 2026-05-14 — Batch 12 (cancellation + budgets)

### Added

- `Engine::setTickBudget(seconds)` + `SkipPolicy::Budget` / `Scripted`.
- `ISystem::skippable()` opt-in flag.
- `SystemContext::shouldYield()` cooperative-cancel poll.
- `SystemSkipped` event POD.
- `JobPriority` (`High` / `Normal` / `Low`) on `parallelFor`.
- `IResourceLoader::cancel(Engine&)` per-tick cancellation hook.

---

## [0.11.0] — 2026-05-14 — Batch 11 (frame task graph)

### Added

- `TaskTag { string_view name; uint64_t hash }` POD.
- `ISystem::dependencies()` / `provides()` / `preferredGrain()`
  optional virtuals.
- Rewritten `rebuildWaves` (topological sort + greedy packing).
- `Engine::taskGraphSnapshot()` → `vector<TaskGraphNode>` for
  HUD / Graphviz export.

---

## [0.10.0] — 2026-05-15 — Batch 10 (RPG demo example)

### Added

- `examples/rpg_demo/` — 3D RPG demo (1 player, 50 NPCs, 100
  pickups) integrating combat, save/load, multi-camera frustum
  culling, quest tracking, scale stress, procedural animation,
  preload + hot reload.
- 10 demo systems + headless test suite at `tests/rpg_demo/`.

---

## [0.9.0] — 2026-05-15 — Batches 9 + 9b.x (Vulkan reference renderer)

### Added

- **Batch 9**: Vulkan 1.3 reference renderer at
  `examples/vulkan_renderer/`. VK_KHR_dynamic_rendering pipeline,
  multi-camera viewport support, mesh / texture / shader loaders.
- **Batch 9b.1**: OBJ mesh parser (sibling-library in
  `examples/rpg_demo/`).
- **Batch 9b.2a**: `MeshLoader::createMesh` + `VulkanRenderer::setDefaultMesh*`
  end-to-end OBJ→GPU upload.
- **Batch 9b.2b**: multi-mesh dispatch (per-meshId draw loop +
  `VulkanRenderer::registerMesh*` + `CubeRender::meshId`).
- **Batch 9b.3**: shader file I/O + renderer-side `AssetReloaded`
  pipeline rebuild.

### Deferred to v1.x

- **Batch 9b.4**: full Vulkan bone-weighted skinning pipeline.

---

## [0.8.0] — 2026-05-14 — Batch 8 (render contract expansion)

### Added

- `include/threadmaxx/render/` hierarchical render PODs (Camera,
  Light, DrawItem with auxiliary refs, DebugLine / DebugPoint /
  DebugText, RenderPasses, RenderFrameBuilder, Visibility frustum
  helpers, InstanceBufferLayout, UploadRing).
- `ISystem::buildRenderFrame(RenderFrameBuilder&)` lifecycle hook.
- `RenderFrame` now carries both flat `instances` (Milestone-1
  auto-populated) and hierarchical `drawItems[pass]` / `cameras` /
  `lights` / `debugLines` / `debugPoints` / `debugText`.

---

## [0.7.0] — 2026-05-14 — Batch 7 (resource & event maturity)

### Added

- `ResourceHandle<T>` (refcounted RAII), `ResourceRegistry::addRefCounted`
  / `acquire` / `refCount`.
- Hot reload: `Engine::markResourceStale<T>(id)` + `EventChannel<AssetReloaded>`.
- `Engine::preloadUntil(predicate, timeout)` blocking yield loop.
- `IResourceLoader::onShutdown` / `markStale` / `stats` virtuals;
  `LoaderStats` POD; `Engine::aggregateLoaderStats()`.
- `Subscription` RAII subscription handle via
  `EventChannel<T>::subscribeScoped`.

---

## [0.6.0] — 2026-05-14 — Batches 6 / 6a / 6b (archetype storage)

### Added

- **Batch 6a**: generic `CommandBuffer::addComponent<T>` /
  `removeComponent<T>`, `World::archetypeSignatures()`,
  `archetype_storage_stress_test.cpp` baseline.
- **Batch 6**: chunked archetype storage. `ArchetypeChunk` /
  `ArchetypeTable`; physical migration on mask change;
  `forEachChunk<Required...>` parallel iterator;
  `World::archetypeChunkCount()` / `archetypeChunk(i)`; lazy
  stitched view for legacy parallel-vector API.
- **Batch 6b**: `UserComponent<T>` extension hook.
  `Engine::registerUserComponent<T>` → `UserComponentId`;
  `addUserComponent` / `removeUserComponent`; `user::has` /
  `tryGet` / `chunkSpan`; `World::locate`.

---

## [0.5.0] — 2026-05-13 — Batch 5 (data model widening)

### Added

- `ComponentSet` widened to 64 bits.
- 6 new POD components: `Health`, `Faction`, `AnimationStateRef`,
  `PhysicsBodyRef`, `NavAgentRef`, `BoundingVolume`.
- 3 tag-only categories: `StaticTag`, `DisabledTag`, `DestroyedTag`.
- `CommandBuffer::addTag` / `removeTag`; `World::hasTag`.
- `MaskCache` + `forEachWithCached`.
- N-tick determinism golden test.
- `kWorldSnapshotVersion` bumped 1 → 2.

---

## [0.4.0] — 2026-05-13 — Batch 4 (observability + M1 polish)

### Added

- `JobSystemStats::jobDurationHistogram` (16 log2-µs bins).
- `SystemStats::waitSeconds` + `peakQueueDepth`.
- `ChromeTraceWriter` (move-only streaming sink).
- `ILogger` + `DefaultLogger` (engine routes startup/shutdown messages).
- Persistent `EventChannel::subscribe` / `unsubscribe`.
- `HierarchyConfig::propagateScale` opt-in knob.
- `Serialization.hpp` + `WorldSnapshot` + `World::snapshot()`.
- `registerSystemAt` (insertion-order injection).
- `Bundle` / `spawnBundle` variadic spawn helper.
- `World::has<T>` / `get<T>` / `hasTag`.
- `EventChannel::subscribeScoped` (RAII `Subscription`).

---

## [0.3.0] — 2026-05-13 — Batch 3 (ergonomics + tracing)

### Added

- `Engine::reserveEntityHandle` + `SystemContext::reserveHandle` +
  batch variants.
- `ScratchArena` (chained-slab bump allocator).
- `EventChannel<T>` typed double-buffered queue + tick-end drain.
- `Engine::setTimeScale` + `Engine::setPaused`.
- `FrameSnapshot` + `writeJsonLines`.
- `IResourceLoader` per-tick async loader pump.
- `SpatialHash<Payload>` uniform-grid index.
- Parent auto-derive in `defaultSpawnMask`.
- `EventChannel<AssetReloaded>` event POD.

---

## [0.2.0] — 2026-05-13 — Batch 2 (lifecycle + time control)

### Added

- `ISystem::preStep` / `postStep` lifecycle hooks (serial).
- `ScratchArena` (chained slabs).
- `EventChannel<T>` initial implementation.
- Reserved entity handles.
- `Engine::quitRequested()`.

---

## [0.1.0] — 2026-05-12 — Batch 1 (initial public release)

### Added

- Core simulation loop with fixed-step `step()` + interpolated `run()`.
- `Engine` + `World` + `ISystem` + `CommandBuffer` + `IGame`.
- Per-worker work-stealing `JobSystem`.
- `SystemContext::parallelFor` / `single`.
- Wave scheduler based on `ISystem::reads` / `writes` masks.
- `EngineStats` + `SystemStats` per-tick reporting.
- `World::transforms` / `velocities` / `renderTags` / `userData`
  stitched parallel-vector views.
- `IRenderer` interface + `RenderFrame` (flat `instances` lane).
- Basic `IResourceLoader` registration.
- `examples/minimal/` smoke + `examples/boids/` SDL2 demo.
