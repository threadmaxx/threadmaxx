# threadmaxx Public-API Test Coverage Audit

Generated for §3.8 batch 32 ("Sanitizer + soak hygiene pass"). Captures
the surface inventory at the time of the audit; refresh this file when
new public symbols land.

Scope: every public header under `include/threadmaxx/` (excluding `internal/`).
Tests under `tests/` (now 93 files; 88 ctest targets after B32).
"Covered" = at least one test file calls / instantiates the symbol; gaps are
flagged load-bearing (a method real users call) vs trivial (default-virtual
no-ops, getters that share storage with a covered field, etc.).

## B32 close-out — gaps closed by extending existing tests

All six load-bearing gaps were resolved as part of B32:

1. ✓ `Bundle::with<T>` — `bundle_test.cpp` end-of-file extension.
2. ✓ `Engine::clearScriptedSkips` — `cancellation_test.cpp` new
   trailing scenario.
3. ✓ `forEachSerial<...>` — new `for_each_serial_test.cpp`.
4. ✓ `EngineStats::engineBuildRenderFrameSeconds` /
   `renderSubmitSeconds` — `build_render_frame_seconds_test.cpp`
   sleepy-renderer extension.
5. ✓ `World::tryGetAnimationStateRef` / `tryGetPhysicsBodyRef` /
   `tryGetNavAgentRef` / `tryGetBoundingVolume` —
   `new_components_test.cpp` end-of-file extension.
6. ✓ `Viewport` (render/Camera.hpp) + `LightType` switched values —
   `render_frame_builder_test.cpp` CameraEmitterA extension.

The ~18 trivial gaps (sister getters, default-empty virtuals) are
intentionally left open — they're correctness-by-inspection items
and folding them in would dilute the test suite without catching
real regressions. They are still listed below for completeness.

## CommandBuffer.hpp
- ✓ `Bundle` + `bundle(...)` factory — covered by `bundle_test.cpp`, `archetype_hash_determinism_test.cpp`
- ✗ `Bundle::with<T>` (batch-22 builder) — **UNCOVERED. Load-bearing.** New shipping API that callers are expected to chain instead of touching `initialMask` directly. Recommendation: add a `bundle_with_test.cpp` that exercises chained `.with(Transform{...}).with(Velocity{...})` and asserts both the value and the `initialMask` bit are set.
- ✓ `CommandBuffer::spawn` (all overloads) — `command_buffer_test.cpp`, `integration_kitchen_sink_test.cpp`
- ✓ `CommandBuffer::spawnBundle` (both overloads) — `bundle_test.cpp`
- ✓ `CommandBuffer::spawnBundleN` — `command_buffer_spawn_n_test.cpp`
- ✓ `CommandBuffer::destroy` — `command_buffer_test.cpp`
- ✓ `setTransform` / `setVelocity` / `setRenderTag` / `setUserData` / `setAcceleration` / `setParent` — `command_buffer_test.cpp`, `acceleration_test.cpp`
- ✓ §3.1 batch-5 setters (`setHealth`, `setFaction`, `setAnimationStateRef`, `setPhysicsBodyRef`, `setNavAgentRef`, `setBoundingVolume`) — `new_components_test.cpp`
- ✓ `setComponentMask` — `component_mask_test.cpp`
- ✓ `addTag` / `removeTag` — `tags_test.cpp`, `archetype_storage_stress_test.cpp`
- ✓ `addComponent<T>` / `removeComponent<T>` — `component_transition_test.cpp`, `archetype_signatures_test.cpp`
- ✓ `reserve` / `clear` / `size` / `empty` — `commit_hash_test.cpp` (reserve), recording paths exercise the rest
- ✗ `CommandBuffer::valueOnlyCount` — **UNCOVERED. Trivial.** Public accessor read internally by `commitBuffersSharded` (covered through the sharded-commit tests, which would fail if the counter were broken). No standalone test needed.
- ✓ `commands()` accessors — `component_mask_test.cpp`, `hierarchy_test.cpp` (engine-internal contract)

## Components.hpp
- ✓ POD structs (`Transform`, `Velocity`, `Acceleration`, `RenderTag`, `UserData`, `Parent`, `Health`, `Faction`, `AnimationStateRef`, `PhysicsBodyRef`, `NavAgentRef`, `BoundingVolume`, `Vec3`, `Quat`) — exercised across `acceleration_test.cpp`, `new_components_test.cpp`, `serialization_test.cpp`, etc.
- ✓ `Component` enum + `ComponentSet` (`all`, `none`, `bits`, `has`, `hasAll`, `intersects`, `add`, `remove`, `|`, `&`, `|=`, `==`) — `component_mask_test.cpp`, `wide_mask_test.cpp`
- ✓ `operator|(Component, Component)` — used pervasively in test register/reads/writes overrides

## Config.hpp
- ✓ `Config::workerCount`, `fixedStepSeconds`, `deterministic`, `sleepToPace`, `initialEntityCapacity`, `singleThreadedCommit`, `logCommitHashEvery`, `legacyCommitHash` — covered across `alpha_test.cpp`, `archetype_hash_determinism_test.cpp`, `commit_hash_test.cpp`, `sharded_commit_test.cpp`, `v1_2_legacy_commit_hash_test.cpp`
- ✗ `Config::maxStepsPerIteration` — **UNCOVERED. Trivial.** Only meaningful inside `run()`'s catch-up loop, and `run()` is itself covered by `render_integration_test.cpp` / `alpha_test.cpp`. No standalone need.

## Engine.hpp
- ✓ ctor / dtor / `initialize` / `step` / `run` / `shutdown` — `alpha_test.cpp` + most integration tests
- ✓ `requestQuit` — `alpha_test.cpp`
- ✗ `quitRequested` — **UNCOVERED. Trivial.** Sister to `requestQuit`; `run()` polls it internally. No new test required.
- ✓ `registerSystem` / `registerSystemAt` / `registeredSystemCount` — `register_system_at_test.cpp`
- ✓ `taskGraphSnapshot` — `task_graph_test.cpp`
- ✓ `setRenderer` (via every IRenderer-installing test) / `notifyResize` — `renderer_resize_test.cpp`
- ✓ `setLogger` / `logger()` — `logger_test.cpp`, `commit_hash_test.cpp`
- ✓ `world()` / `config()` / `tick()` / `simulationTime` — `pause_timescale_test.cpp`, `render_integration_test.cpp`, ubiquitous
- ✓ `stats` / `systemStats` / `frameSnapshot` / `jobSystemStats` — `frame_snapshot_test.cpp`, `register_system_at_test.cpp`, `chrome_trace_test.cpp`
- ✓ `resources()` — `resource_registry_test.cpp`, `resource_handle_test.cpp`
- ✓ `addResourceLoader` / `resourceLoaderCount` / `aggregateLoaderStats` / `markResourceStale` — `asset_reload_test.cpp`, `loader_shutdown_test.cpp`, `loader_stats_test.cpp`, `cancellation_test.cpp`
- ✓ `preloadUntil` — `preload_until_test.cpp`
- ✓ `workerCount` — `renderer_resize_test.cpp` (also `alpha_test.cpp`)
- ✓ `setTraceSink` — `telemetry_sink_test.cpp`, `async_snapshot_test.cpp`, `file_trace_sink_rotation_test.cpp`
- ✓ `setStallTimeout` — `concurrency_soak_test.cpp`, `concurrency_soak_long.cpp`
- ✗ `stallTimeout()` getter — **UNCOVERED. Trivial.** Sister to `setStallTimeout`.
- ✓ `snapshotAsync` — `async_snapshot_test.cpp`
- ✓ `reserveEntityHandle` / `reserveEntityHandles` — `lifecycle_hooks_test.cpp`, `reserve_handles_batch_test.cpp`
- ✓ `setTimeScale` / `paused`/ `setPaused` — `pause_timescale_test.cpp`
- ✗ `timeScale()` getter — **UNCOVERED. Trivial.** Sister to `setTimeScale`.
- ✓ `setTickBudget` / `setSkipPolicy` / `pushScriptedSkip` — `cancellation_test.cpp`
- ✗ `tickBudget()` / `skipPolicy()` getters — **UNCOVERED. Trivial.** Sister-of-setter pairs.
- ✗ `clearScriptedSkips` — **UNCOVERED. Load-bearing.** Called at session boundaries to release the queue. Recommendation: extend `cancellation_test.cpp` with a 3-line assertion that `pushScriptedSkip` + `clearScriptedSkips` results in no skips on the named tick.
- ✓ `events<Ev>()` template — pervasive across event tests
- ✓ `registerUserComponent` / `userComponent<T>` — `user_component_test.cpp`, `engine_user_component_lookup_test.cpp`
- ✓ `engineSerial` — `event_channel_cache_test.cpp`
- ✓ `TaskGraphNode` struct — `task_graph_test.cpp`

## EventChannel.hpp
- ✓ `EventChannel::emit` / `drainTick` — `event_callback_reemit_test.cpp`, `commit_soak_test.cpp`
- ✓ `subscribe` / `subscribeScoped` / `unsubscribe` — `event_subscribe_test.cpp`, `subscription_raii_test.cpp`
- ✓ `subscriberCount` / `pendingCount` — `event_subscribe_test.cpp`, `event_channel_lockfree_test.cpp`
- ✓ `Subscription` RAII handle (`reset`, `valid`, `id`, move semantics) — `subscription_raii_test.cpp`

## Game.hpp
- ✓ `IGame::onSetup` — `alpha_test.cpp` and every integration test
- ✗ `IGame::onTeardown` — **UNCOVERED. Trivial.** Default-empty virtual. Recommendation: add a 5-line test that registers an `IGame` whose `onTeardown` flips a bool, calls `engine.shutdown()`, and asserts the flag flipped. Not blocking.

## Handles.hpp
- ✓ `EntityHandle` (`valid`, `==`, `!=`, fields) — `command_buffer_test.cpp`, `foreach_chunk_test.cpp`
- ✓ `kInvalidEntity` — `component_transition_test.cpp`, `parent_autoderive_test.cpp`
- ✓ `std::hash<EntityHandle>` — used by SpatialHash (`spatial_hash_test.cpp`) and the `prevTransformMap_` path tested via `render_frame_interpolation_test.cpp`

## Logger.hpp
- ✓ `LogLevel` enum, `ILogger`, `DefaultLogger` — `logger_test.cpp`

## Query.hpp
- ✓ `forEach<...>` — `archetype_storage_stress_test.cpp`, `determinism_golden_test.cpp`
- ✓ `forEachWith<...>` — `component_mask_test.cpp`, `small_wins_test.cpp`
- ✓ `forEachWithCached<...>` + `MaskCache` (`rebuild` / `indices` / `required` / `size` / `clear`) — `mask_cache_test.cpp`
- ✗ `MaskCache::reserve` / `MaskCache::capacity` (batch-17 prewarm) — **UNCOVERED. Trivial.** Pure capacity hints; correctness is identical to a default-constructed cache. Could be folded into `mask_cache_test.cpp` in a 3-line follow-up but not blocking.
- ✓ `forEachChunk<...>` + `kForEachChunkSubJobThreshold` — `foreach_chunk_test.cpp`
- ✗ `forEachSerial<...>` — **UNCOVERED. Load-bearing.** Documented public API for the "too small to parallelize" path. Recommendation: add a `for_each_serial_test.cpp` (or a section in `query_test.cpp`) that exercises a 4-entity world and asserts the body sees every entity exactly once, on the sim thread.
- ✓ `required<...>()` factory — `mask_cache_test.cpp`

## RenderFrame.hpp
- ✓ `RenderFrame` struct (consumed in `submitFrame` callbacks) — `render_passes_test.cpp`, `render_integration_test.cpp`, `tags_test.cpp`
- ✓ `RenderFrame::prevTransforms` + `RenderInstancePrev` — `render_frame_interpolation_test.cpp`
- ✓ `RenderFrame::cameraIndexById` — `visibility_culling_32_cam_test.cpp`
- ✓ `RenderFrame::drawItems` per-pass — `render_pass_ordering_test.cpp`
- ✗ `RenderInstance` (the flat-instance POD) — **UNCOVERED as a named type.** Read indirectly through `frame.instances[i].entity` / `.transform` / `.meshId` in `render_passes_test.cpp`, `render_integration_test.cpp`, `tags_test.cpp`. Effectively covered; no new test needed.
- ✗ `kMaxCameras` constant — **UNCOVERED. Trivial.** The 32-camera cap is exercised by `visibility_culling_32_cam_test.cpp` (which references the limit semantically); the literal constant isn't grepped. Not blocking.

## Renderer.hpp
- ✓ `IRenderer::initialize` / `shutdown` / `submitFrame` — `alpha_test.cpp`, `debug_text_owning_test.cpp`, `render_frame_builder_test.cpp`
- ✓ `IRenderer::onResize` — `renderer_resize_test.cpp`

## Resource.hpp
- ✓ `ResourceId<T>` (`valid`, `==`, fields) — `resource_handle_test.cpp`, `asset_reload_test.cpp`
- ✓ `ResourceHandle<T>` lifecycle + `get` / `operator->` / `operator*` / `reset` / `id` / `valid` — `resource_handle_indirection_test.cpp`, `resource_handle_test.cpp`
- ✓ `ResourceRegistry::add` / `get` / `remove` / `count` — `resource_registry_test.cpp`, `resource_handle_test.cpp`
- ✓ `ResourceRegistry::addRefCounted` / `acquire` / `refCount` — `resource_handle_test.cpp`, `concurrency_soak_test.cpp`
- ✓ `LoaderStats` struct — `loader_stats_test.cpp`, `cancellation_test.cpp`
- ✓ `IResourceLoader::update` / `markStale` / `stats` / `cancel` / `onShutdown` — `asset_reload_test.cpp`, `loader_shutdown_test.cpp`, `loader_stats_test.cpp`, `cancellation_test.cpp`
- ✓ `AssetReloaded` + `matches<T>` — `asset_reload_test.cpp`

## ScratchArena.hpp
- ✓ ctor (default + sized), `allocate<T>`, `reset`, `bytesUsed`, `capacity` — `scratch_arena_test.cpp`

## Serialization.hpp
- ✓ per-component `serialize` / `deserialize` — `serialization_test.cpp`, `new_components_test.cpp`
- ✓ `WorldSnapshot` + bundled `serialize`/`deserialize` — `commit_soak_test.cpp`, `commit_hash_test.cpp`, `async_snapshot_test.cpp`
- ✗ `kWorldSnapshotMagic` / `kWorldSnapshotVersion` constants — **UNCOVERED. Trivial.** The version field is implicitly validated by every snapshot round-trip; no standalone test needed.

## SkipPolicy.hpp
- ✓ `SkipPolicy` enum (Budget + Scripted), `SystemSkipped` event — `cancellation_test.cpp`

## SpatialHash.hpp
- ✓ ctor / `clear` / `insert` / `size` / `cellCount` / `cellSize` / `forEachInRadius` / `forEachInBox` — `spatial_hash_test.cpp`

## Stats.hpp
- ✓ `EngineStats` (all common fields, `commitHash`) — `stats_test.cpp`, `commit_hash_test.cpp`, `archetype_hash_determinism_test.cpp`
- ✓ `JobSystemStats` (totals, `jobDurationHistogram`) — `job_histogram_test.cpp`, `small_wins_test.cpp`
- ✓ `SystemStats` (`waitSeconds`, `peakQueueDepth`, `buildRenderFrameSeconds`) — `system_wait_test.cpp`, `build_render_frame_seconds_test.cpp`
- ✓ `SystemStats::avgSubJobMicros` / `subJobsLastStep` (ADAPTIVE_TUNING.md T3) — `sub_job_telemetry_test.cpp`
- ✓ `kJobDurationHistogramBins` — `job_histogram_test.cpp`
- ✗ `EngineStats::engineBuildRenderFrameSeconds` / `renderSubmitSeconds` — **UNCOVERED. Load-bearing.** New 2026-05-20 split that pins down whether time is going to engine bookkeeping vs the renderer. Recommendation: add a 20-line test that registers a `submitFrame` sleeping for ~1 ms and asserts `renderSubmitSeconds > 0` while `engineBuildRenderFrameSeconds >= 0` and both are `<= lastStepSeconds`.

## System.hpp
- ✓ `JobPriority` + `parallelFor` priority overload — `cancellation_test.cpp`
- ✗ `kJobPriorityLevels` constant — **UNCOVERED. Trivial.** Internal-sizing constant.
- ✓ `Range` — `alpha_test.cpp`, `integration_kitchen_sink_test.cpp` and most parallelFor bodies
- ✓ `SystemContext` (`world`, `dt`, `tick`, `parallelFor`, `single`, `reserveHandle`, `reserveHandles`, `shouldYield`, `worldView`) — broadly covered; `worldView` in `world_view_test.cpp`, `shouldYield` in `cancellation_test.cpp`
- ✗ `SystemContext::workerCount()` (batch-28 virtual) — **UNCOVERED directly. Trivial.** Used internally by `forEachChunk` (`foreach_chunk_test.cpp` exercises the wired-up path). Add a 3-line check in `foreach_chunk_test.cpp` if you want explicit coverage.
- ✓ `ISystem` (`name`, `onRegister`, `update`, `preStep`, `postStep`, `buildRenderFrame`, `reads`, `writes`, `dependencies`, `provides`, `preferredGrain`, `skippable`) — covered across `lifecycle_hooks_test.cpp`, `task_graph_test.cpp`, `cancellation_test.cpp`, `build_render_frame_timing_test.cpp`
- ✗ `ISystem::onUnregister` — **UNCOVERED. Trivial-ish.** Default no-op virtual; engine calls it during shutdown. Recommendation: a 10-line test that registers a system whose `onUnregister` flips a flag, calls `engine.shutdown`, asserts. Not blocking; engine teardown is otherwise well exercised.
- ✓ `HierarchyConfig` + `makeHierarchySystem` — `hierarchy_test.cpp`, `hierarchy_scale_test.cpp`, `hierarchy_cycle_test.cpp`

## TaskTag.hpp
- ✓ `TaskTag` constructor + `valid` + `operator==` — `task_graph_test.cpp`, `cancellation_test.cpp`

## Telemetry.hpp
- ✓ `ITraceSink` — `telemetry_sink_test.cpp`
- ✓ `FileTraceSink` (`onFrame`, `onShutdown`, `setAsync`, `isAsync`, `rotationIndex`, `bytesWrittenCurrent`) — `file_trace_sink_rotation_test.cpp`, `async_snapshot_test.cpp`, `telemetry_sink_test.cpp`
- ✓ `HudTraceSink` (`onFrame`, `tryGet`, `LatestTelemetry`) — `telemetry_sink_test.cpp`
- ✓ `BudgetExceeded` event + `FrameBudgetWatcher` (`postStep`, `exceedCount`) — `telemetry_sink_test.cpp`, `integration_kitchen_sink_test.cpp`
- ✗ `FrameBudgetWatcher::targetSeconds()` getter — **UNCOVERED. Trivial.** Sister to ctor argument.
- ✓ `EngineStall` event — `telemetry_sink_test.cpp`, `concurrency_soak_test.cpp`
- Note: `Telemetry.hpp` is NOT included from `threadmaxx.hpp`. Users must `#include <threadmaxx/Telemetry.hpp>` explicitly. Worth flagging for the docs / umbrella header.

## Trace.hpp
- ✓ `FrameSnapshot` — `telemetry_sink_test.cpp`, `frame_snapshot_test.cpp`
- ✓ `writeJsonLines` — `frame_snapshot_test.cpp`
- ✓ `ChromeTraceWriter` — `chrome_trace_test.cpp`, `async_snapshot_test.cpp`

## UserComponent.hpp
- ✓ `UserComponentId` (`valid`, `componentBit`) — `user_component_test.cpp`, `engine_user_component_lookup_test.cpp`
- ✓ `addUserComponent<T>` / `removeUserComponent` — `user_component_test.cpp`
- ✓ `user::has` / `user::tryGet<T>` / `user::chunkSpan<T>` — `user_component_test.cpp`

## version.hpp
- ✓ `version_string` + `THREADMAXX_VERSION` macros — `version_test.cpp`

## World.hpp
- ✓ tryGet for Transform/Velocity/RenderTag/UserData/Acceleration/Parent/Health/Faction/ComponentMask — `acceleration_test.cpp`, `lifecycle_hooks_test.cpp`, `component_mask_test.cpp`, `component_transition_test.cpp`, `bundle_test.cpp`
- ✗ `tryGetAnimationStateRef` / `tryGetPhysicsBodyRef` / `tryGetNavAgentRef` / `tryGetBoundingVolume` — **UNCOVERED. Load-bearing.** Sister accessors for the §3.1 batch-5 components. The dense arrays are exercised through `world.animationStates()` / etc. in `new_components_test.cpp`, but the per-handle `tryGet` path isn't. Recommendation: extend `new_components_test.cpp` with a four-liner asserting each per-handle accessor returns the expected value after a `setX` commit and `nullptr` for an entity that lacks the bit.
- ✓ `World::alive` — pervasive (e.g. `reserved_handle_test.cpp`, `entity_storage_test.cpp`)
- ✓ `World::has<T>` / `hasTag` / `get<T>` — `world_has_get_test.cpp`, `tags_test.cpp`
- ✓ Dense iteration spans (`entities` / `transforms` / `velocities` / etc.) — pervasive (`archetype_storage_stress_test.cpp`, `determinism_golden_test.cpp`); batch-5 dense vectors covered by `new_components_test.cpp`
- ✓ `World::size`, `World::snapshot` — `commit_hash_test.cpp`, `new_components_test.cpp`
- ✓ `archetypeSignatures` / `ArchetypeSignature` — `archetype_signatures_test.cpp`
- ✓ `archetypeChunkCount` / `archetypeChunk` — `world_for_each_chunk_of_test.cpp`, `world_view_test.cpp`
- ✓ `forEachChunkOf` (batch-24 F13) — `world_for_each_chunk_of_test.cpp`
- ✓ `locate` (`ArchetypeLocation`) — `user_component_test.cpp`, `sharded_commit_test.cpp`
- ✓ `WorldView` (`rebuild`, `chunks`, `chunkCount`, `entityCount`, `world`) — `world_view_test.cpp`
- ✗ `ArchetypeLocation::valid()` — **UNCOVERED. Trivial.** Tests check `archetype != UINT32_MAX` instead. Functionally exercised, just not by name.

## render/Camera.hpp
- ✓ `Camera` POD + `ProjectionMode` — `render_frame_builder_test.cpp`, `visibility_culling_test.cpp`, `visibility_culling_32_cam_test.cpp`
- ✗ `Viewport` POD (D2/§3.11.2) — **UNCOVERED. Load-bearing.** New multi-camera viewport field. Recommendation: add a small test that pushes two cameras with non-default viewport rects via `RenderFrameBuilder::addCamera`, captures the published frame in a renderer, and asserts the viewport fields round-trip unchanged. Even if the renderer doesn't consume it yet, pinning the round-trip prevents future regressions.

## render/DebugGeometry.hpp
- ✓ `DebugLine` / `DebugPoint` / `DebugText` — `render_passes_test.cpp`, `debug_text_owning_test.cpp`

## render/DrawItem.hpp
- ✓ `DrawItem` + `MaterialOverride` — `visibility_culling_test.cpp`, `instance_buffer_layout_test.cpp`, `render_passes_test.cpp`
- ✗ `MaterialOverride` as a named type — **UNCOVERED. Trivial.** Fields exercised through `DrawItem::materialOverride` in `instance_buffer_layout_test.cpp`.
- ✗ `MeshSkinnedRef` — **UNCOVERED. Load-bearing.** Public POD used by skinned-mesh renderers. Recommendation: add a 10-line test that constructs `MeshSkinnedRef{mesh, skel}` and asserts field round-trip; or, more usefully, exercise it through a `DrawItem` with a skinned attachment in the existing `instance_buffer_layout_test.cpp`.
- ✗ `AnimationPoseRef` — **UNCOVERED as a named symbol. Trivial.** `DrawItem::pose.ringSlot` is exercised by `instance_buffer_layout_test.cpp` (line 28-ish).
- ✗ Phantom tags `Skeleton`, `PoseBuffer` — Trivial. They're brands on `ResourceId<>`, never instantiated.

## render/InstanceBufferLayout.hpp
- ✓ `InstanceLayoutEntry` (size, alignment, field layout), `packInstance`, `packInstances` — `instance_buffer_layout_test.cpp`

## render/Light.hpp
- ✓ `Light` POD — `render_frame_builder_test.cpp`, `render_integration_test.cpp`
- ✗ `LightType` enum — **UNCOVERED as a named symbol.** Trivial; values are set via `Light::type = LightType::Directional` (default) in tests but not explicitly switched to Point/Spot. Recommendation: extend `render_frame_builder_test.cpp` with one Light per type and assert round-trip.

## render/RenderFrameBuilder.hpp
- ✓ ctor / `addCamera` / `addLight` / `addDrawItem` / `addDebugLine` / `addDebugPoint` / `addDebugText` (both overloads) / `reset` / `cameras` / `lights` / `drawItems` / `debugLines` / `debugPoints` / `debugText` — `render_frame_builder_test.cpp`, `debug_text_owning_test.cpp`, `render_passes_test.cpp`
- ✗ `finalizeDebugTextViews` — **UNCOVERED directly.** Trivial — the owning-string overload's view-fixup is implicitly tested by `debug_text_owning_test.cpp` (the engine calls `finalize` after each `buildRenderFrame`). No standalone test needed.

## render/RenderPasses.hpp
- ✓ `RenderPass` enum + `kRenderPassCount` + `passIndex` — `render_pass_ordering_test.cpp`, `render_passes_test.cpp`, `render_frame_builder_test.cpp`

## render/UploadRing.hpp
- ✓ ctor / `nextFrame` / `allocate` / `pushBytes` / `head` / `bytesPerFrame` / `frameCount` / `currentFrame` / `setGrowOnOverflow` — `upload_ring_test.cpp`

## render/Visibility.hpp
- ✓ `Frustum`, `extractFrustum`, both `intersectsFrustum` overloads, `cullByFrustum` — `visibility_culling_test.cpp`, `visibility_culling_32_cam_test.cpp`

## threadmaxx.hpp
- Umbrella header. Covered by every test that `#include`s it (e.g. `alpha_test.cpp`).
- Note: `Telemetry.hpp` is intentionally NOT in the umbrella; users must include it explicitly. Doc clarification candidate, not a code gap.

---

## Summary

| Header | Symbols | Covered | Uncovered (load-bearing) | Uncovered (trivial) |
| --- | ---: | ---: | ---: | ---: |
| CommandBuffer.hpp | 27 | 25 | 1 (`Bundle::with`) | 1 (`valueOnlyCount`) |
| Components.hpp | 14 | 14 | 0 | 0 |
| Config.hpp | 9 | 8 | 0 | 1 (`maxStepsPerIteration`) |
| Engine.hpp | ~38 | 33 | 1 (`clearScriptedSkips`) | 4 (getters: `quitRequested`, `stallTimeout`, `timeScale`, `tickBudget`/`skipPolicy`) |
| EventChannel.hpp | 9 | 9 | 0 | 0 |
| Game.hpp | 2 | 1 | 0 | 1 (`onTeardown`) |
| Handles.hpp | 3 | 3 | 0 | 0 |
| Logger.hpp | 3 | 3 | 0 | 0 |
| Query.hpp | 8 | 6 | 1 (`forEachSerial`) | 1 (`MaskCache::reserve/capacity`) |
| RenderFrame.hpp | 5 | 3 | 0 | 2 (`RenderInstance` named, `kMaxCameras` literal) |
| Renderer.hpp | 4 | 4 | 0 | 0 |
| Resource.hpp | ~16 | 16 | 0 | 0 |
| ScratchArena.hpp | 5 | 5 | 0 | 0 |
| Serialization.hpp | ~15 | 13 | 0 | 2 (`kWorldSnapshotMagic`, `kWorldSnapshotVersion`) |
| SkipPolicy.hpp | 2 | 2 | 0 | 0 |
| SpatialHash.hpp | 8 | 8 | 0 | 0 |
| Stats.hpp | 6 | 5 | 1 (new `engineBuildRenderFrameSeconds`/`renderSubmitSeconds`) | 0 |
| System.hpp | ~14 | 12 | 0 | 2 (`onUnregister`, `kJobPriorityLevels`, `ctx.workerCount` direct) |
| TaskTag.hpp | 1 | 1 | 0 | 0 |
| Telemetry.hpp | 8 | 7 | 0 | 1 (`targetSeconds` getter) |
| Trace.hpp | 3 | 3 | 0 | 0 |
| UserComponent.hpp | 5 | 5 | 0 | 0 |
| version.hpp | 2 | 2 | 0 | 0 |
| World.hpp | ~20 | 16 | 1 (4 missing batch-5 `tryGetX`) | 1 (`ArchetypeLocation::valid`) |
| render/Camera.hpp | 2 | 1 | 1 (`Viewport`) | 0 |
| render/DebugGeometry.hpp | 3 | 3 | 0 | 0 |
| render/DrawItem.hpp | 5 | 1 (DrawItem) | 1 (`MeshSkinnedRef`) | 3 (`MaterialOverride`/`AnimationPoseRef`/phantoms) |
| render/InstanceBufferLayout.hpp | 3 | 3 | 0 | 0 |
| render/Light.hpp | 2 | 1 | 0 | 1 (`LightType` switched) |
| render/RenderFrameBuilder.hpp | 14 | 13 | 0 | 1 (`finalizeDebugTextViews` direct) |
| render/RenderPasses.hpp | 3 | 3 | 0 | 0 |
| render/UploadRing.hpp | 9 | 9 | 0 | 0 |
| render/Visibility.hpp | 5 | 5 | 0 | 0 |

**Load-bearing gaps requiring new tests (6):**
1. `Bundle::with<T>` — batch-22 builder method; never exercised.
2. `Engine::clearScriptedSkips` — needed for session boundaries.
3. `forEachSerial<...>` — documented public iteration path; not exercised.
4. `EngineStats::engineBuildRenderFrameSeconds` / `renderSubmitSeconds` — new 2026-05-20 instrumentation split.
5. `World::tryGetAnimationStateRef` / `tryGetPhysicsBodyRef` / `tryGetNavAgentRef` / `tryGetBoundingVolume` — sister accessors for batch-5 components.
6. `Viewport` (render/Camera.hpp) — batch-D2 multi-camera viewport field; round-trip never asserted.

**Trivial gaps (sister getters, default-empty virtuals, named-vs-fielded
exposure) total ~18 and don't block B32.** Most could be folded into existing
tests in a 3- to 10-line extension each, but they're correctness-by-inspection
items, not behavior regressions waiting to happen.

**Telemetry.hpp doc note:** not included in the `threadmaxx.hpp` umbrella;
worth either adding it or documenting the intentional omission.
