# threadmaxx — Architecture

`threadmaxx` is a renderer-agnostic C++20 game backend. Game code links the
library, registers `ISystem`s (gameplay, physics, AI, render-prep, …) and an
optional `IRenderer`, and the engine drives a fixed-step simulation loop on
a worker pool of its own. The library compiles clean under
`-Wall -Wextra -Wpedantic -Wshadow -Wsign-conversion -Wconversion
-Wold-style-cast` with `-DTHREADMAXX_WARNINGS_AS_ERRORS=ON`, ASAN/UBSAN/TSAN.

This document explains how every load-bearing subsystem fits together. For
the daily contributor playbook (recipes, gotchas, "what to grep when") see
`CLAUDE.md`. For the deep history of every shipped batch, see
`CLAUDE_ARCHIVE.md`. For the user-guide narrative see `doc/index.md`.

Throughout this document, "v1.3 default" and "v1.2 fallback" refer to the
`Config::legacyCommitHash` knob; today's published version is `v1.2.1`
(see `include/threadmaxx/version.hpp`).

---

## 1. Design goals

| Goal | How the engine delivers it |
|---|---|
| **One authoritative world; gameplay never races itself.** | Worker jobs receive a `const World&` and write into private `CommandBuffer`s. All mutations go through `EngineImpl::commitBuffer` / `commitBuffersSharded` on the sim thread or — for value-only commands — on worker threads that target disjoint chunks. |
| **Parallel by default.** | Per-system `parallelFor` slices entity ranges across the engine's work-stealing `JobSystem`. Sibling systems whose declared `reads()`/`writes()` don't conflict share a wave and run concurrently. |
| **Renderer-agnostic.** | The renderer sees a POD `RenderFrame` (flat instances + hierarchical fields). The default content is engine-built; richer content (cameras, lights, debug geometry) comes from systems via the `ISystem::buildRenderFrame` hook. |
| **Small, stable public surface.** | Everything user-visible lives in `include/threadmaxx/`. All implementation hides behind PImpl in `src/`. `World::impl_()` / `EngineImpl` are engine-internal access points. |
| **Determinism is a knob.** | Same command stream → same final per-archetype state, byte-for-byte, across runs and machines. The `EngineStats::commitHash` per-tick fingerprint is the runtime safety net. |
| **No required dependencies.** | The library ships a tiny no-dependency test harness (`tests/Check.hpp`). Vulkan and SDL2 are opt-in for the renderer examples; the core engine compiles standalone. |

---

## 2. Tick lifecycle

The canonical fixed step is `EngineImpl::step()`
(`src/EngineImpl.cpp:2455`). The ordered phases are:

```
step()
 ├─ early-return paths
 │   ├─ if !initialized_                       → no-op
 │   └─ if paused_                             → zero per-tick stats, return
 │
 ├─ apply pending adaptive-tuning patch        (T4)
 │   • overwrites systemPreferredGrain_ entries for any
 │     ITuningPolicy::propose() patch staged at the end of the previous tick
 │
 ├─ publish watchdog start (atomics)           (§3.7 batch 14)
 │
 ├─ reset per-step accumulators
 │   • commitBreakdown_, commitHashAcc_, per-system *LastStep counters
 │
 ├─ preStep waves
 │   • worldView_.rebuild(world_)
 │   • for each registered system in registration order:
 │       construct SystemContextImpl
 │       call system->preStep(ctx) on the sim thread
 │       commitBuffer() each emitted CommandBuffer (serial)
 │       worldView_.rebuild(world_)            // so next preStep sees commits
 │
 ├─ reset overBudget_ = false
 │
 ├─ for each wave in waves_:                   ← the parallel-update phase
 │     worldView_.rebuild(world_)              // chunk inventory may have moved
 │     clear cumulative shardMigratingBitmap_  // S8
 │     decide skip for each system in the wave (skippable + policy)
 │     dispatch updates:
 │       • |wave| == 1  → runIndex(0) on sim thread
 │       • |wave|  > 1  → JobLatch over (wave.size - 1) submitted
 │                         to JobSystem; the tail runs inline on sim;
 │                         the latch's spin-then-block scheme caps wake cost
 │     emit SystemSkipped events for skipped slots
 │     for each system in the wave:
 │       commit its CommandBuffers
 │         • cfg_.singleThreadedCommit       → commitBuffer (per buffer)
 │         • !cfg_.singleThreadedCommit      → commitBuffersSharded(...)
 │       update SystemStats (lastUpdateSeconds, jobsSubmitted, commands,
 │                            waitSeconds, peakQueueDepth, avgSubJobMicros)
 │     post-wave budget check
 │       • SkipPolicy::Budget + elapsed > budget → set overBudget_ true
 │         (next wave's skippable systems get dropped)
 │
 ├─ postStep waves                              ← serial again
 │     • same structure as preStep — each hook runs in registration order,
 │       commits flush immediately, worldView_ refreshed between calls
 │
 ├─ resource loaders                            (§3.3)
 │     • for each registered IResourceLoader in registration order:
 │         loader->cancel(*publicEngine_)        // drop newly-stale work
 │         loader->update(*publicEngine_)        // pump pending I/O
 │
 ├─ reservation reap
 │     • discard any reserveHandle() not consumed by a cb.spawn(handle, …)
 │
 ├─ event drain
 │     • every EventChannel<T> swaps front/back; emits from this tick
 │       become visible on the next
 │
 ├─ buildRenderFrame hook                       (§3.2)
 │     • for each system in registration order:
 │         reset systemRenderBuilders_[i]
 │         system->buildRenderFrame(builder)
 │         finalize debug-text views
 │         record buildRenderFrameSeconds in SystemStats
 │
 ├─ tick_++; simulationTime_ += fixedStepSeconds
 │
 ├─ buildRenderFrame()                          (engine-side merge)
 │     • walk chunks; populate instances + prevTransforms (the auto lane)
 │     • merge each system's RenderFrameBuilder into the hierarchical fields
 │     • atomic publish: frontIndex_.store(back, release)
 │
 ├─ renderer->submitFrame(renderFrames_[front])  // if a renderer is installed
 │
 ├─ publish EngineStats
 │     • lastStepSeconds, avgStepSeconds (EWMA α = 1/16), commitDurationSeconds
 │     • commitHash:
 │         • legacyCommitHash → commitHashAcc_ (the v1.x byte-per-command mix)
 │         • !legacyCommitHash → finalizeCommitHash() (per-archetype rollup)
 │     • engineBuildRenderFrameSeconds / renderSubmitSeconds
 │
 ├─ ITraceSink::onFrame(snapshot)                // if a sink is installed
 │
 ├─ adaptive tuning observe+propose              // T4 / T6, depending on mode
 │     • TuningMode::Active  → policy.observe(); patch ← policy.propose();
 │                              record into TuningTrace if attached
 │     • TuningMode::Scripted → patch ← trace.tryGet(currentTick)
 │     • staged patch is applied at the TOP of the next step()
 │
 └─ clear watchdog "stall emitted" latch
```

`Engine::run()` is a thin wrapper that calls `step()` in a wall-clock-paced
loop. `Config::maxStepsPerIteration` caps how many catch-up steps fire per
outer iteration (default 8) — without it, a slow frame would compound. Between
ticks `run()` calls `submitInterpolatedFrame(alpha)` so interpolation-capable
renderers can lerp transforms by `alpha = elapsed / fixedStepSeconds`. There
is no rebuild on the interp path: world state is unchanged between ticks, so
only `RenderFrame::alpha` mutates on the front frame and `submitFrame` is
re-invoked.

---

## 3. Threading model

```
                          main / simulation thread
                          ─────────────────────────
   step()
   │
   │  preStep    → serial, registration order, sim thread
   │  for each wave:
   │      systems in the wave dispatched via JobSystem (size-1 jobs)
   │      tail system runs inline on sim thread
   │      each system's update calls into parallelFor / single
   │           │
   │           ▼
   │     ┌────────┐  ┌────────┐  ┌────────┐  …  ┌────────┐
   │     │ worker │  │ worker │  │ worker │     │ worker │   per-worker:
   │     │  deque │  │  deque │  │  deque │     │  deque │     - 3 priority
   │     │  + mtx │  │  + mtx │  │  + mtx │     │  + mtx │       deques
   │     │  + cv  │  │  + cv  │  │  + cv  │     │  + cv  │     - cv park
   │     └───┬────┘  └───┬────┘  └───┬────┘     └───┬────┘
   │         │ pop own (FIFO front, priority-ordered)
   │         │ steal sibling (LIFO back, priority-ordered, try_lock)
   │         ▼
   │     each job:  fn(Range, CommandBuffer&[, ScratchArena&])
   │
   │  commit phase (after the wave's JobLatch fires):
   │      cfg_.singleThreadedCommit = true  → serial commitBuffer on sim
   │      cfg_.singleThreadedCommit = false → sharded:
   │          Pass A (sim)     migrating-entity bitmap
   │          Pass B (sim)     classify; apply global lane; bin value-only
   │          Pass C (workers + sim) parallel-apply per-chunk bins
   │                                  (sim runs the largest bin inline; S9)
   │
   │  postStep → serial, registration order, sim thread
   │  resource loaders pump (sim)
   │  reservation reap (sim)
   │  event drain (sim)
   │  buildRenderFrame hooks → serial, sim
   │  engine-side render frame build + atomic publish
   │  renderer->submitFrame(front)
   │
   └──────────────────────────────────────────────
   stall-watchdog thread        (only when setStallTimeout > 0)
   snapshot-writer thread       (lazily spawned by snapshotAsync)
   per-loader internal threads  (each IResourceLoader owns its own)
```

### Load-bearing invariants

1. **Worker jobs never mutate live world state.** They read via `const
   World&` / `WorldView`, write into per-job `CommandBuffer`. The single
   explicit exception is `EntityStorage::reserveHandle()`, guarded by its
   own `reservationMtx_`; it only allocates a slot record (and the matching
   `reserveHandles(count, span)` batch form takes the mutex once for N
   handles). Dense chunk storage is untouched during a wave.
2. **Commit order is submission order, never execution order.** Workers
   race freely; the per-system `std::vector<CommandBuffer>` in
   `SystemContextImpl::buffers_` is the authoritative submission record.
   `parallelFor` resizes that vector up front (so pointers stay valid while
   jobs run) and slot `k` is bound to the k-th `parallelFor` chunk.
3. **Per-archetype hash determinism (v1.3).** `Config::legacyCommitHash =
   false` (default) ships the per-archetype state-fingerprint rollup;
   `true` preserves the v1.x byte-per-command FNV-1a-64 mix.
4. **The public surface is the contract.** `include/threadmaxx/` is the
   only header set game code is expected to include. `include/threadmaxx/
   internal/Archetype.hpp` is technically in the include tree but exposes
   internal storage — game code touches it only through the chunk-iteration
   helpers in `Query.hpp` and `UserComponent.hpp`.
5. **Async I/O lives on engine-owned threads, not workers.** The stall
   watchdog has its own thread; `snapshotAsync` lazily spawns a writer
   thread joined at shutdown; resource loaders own their own pools.
   Workers are reserved for sim-step work.

---

## 4. Public surface

The headers under `include/threadmaxx/` are the contract:

```
threadmaxx/
├── threadmaxx.hpp           ← umbrella include
├── version.hpp              ← THREADMAXX_VERSION + version_string()
├── Engine.hpp               ← top-level Engine + TaskGraphNode
├── World.hpp                ← read-only view + WorldView + ArchetypeLocation
├── Handles.hpp              ← EntityHandle, kInvalidEntity
├── Components.hpp           ← Component / ComponentSet + the built-in PODs
├── CommandBuffer.hpp        ← per-job mutation recorder + Bundle / bundle()
├── System.hpp               ← ISystem + SystemContext + makeHierarchySystem
├── Game.hpp                 ← IGame (one-shot setup hook)
├── Query.hpp                ← forEach / forEachWith / forEachChunk / MaskCache
├── Renderer.hpp             ← IRenderer
├── RenderFrame.hpp          ← RenderFrame + RenderInstance + RenderInstancePrev
├── render/                  ← renderer-neutral PODs (Camera, Light, DrawItem,
│                              DebugGeometry, RenderPasses, RenderFrameBuilder,
│                              InstanceBufferLayout, UploadRing, Visibility)
├── EventChannel.hpp         ← typed double-buffered queues
├── Resource.hpp             ← ResourceId<T>, ResourceHandle<T>, ResourceRegistry
├── ScratchArena.hpp         ← per-job bump allocator
├── SpatialHash.hpp          ← header-only uniform-grid index
├── Serialization.hpp        ← WorldSnapshot binary I/O
├── Stats.hpp                ← EngineStats / SystemStats / JobSystemStats
├── CommitBreakdown.hpp      ← Pass A/B/C wall-clock + counters
├── SkipPolicy.hpp           ← SkipPolicy enum + SystemSkipped event
├── TaskTag.hpp              ← FNV-1a-64 constexpr tag for tag-edge scheduling
├── Telemetry.hpp            ← ITraceSink + FileTraceSink + HudTraceSink + watchdog
├── Trace.hpp                ← FrameSnapshot + writeJsonLines + ChromeTraceWriter
├── Tuning.hpp               ← ITuningPolicy + TuningPatch + TuningMode + TuningTrace
├── AdaptiveGrainPolicy.hpp  ← built-in policy targeting per-sub-job µs
├── Config.hpp               ← knobs (workerCount, fixedStepSeconds, etc.)
├── Logger.hpp               ← ILogger + DefaultLogger
├── UserComponent.hpp        ← game-side per-entity columns
└── internal/Archetype.hpp   ← ArchetypeChunk + ArchetypeTable (used by Query.hpp + UserComponent.hpp)
```

Private implementation in `src/` may change freely; `EngineImpl`,
`WorldImpl`, `EntityStorage`, `JobSystem`, the user-component registry, and
all telemetry plumbing are PImpl'd. `World::impl_()` and `EngineImpl` are
reachable for engine-internal code only.

---

## 5. Entity storage — chunked archetypes

### 5.1 Handles and slots

`EntityHandle` (`Handles.hpp`) is a 64-bit pair `(index, generation)`.
Generation 0 is the canonical invalid sentinel. `EntityStorage::Slot`
(`src/EntityStorage.hpp`) is the per-slot record:

```cpp
struct Slot {
    std::uint32_t archetypeIndex = UINT32_MAX;
    std::uint32_t row            = UINT32_MAX;
    std::uint32_t generation     = 0;
    bool          alive          = false;
    bool          reserved       = false;
};
```

Slots are owned by a `std::vector<Slot> slots_`; recycled indices come from
`std::vector<std::uint32_t> freeSlots_`. `slotCount()` is the
ever-allocated upper bound — useful for sizing the sharded commit's
migrating-entity bitmap. Every `destroy()` bumps `generation` so stale
handles never alias new entities.

### 5.2 ArchetypeChunk and ArchetypeTable

Entities live in `ArchetypeChunk` instances keyed by their per-entity
`ComponentSet`. One chunk per unique mask; chunks own dense parallel arrays
(one `std::vector<T>` per built-in component plus a
`std::vector<UserComponentColumn>` for game-side bits). The chunk also
carries `denseToSlot[r]` (row → owning slot index) and `entities[r]` (the
parallel `EntityHandle` array — index-into-`slots_` + generation).

```cpp
struct ArchetypeChunk {
    ComponentSet                   mask;
    std::vector<std::uint32_t>     denseToSlot;
    std::vector<EntityHandle>      entities;
    std::vector<Transform>         transforms;
    std::vector<Velocity>          velocities;
    /* … one vector per built-in dense component … */
    std::vector<UserComponentColumn> userColumns;
    mutable std::uint64_t                cachedHash = 0xcbf29ce484222325ull;
    mutable std::atomic<bool>            hashDirty{true};
};
```

`ArchetypeTable` indexes chunks by `mask.bits()`; `chunks()` returns them
in **creation order** (stable across the entire engine lifetime — only the
introduction of a brand-new mask appends a chunk). Within a chunk, entity
order is **spawn order**; `destroy()` is a swap-and-pop so the
last-spawned entity slides into the hole. Two non-obvious consequences:

- Code that needs entity-ordinal stability across mask changes must look
  up by `EntityHandle`, not by dense index.
- `World::transforms()` (the stitched legacy view) iterates in
  **archetype-creation order × spawn-order within archetype**, not global
  spawn order.

`ArchetypeChunk` defines custom copy/move because `hashDirty` is
`std::atomic<bool>` (default-non-copyable/movable). Both transfers
load-then-store the atomic with relaxed ordering — safe because chunk
vector growth happens single-threaded on the sim thread outside any
commit window.

### 5.3 The stitched cache

`World::transforms()` and its siblings return spans backed by a
**lazily-rebuilt stitched cache**. `EntityStorage::ensureStitched()`
(`src/EntityStorage.cpp`) walks chunks in creation order and concatenates
every component vector — for entities whose chunk doesn't carry a
component, the parallel slot is the default-constructed value. The cache
is invalidated by any mutation via `stitchedDirty_` (atomic, relaxed) and
rebuilt under `stitchedMtx_` with double-checked locking so the steady-
state hot path is lock-free.

The cache is safe to read from many concurrent workers within a wave
**when no writers are in flight** (the wave's commit hasn't started yet).
It is **NOT** safe during a sharded Pass C — workers there are mutating
the underlying chunk vectors while `stitchedDirty_` flips. Use
`forEachChunk` / `worldView()` instead; those bypass the stitched view
entirely.

### 5.4 Mask changes and migration

Mutations that toggle a component-presence bit flow through
`EntityStorage::setMaskAndMigrate(handle, newMask)`:

1. Look up source chunk and row.
2. `getOrCreateArchetype(newMask)` → destination chunk index.
3. `ArchetypeTable::migrate` does insert-into-destination + swap-and-pop
   from source, preserving every component value carried by both masks.
4. Returns the (dstArchetype, dstRow) and the slot index of the entity
   that swapped into the source row (so the caller can patch its slot).
5. Both chunks mark `hashDirty = true` (relaxed atomic store).

**Batch migration.** When a contiguous run of `CmdAddTag` / `CmdRemoveTag`
/ `CmdSetHealth` / `CmdSetFaction` / `CmdSetBoundingVolume` targets the
same `(srcArch, dstMask)` AND the run is ≥ `Config::batchMigrateThreshold`
(default 16), the commit path dispatches through
`setMaskAndMigrateBatch`. Underneath, `ArchetypeTable::migrateBatch`
inserts in submission order, then pops from source in **descending
srcRow order** — the descending order is the determinism-preserving
invariant proven in `tests/migration_batch_test.cpp`. The batch path
amortizes the destination-archetype lookup, the per-component vector
capacity grow, and the user-column carry buffer across the run.
`CommitBreakdown::batchedMigrations` counts the commands that took this
path. Set the threshold to `UINT32_MAX` to disable (the bench A/B uses
that).

### 5.5 Adding a new built-in component

This is the one operation that crosses every layer. See the canonical
checklist in `CLAUDE.md` under "Adding a new built-in component" — every
step is load-bearing. The most-often-forgotten bits:

- `Component::Foo` enum bit + extending `ComponentSet::all()`'s mask
  (64-bit space; 16 bits used through `DestroyedTag = 1ull << 15`).
- Parallel writes in `ArchetypeChunk::insert` / `removeSwapPop` /
  `migrate` (`src/Archetype.cpp`) and the matching `mut*()` setter on
  `EntityStorage`.
- New variant alternative in `detail::Command` (`CommandBuffer.hpp`)
  and arms in `applyCommandImpl` and `hashCommandImpl` (top of
  `src/EngineImpl.cpp`) — both commit paths go through these helpers,
  so a missing arm fails to compile rather than silently corrupt state.
- `EntityStorage::ensureStitched()` needs the new stitched-view vector +
  `fill()` call.
- The setter MUST set the touched chunk's `hashDirty.store(true,
  relaxed)`. Extend `hashChunkContent()` in `src/EngineImpl.cpp` with
  an `if (c.mask.has(...)) mixVecBytes(c.foos.data(), sizeof(Foo))` arm
  in component-enum order.

### 5.6 Tag-only categories

`StaticTag` (bit 13), `DisabledTag` (bit 14), `DestroyedTag` (bit 15)
have no dense storage and no `setFoo` command — only the presence bit.
Game code drives them via `cb.addTag(e, Component::StaticTag)` /
`cb.removeTag(...)` / `world.hasTag(...)`. Tag bits round-trip through
`WorldSnapshot`. `buildRenderFrame` automatically skips entities with
`DisabledTag`; the engine never auto-destroys on `DestroyedTag` — it is
purely a gameplay-side marker.

### 5.7 User components

`UserComponent.hpp` is the engine-blind escape hatch for game-side dense
arrays. Registration happens once via
`engine.registerUserComponent<MyType>()` — the engine assigns the next
free bit (≥ 16), captures `sizeof(T)`, and returns a `UserComponentId`.
Registration is idempotent on `typeid(T)` and bit assignment is
registration-order-stable within a process.

Storage lives in `ArchetypeChunk::userColumns`:

```cpp
struct UserComponentColumn {
    std::uint32_t          bit;
    std::uint32_t          stride;
    std::vector<std::byte> bytes;  // size = rowCount * stride
};
```

A fresh chunk consults the engine-owned `UserComponentRegistry` (wired
into `ArchetypeTable::setUserComponentRegistry` at engine `initialize`)
when materializing — unknown user bits are silently dropped.

Public API is free-function-shaped to avoid templating `World` /
`CommandBuffer` on user types:

- `addUserComponent<T>(cb, id, e, value)` / `removeUserComponent(cb, id, e)`
- `user::has(world, id, e)` / `user::tryGet<T>(world, id, e)`
- `user::chunkSpan<T>(chunk, id)` — typed read-only span over one chunk
- `World::locate(handle) → ArchetypeLocation` jumps straight to
  (archetype, row) for any handle.

Out of scope: serialization, cross-process bit stability, non-trivially-
copyable types — those are game-side responsibility. `T` must be
`std::is_trivially_copyable_v` (the engine memcpys).

---

## 6. Commands and the CommandBuffer

### 6.1 Variant layout

`detail::Command` (`CommandBuffer.hpp`) is a `std::variant` of every
mutation kind:

```
CmdSpawnPtr (unique_ptr wrapper, 8 B)
CmdDestroy
CmdSetTransform / CmdSetVelocity / CmdSetAcceleration / CmdSetUserData
CmdSetRenderTag
CmdSetParent
CmdSetHealth / CmdSetFaction
CmdSetAnimationState / CmdSetPhysicsBody / CmdSetNavAgent
CmdSetBoundingVolume
CmdSetComponentMask
CmdAddTag / CmdRemoveTag
CmdAddUserComponentPtr (unique_ptr wrapper, 8 B)
CmdRemoveUserComponent
```

`CmdSpawn` (248 B if inlined) and `CmdAddUserComponent` (112 B if inlined)
are wrapped in `std::unique_ptr` so the variant stays at 56 B (dominated
by `CmdSetTransform` at 48 B). Without this trick a 100k-command buffer
would consume ~25 MB with ~80% padding; with it the four high-frequency
value setters live in place and the rare oversize ones pay one allocation
each. The `unwrap` helper at the top of `applyCommandImpl` /
`hashCommandImpl` dereferences the `unique_ptr` transparently.

### 6.2 Recording

`CommandBuffer` is move-only and is exclusive to one worker for the
duration of one job. There are zero locks: the only contention point —
allocations inside the variant vector — is unique per buffer.

Recording paths:

- `spawn(...)` / `spawn(handle, ...)` — six overloads covering default-
  mask + explicit-mask + reserved-handle variants. Default-mask
  derivation auto-attaches `RenderTag` (if `meshId >= 0`) and `Parent`
  (if `parent.valid()`); the §3.1 batch-5 components (`Health`,
  `Faction`, `AnimationStateRef`, `PhysicsBodyRef`, `NavAgentRef`,
  `BoundingVolume`) are NOT attached by default.
- `spawnBundle(b)` / `spawnBundle(handle, b)` — feeds a `Bundle` with a
  caller-derived `initialMask` (see §6.4).
- `spawnBundleN(reservedSpan, bundlesSpan)` — bulk-spawn helper. Pairs
  reserved handles with bundles 1:1; pre-reserves command-buffer storage
  to amortize `emplace_back` churn.
- `setX(e, v)` for every built-in dense type.
- `setRenderTag` writes the value AND updates the bit (set iff
  `meshId >= 0`); `setParent` writes the value AND updates the bit (set
  iff `parent.valid()`); the §3.1 batch-5 setters attach their
  presence bit on commit.
- `setComponentMask(e, mask)` overwrites the entire mask. Use sparingly;
  for single-bit edits prefer `addTag` / `removeTag` (they compose when
  multiple workers each flip a different bit on the same entity within
  a tick).
- `addTag(e, Component::Foo)` / `removeTag(e, Component::Foo)` — single-
  bit flips. Race-free against other workers' tag flips on disjoint
  bits.
- `addComponent<T>(e, v)` template — ALWAYS attaches the bit, regardless
  of value (unlike `setRenderTag(RenderTag{-1})` which clears it). Tag-
  only categories `static_assert` out — use `addTag` for them.
- `removeComponent<T>(e)` template — clears the bit.
- `addUserComponent<T>(cb, id, e, v)` / `removeUserComponent(cb, id, e)`
  — game-side dense column attach/detach.

### 6.3 Record-time per-chunk routing (S8)

When `cfg_.recordTimeRouting == true` (default) AND sharded commit is
on, the engine installs a chunk-locator function pointer on every fresh
`CommandBuffer` it hands to a worker:

```cpp
using LocatorFn = std::uint32_t(*)(const void* ctx, EntityHandle) noexcept;
```

The locator closes over the engine's `EntityStorage`, returns the
entity's current archetype index, and `kInvalidArchetype` for stale
handles. Inside `CommandBuffer`, the four value-only recording methods
(`setTransform` / `setVelocity` / `setAcceleration` / `setUserData`)
call the locator, then push the command's index into either
`chunkBuckets_[arch]` or `globalIdx_`. Migrating commands and stale
handles always go to `globalIdx_`.

Pass A of the sharded commit consumes only `globalIdx_` (a small
fraction of total commands for typical workloads); Pass B walks
`chunkBuckets_[]` directly and demotes any entry whose target is now
listed in the cumulative wave-migrating bitmap. The bitmap is
**wave-cumulative**, cleared at wave start in `step()` (not per-commit),
so cross-system migrations within a wave correctly demote stale-binned
entries in later systems' commits.

Set `cfg.recordTimeRouting = false` (or env `THREADMAXX_NO_ROUTING=1`)
to revert to the pre-S8 full-cmd scan. Ignored when `singleThreadedCommit
= true` — the serial path pays zero S8 overhead.

### 6.4 Bundles

`Bundle` (in `CommandBuffer.hpp`) packages a parameter pack of component
values with a compile-time-derived initial mask:

```cpp
auto enemy = bundle(Transform{...}, Velocity{...}, Health{200, 200});
cb.spawnBundle(enemy);
```

`bundle(...)` is variadic; the resulting `initialMask` is the union of
the bits for each type. Composable via `Bundle{}.with(...)` builder
chains. The big difference from default-mask `spawn`: `bundle()` sets
ONLY the bits for components the caller listed — no implicit
Transform+Velocity+UserData+Acceleration freebies.

---

## 7. JobSystem — work-stealing worker pool

`src/JobSystem.{hpp,cpp}` is the engine's worker pool. The full
contract — including the load-bearing wake-contract comment block — lives
at the top of `JobSystem.hpp`. Summary:

### 7.1 Topology

```
workers_[0..N-1] : Worker
                 ├─ queues : array<deque<JobFn>, 3>     // High / Normal / Low
                 ├─ mtx    : mutex
                 ├─ cv     : condition_variable          // per-worker park CV
                 ├─ thread : std::thread
                 └─ stats  : ownPops / stolenJobs / histogram   (atomic relaxed)
```

`submit(fn, priority = Normal)` picks the target worker round-robin
(`pushCounter_.fetch_add(1) % N`), locks `target.mtx`, pushes onto
`target.queues[priority]`, notifies `target.cv`. Workers
`pop_front` (FIFO within a priority class) from their own queue; cross-
worker stealers `pop_back` (LIFO) under `try_lock` so an active producer
can't block them. Steal order respects priority too.

`waitIdle()` blocks on a separate `doneMtx_`/`doneCv_` pair so workers'
queue mutexes never serialize with sim-thread waits. The last worker to
decrement `outstanding_` from 1 → 0 locks `doneMtx_` and
`doneCv_.notify_all()`.

### 7.2 Wake contract (2026-05-25 missed-wakeup fix)

Pre-fix, the park predicate was `self.hasWork() || stopping_`. Any
`submit()` whose work landed on a sibling's queue left parked workers
parked — their `cv.wait` returned via spurious wakeup, the predicate
re-tested false, they re-parked. Stranded work inside a busy worker (in
a nested `JobLatch::wait`) hung ~1/5 runs of `rpg_demo --stress` on a
72-core host. The fix has three cooperating pieces:

1. **`wakeSeq_` (atomic uint64).** Every successful `submit()` bumps it
   (release, AFTER the queue push). Workers snapshot it BEFORE
   `parkedCount_.fetch_add` and entering `cv.wait`. The predicate
   becomes `self.hasWork() || stopping_ || wakeSeq_ != snapshot`. Any
   submit anywhere advances `wakeSeq_`, so a notified worker exits
   `cv.wait` and re-runs `popOwn + trySteal`.
2. **`parkedCount_` (atomic uint32).** Maintained around the park
   bracket. `submit()` reads it (acquire) to gate the helper notify
   below; when zero (all workers busy) no extra notify fires.
3. **Helper notify at `(target + 1) % N`.** When `parkedCount_ > 0`,
   `submit()` does one extra `notify_one` on the helper's CV in
   addition to `target.cv`. Over a wave of N submits the helper
   rotates through every worker, so any reachable parked worker is
   notified at least once even if their own queue gets no direct
   push.

Pinned by `tests/job_system_missed_wakeup_test.cpp` (200 nested-
parallelism iterations at every worker count from 4 to
`hardware_concurrency`). **Do not re-introduce a single global wake
futex** (semaphore or otherwise) — at 71 workers the contention costs
~50× the per-worker-CV design's sys time. **Do not drop the `wakeSeq_`
leg of the predicate** — workers will re-park on any non-direct-queue
wake and strand sibling work.

### 7.3 `JobLatch` — the TSAN-clean barrier

Lives in the anonymous namespace at the top of `src/EngineImpl.cpp`.
libstdc++'s `std::latch::wait()` lowers to a futex that ThreadSanitizer
cannot see through; `JobLatch` is the engine's hand-rolled replacement
using `std::ptrdiff_t` under mutex+CV. Every `count_down` takes the
lock. **Do not re-introduce `std::latch` here without a TSAN re-run;
do not re-introduce the lock-only-on-last-decrement form without 100×
stress validation under TSAN** (the "optimized" variant had a missed-
wakeup hazard).

**Spin-before-block (S11).** `Config::jobLatchSpinIters` (default 4096,
≈10-40 µs) gates a hot-loop on an atomic `done_` flag (release-stored
by the final `count_down`) before falling back to mutex+CV. When spin
observes `done_` within budget the kernel sleep + wakeup IPI of
`cv_.wait` is skipped, but the mutex is ALWAYS re-acquired before
returning so a stack-allocated latch's dtor cannot race the worker's
in-flight `cv_.notify_all` / `lock_guard` dtor. Empirically
`setTransform/MultiArch` commit_us −22%, `Churn` −9%; no regression on
no-wait workloads. Determinism unchanged.

### 7.4 Per-worker telemetry

`Worker::ownPops`, `stolenJobs`, `histogram` are all
`std::atomic<uint64_t>` with relaxed ordering. The owning thread writes
its `ownPops`/`histogram` slot; the stealing thread writes its OWN
`stolenJobs` slot (counts the steal). Reads from `stats()` happen from
any thread.

`JobSystemStats::jobDurationHistogram` is 16 log2-µs bins;
`JobSystem::binFor(ns)` is `static` for testability.
`JobSystem::outstanding()` reports live in-flight for queue-depth
sampling — `SystemContextImpl` uses it to populate
`SystemStats::peakQueueDepth` right after each `parallelFor` submit.

---

## 8. System scheduling — waves, task tags, grain

### 8.1 The wave scheduler

`EngineImpl::rebuildWaves()` runs on every `registerSystem` /
`registerSystemAt`. The algorithm is topological-sort-then-pack:

1. **Build edges.**
   - rw-conflict: for every ordered pair `a < b`, if
     `a.writes ∩ b.writes ≠ ∅`, OR `a.writes ∩ b.reads ≠ ∅`, OR
     `a.reads ∩ b.writes ≠ ∅`, add edge `a → b`.
   - tag overlap: for every pair, if `a.provides() ∩ b.dependencies()
     ≠ ∅` add edge `a → b`. Symmetric direction checked.
2. **Kahn's algorithm.** Pick zero-in-degree systems in registration
   order (this ties registration order into the topological sort, so
   the schedule is stable across runs). On stall (tag cycle), the
   lowest-index stuck system has its tag-only incoming edges dropped,
   the engine logs the cycle via `ILogger`, and Kahn resumes.
3. **Greedy wave-pack.** Earliest wave where every predecessor is done
   AND no rw-conflict with anyone already in the wave.

`Config::deterministic` is a declaration of intent — the engine is
deterministic on the commit path regardless; the flag is reserved for
future stricter contracts. `Engine::taskGraphSnapshot() →
std::vector<TaskGraphNode>` exposes the graph for HUD / Graphviz / test
assertions.

### 8.2 ISystem hooks

```
preStep        sim thread, registration order, before any wave
update         wave-scheduled (sibling-systems in same wave run concurrently)
postStep       sim thread, registration order, after every wave
buildRenderFrame sim thread, registration order, after postStep + event drain
onRegister     sim thread, once at registration
onUnregister   sim thread, once at shutdown (reverse registration order)
```

Defaults are `ComponentSet::all()` for `reads()` / `writes()` — every
pair conflicts, forcing strict registration-order serial. Overriding
them is the opt-in for parallel scheduling.

`skippable()` (default `false`) opts the system into the engine's skip
machinery. `preStep`, `postStep`, `buildRenderFrame` are NEVER skipped —
they're load-bearing for bookkeeping. Only `update()` is droppable.

`preferredGrain()` (default 0) is the per-system grain hint when
`parallelFor` is called with `grain = 0`. The default heuristic is
`(count + workers*4 - 1) / (workers*4)` — four chunks per worker.

`preferredWorkerCap()` (default 0 = uncapped) clamps the number of
sub-jobs `parallelFor` dispatches. Useful when a system's per-sub-job
work is small enough that `JobLatch + cv-wakeup` overhead dominates
beyond a known worker count (D12 found cube-render's sweet spot at 8
workers despite a 72-core host).

`dependencies()` / `provides()` (default empty) return spans of
`TaskTag`s — `TaskTag.hpp` is a constexpr FNV-1a-64 name+hash struct.
The engine never copies; static storage (e.g. a `constexpr` array
member) is the convention.

### 8.3 SystemContext and WorldView

`SystemContext` (`System.hpp`) is the engine-provided context for
`update()`. The implementation (`SystemContextImpl` in
`src/EngineImpl.hpp`) holds:

- `world_` / `view_` — read-only handles to the engine's `World` and
  the wave-scoped `WorldView`.
- `dt_` / `tick_` — `fixedStepSeconds * timeScale_` and the
  pre-increment tick.
- `buffers_` — `std::vector<CommandBuffer>`, one per `parallelFor`
  chunk (resized up front so pointers stay valid across job execution).
- `arenas_` — parallel `std::vector<ScratchArena>` (same length as
  `buffers_`).
- `subJobNanos_` — atomic ns accumulator; workers `fetch_add(relaxed)`
  after each sub-job, the sim thread reads it after the latch fires
  (ordering supplied by the latch's release/acquire).
- `waitSeconds_` / `peakQueueDepth_` — captured per `parallelFor` call.

`WorldView` (`World.hpp`) is a wave-scoped flat array of pointers to
the chunks in the world at wave start, plus a cached total entity
count. The engine rebuilds it before each wave (commits between waves
may shift chunk inventory); all systems in the wave see the same
pre-wave snapshot. Chunk pointers stay valid for the wave's duration —
capture by reference or by value into worker lambdas. `WorldView` is
the primary read interface for chunk-walking queries; reads from
`preStep` / `postStep` / `buildRenderFrame` use a freshly-built view
against current state but aren't the primary use case.

### 8.4 ScratchArena

`include/threadmaxx/ScratchArena.hpp` is a chained-slab bump allocator,
delivered via the three-arg `parallelFor` / `single` overloads (the
`JobFnArena` callback flavor). One arena per chunk, parallel to
`buffers_`; arenas survive across waves and are reset between calls so
steady-state reuses the slabs. `allocate<T>` `static_assert`s `T` is
trivially destructible (no per-allocation dtor bookkeeping).

---

## 9. Query helpers (`Query.hpp`)

Three layered iteration helpers, all walking `WorldView` chunks
internally:

```
forEach<C...>(ctx, fn, grain = 0)
    iterates every live entity, projects (EntityHandle, const C&..., CB&).
    Uses stitched-view spans; suitable when the per-entity check is the
    bottleneck and the world has one archetype.

forEachWith<C...>(ctx, fn, grain = 0)
    skips chunks whose mask lacks the required bits.
    Mask check is once per chunk; per-entity hot loop has no mask test.
    grain is forced to 1 chunk per job; sub-job fan-out comes from
    `parallelFor`'s default.

forEachChunk<C...>(ctx, fn)
    same chunk filter, but the callback fires with spans
    (EntityHandle, const C..., CB&) instead of per-entity refs.
    For chunks with > kForEachChunkSubJobThreshold (1024) rows,
    forEachChunk splits the chunk into sub-jobs covering contiguous row
    ranges so multiple workers share one huge chunk. Per-sub-job row
    budget: max(threshold, ceil(rowCount / (workers * 8))).
```

`forEachChunk` is the primary recommendation for hot paths — no stitched-
view rebuild, no per-entity mask test, contiguous per-archetype layout
for SIMD-friendly inner loops. Game code adding new query helpers or
extending existing ones should walk `ctx.worldView()`, dispatch via
`detail::getChunkSpan<C>`, and check the mask once per chunk.

### MaskCache + forEachWithCached

`MaskCache` (`Query.hpp`) is a user-owned cache of dense indices matching
a required `ComponentSet`. Rebuild discipline is on the caller: call
`cache.rebuild(world, required<T...>())` in `preStep` when the world's
mask shape might have changed since last tick. `forEachWithCached`
iterates `cache.indices()` directly, skips out-of-range entries (entity
destroyed post-rebuild), and does NOT re-test the mask on the hot loop
(the cache is the source of truth — index-in-range-but-mask-changed is
on the caller). `MaskCache::reserve(n)` pre-warms storage so the first
rebuild after that point is allocation-free.

---

## 10. The commit pipeline

The commit phase is invoked after every wave's update completes (and
also at the end of every `preStep` / `postStep` hook). Two paths:

### 10.1 Single-threaded path — `commitBuffer` (default)

`Config::singleThreadedCommit = true` (default). For each
`CommandBuffer` in submission order:

1. Scan the variant list for adjacent runs of mask-toggling commands
   targeting the same `(srcArch, dstMask)`. Runs ≥ `kRunThreshold`
   trigger a `reserveChunkRows(dstMask, runLen)` capacity hint on the
   destination chunk (`tests/migration_batch_test.cpp` validates the
   pre-reservation is determinism-safe).
2. Runs ≥ `cfg_.batchMigrateThreshold` (default 16) that pass the
   same-source-archetype precondition dispatch through
   `setMaskAndMigrateBatch` instead of N independent `setMaskAndMigrate`
   calls. Post-migrate value writes (for `CmdSetHealth` /
   `CmdSetFaction` / `CmdSetBoundingVolume`) still loop per-command.
3. Apply each command via `applyCommandImpl`; for `legacyCommitHash =
   true` only, mix into `commitHashAcc_` via `hashCommandImpl`.
4. `commandsThisStep_` accumulates the command count.

The single-threaded path is the deterministic reference: every
hash-determinism test compares the new path's commitHash against it.

### 10.2 Sharded path — `commitBuffersSharded`

`Config::singleThreadedCommit = false` opts every wave commit into the
sharded path. Auto-falls-through to single-threaded when
`totalCommands < 256` OR `totalValueOnly == 0` OR
`archetypeChunkCount() < 2`. Additionally, when
`Config::workloadAwareCommit == true`, falls through when
`globalCount * 100 >= totalCommands * workloadAwareGlobalPercent`
(default 30%) — the heuristic from S16.

Three passes per call:

**Pass A (sim thread).** Build the migrating-entity set into
`shardMigratingBitmap_` (keyed by `EntityHandle::index`) and the
parallel `shardMigratingIndices_` (so reset is O(touched) not
O(slotCount)). Walks each buffer's `globalIdx_` (S8 routing on) or the
full command stream (S8 routing off). Bitmap is wave-cumulative —
cleared at wave start, accumulated across every system's commit so
later systems' bucket entries are correctly demoted if an earlier
system migrated their entity.

**Pass B (sim thread, submission order).** For each command:

- Hash via `hashCommandImpl` (when `legacyCommitHash = true`).
- If migrating, oversize, or stale → **global lane**: apply on sim via
  `applyCommandImpl`. Migrating runs go through the batch-migrate path
  same as `commitBuffer` (`CommitBreakdown::batchedMigrations` mirrors
  the serial counter).
- If value-only (`CmdSetTransform` / `CmdSetVelocity` /
  `CmdSetAcceleration` / `CmdSetUserData`) AND target entity is NOT in
  `shardMigratingBitmap_` → push into `shardChunkBins_[destArchetype]`
  for Pass C.

**Pass C (workers + sim).** Apply per-chunk bins:

- Below `kMinBinForJob` → run inline on sim (S5). Saves the dispatch
  cost on bins too small to amortize the latch + steal + wake.
- When `cfg_.inlineLargestBin == true` (default) AND `largeBins ≥ 1` →
  identify the single largest large-bin and run it inline on sim too;
  only `largeBins − 1` jobs go to the `JobLatch` (S9). On balanced
  workloads this turns "sim waits for workers" into "sim is a peer of
  workers."
- Remaining bins dispatch through `JobSystem::submit` onto a
  `JobLatch` whose spin budget is `cfg_.jobLatchSpinIters` (S11).
- Each worker apply path uses `applyCommandNoHash` (pass B already
  hashed). `mut*()` setters re-look-up by `EntityHandle` so migrations
  in earlier passes don't break the chunk-row routing — Pass A
  guarantees migrate-touched entities went through the global lane.

**S10 row-split is opt-in (default OFF).** `Config::splitLargestBin`
partitions the single largest bin (when `largeBins == 1`) into
`min(workerCount + 1, largestBinSize / kMinBinForJob)` row-range
sub-bins. The split is determinism-preserving but the bench shows
the partitioner's per-cmd `std::visit + locate()` cost (~25-30 ns)
exceeds the apply cost (~13.6 ns), so the only S10-eligible bench
workload regresses +207%. Preserved as a fixed point for a future
revisit with record-time row-bucketing.

`CommitBreakdown` (`CommitBreakdown.hpp`) is the per-step Pass A/B/C
wall-clock + counter accumulation, accessible via
`Engine::lastCommitBreakdown()`. Always populated regardless of which
path runs.

### 10.3 Hash determinism (the v1.3 contract)

`Config::legacyCommitHash = false` (default) implements the
per-archetype state rollup. Every `ArchetypeChunk` carries
`mutable uint64_t cachedHash` + `mutable std::atomic<bool> hashDirty`;
all mutation paths set `hashDirty.store(true, relaxed)`.
`EngineImpl::finalizeCommitHash()` runs at end of step:

1. Collect dirty chunks (re-hash needed). Parallel re-hash when ≥ 2
   dirty (via `jobs_`); serial otherwise.
2. `hashChunkContent(chunk)`: FNV-1a-64 over `mask.bits` → `entities.size`
   → `entities[0..]` → each built-in dense vec in component-enum order
   (mask-gated) → each user column in ascending-bit order (`col.bit`,
   `col.stride`, `col.bytes`).
3. Sort all chunks by `mask.bits()` ascending and fold each chunk's
   `cachedHash` into a fresh running hash. Empty chunks are included.
4. Returns the combined hash, published to `EngineStats::commitHash`.

`Config::legacyCommitHash = true` keeps the v1.x byte-per-command FNV-1a-64
mix. The two paths produce different hashes for the same inputs by
construction (they're hashing different things); both are deterministic
across runs and machines for the same command stream. Slated for removal
one MINOR cycle after v1.3 ships per the deprecation policy in
`include/threadmaxx/version.hpp`.

`Config::logCommitHashEvery = N > 0` logs `commitHash tick=<T>
hash=0x<16hex>` via `ILogger@Info` every N ticks — incident-response
knob, default 0.

---

## 11. Render frame pipeline

### 11.1 Double-buffered storage

`EngineImpl` owns two `RenderFrame`s and parallel storage:

```cpp
std::array<std::vector<RenderInstance>,       2> renderInstanceBuffers_;
std::array<std::vector<RenderInstancePrev>,   2> renderInstancePrev_;
std::array<HierarchicalRenderStorage,         2> renderFrameStorage_; // cameras, lights, drawItems[], debug*
std::array<RenderFrame,                       2> renderFrames_;
std::atomic<unsigned> frontIndex_{0};
```

`buildRenderFrame()` writes into `back = 1 - frontIndex_`, then
publishes via `frontIndex_.store(back, release)`. `RenderFrame::*` spans
point into engine-owned vectors — renderers must finish consuming the
frame before `submitFrame` returns, or copy what they need. The atomic
swap is the cross-thread synchronization point if a future renderer
reads from a non-sim thread.

### 11.2 Auto-populated `instances` lane

The engine walks chunks and emits one `RenderInstance` per entity with
the `RenderTag` bit set and without `DisabledTag`. Each instance gets
the entity's transform, mesh/material IDs, render flags, and 64-bit
`UserData`. The previous-tick transform is looked up via
`prevTransformByIndex_` / `prevTransformGenByIndex_` — flat vectors
keyed by `EntityHandle::index` with a parallel generation guard so
stale entries are filtered. Entities appearing for the first time get
their own current transform as `prev`, so renderers can write
`lerp(prev, current, alpha)` with no spawn special case.

### 11.3 Hierarchical lane (via `RenderFrameBuilder`)

The §3.2 `ISystem::buildRenderFrame(RenderFrameBuilder&)` hook gets a
per-system builder (engine-owned, persistent across ticks for zero-
alloc steady state; reset before each call). Systems push:

- `addCamera(Camera)` — view + projection matrices (column-major).
  **Projection MUST be Vulkan-style (NDC z ∈ [0, 1]): `m22 = f/(n-f)`,
  `m32 = fn/(n-f)`.** GL-style clips near-camera vertices.
- `addLight(Light)` — directional / point / spot, color / intensity /
  range / cones / `castsShadow`.
- `addDrawItem(RenderPass, DrawItem)` — per-pass bins (Opaque /
  Transparent / ShadowCasters / Overlay). `DrawItem::cameraMask` is a
  32-bit bitset for the 32-camera cap.
- `addDebugLine`, `addDebugPoint`, `addDebugText` — debug overlays.
  Two `addDebugText` flavors: borrowed `string_view` (producer owns
  lifetime to the next frame swap) and owning-string (copies into a
  per-builder arena).

The engine merges every builder into the back frame in registration
order, so output is deterministic. Per-system `buildRenderFrameSeconds`
is recorded in `SystemStats`.

### 11.4 Renderer-neutral PODs

`include/threadmaxx/render/` ships every render type as a renderer-
neutral POD; the engine never positions Camera/Light/MeshSkinned/
AnimationPose as per-entity dense storage (cameras/lights are few,
AnimationPose is ringbuffered — `RenderFrameBuilder` fits them better
than `EntityStorage`).

`Visibility.hpp` adds `extractFrustum(camera) → Frustum`,
`intersectsFrustum` (AABB p-vertex test), `cullByFrustum(items,
bounds, cameras)` (writes `cameraMask` in place; 33rd+ camera silently
dropped). `InstanceBufferLayout.hpp` ships `alignas(16)
InstanceLayoutEntry` (128 B, std140-friendly) + `packInstance(item)`.
`UploadRing.hpp` is a header-only frame-to-frame bump allocator with
optional `setGrowOnOverflow(true)`.

### 11.5 Interpolation

`RenderFrame::alpha` is the wall-clock fraction past the last committed
tick (0..1). `step()` always submits with `alpha = 0`. `run()` calls
`submitInterpolatedFrame(alpha)` between ticks — mutates only
`renderFrames_[front].alpha` and re-submits; no rebuild because world
state is unchanged. Renderers cache previous-tick transforms (or use
the engine-provided `RenderFrame::prevTransforms` lane) and lerp.

---

## 12. EventChannel — typed inter-system queues

`EventChannel<T>` (`EventChannel.hpp`) is a typed double-buffered queue.
`Engine::events<T>()` returns the engine-owned channel — same instance
across calls and across threads. Channels are stored in
`EngineImpl::eventChannels_`, keyed by `std::type_index` under an
internal `eventChannelsMtx_`.

**Storage.** Per-channel state is a **lock-free MPSC Treiber stack**
(atomic `Node*` head) for the back buffer plus a stable front buffer.
`emit` CAS-prepends a `Node*`; producers contend only on the head
pointer, never on a mutex. `drain()` atomically `exchange(nullptr)` to
detach the stack, walks the list into `front_`, and reverses so
per-thread FIFO order is restored. The subscriber list is mutex-guarded
but touched only on subscribe / unsubscribe (low frequency).

**Lifetime.** The Treiber stack means a subscriber callback that
re-emits during drain lands on `backHead_` (the new tick) instead of
deadlocking — the older mutex-protected emit had a self-deadlock
hazard. Subscriber list is snapshotted before invocation so a callback
that subscribes/unsubscribes doesn't invalidate iteration.

**Subscriptions.** Two flavors:

- `subscribe(fn) → SubscriptionId` / `unsubscribe(id)` — persistent.
- `subscribeScoped(fn) → Subscription` — RAII (move-only, type-erased,
  auto-detaches; holds a `weak_ptr` so it safely no-ops if the channel
  dies first; `reset()` / `valid()`).

**Channel warm-up.** First call for a new `Ev` performs a map insert
under `eventChannelsMtx_`; subsequent calls are uncontended lookup-hits.
If worker threads or the stall-watchdog thread will be first to call
`events<Ev>()`, call it once on the sim thread at setup to avoid paying
the contended-insert cost on a worker.

**Per-engine serial.** `EngineImpl::engineSerial_` is a non-zero serial
assigned from a global atomic at construction. `Engine::events<T>()`
uses it as the validity key for its `thread_local` channel cache (§3.10.3
batch 24 / F8) — a fresh engine that lands at the same memory address
as a destroyed one has a different serial, so the cache invalidates
automatically. No UAF risk.

---

## 13. Resources — typed store + loaders + hot reload

`Resource.hpp` is the engine-owned typed resource store.

### 13.1 `ResourceRegistry`

Storage is `std::shared_ptr<void>` keyed by `std::type_index`, behind a
single internal mutex. Designed for setup-time registration + per-frame
lookups, not thousands of concurrent inserts (async loaders do I/O off-
thread and call `add()` when ready). Stale handles never alias new ones
— slot generation bumps on every `remove`.

`ResourceId<T>` is a typed (`index`, `generation`) handle. Passing a
`ResourceId<Mesh>` to `get<Texture>(id)` returns `nullptr` because the
type check fires first.

### 13.2 Refcounted `ResourceHandle<T>`

Opt-in via `addRefCounted<T>(value)`. Returns a `ResourceHandle<T>`
(refcount = 1). `acquire(id)` bumps; `~ResourceHandle` decrements; on
zero refcount the slot is freed AND its generation bumps. Legacy
`add`-ed slots are marked `refCounted = false` and `acquire` refuses
them. Do not mix `add`/`remove` and `addRefCounted`/`acquire` on the
same slot. `ResourceHandle::get()` / `operator->` / `operator*` are
the indirection sugar.

### 13.3 `IResourceLoader`

`IResourceLoader` (in `Resource.hpp`) is the per-tick I/O pump.
Registered via `Engine::addResourceLoader`. The engine calls
`loader->update(engine)` once per `step()` after the last `postStep`
commits and BEFORE the reservation reap / event drain. Teardown in
reverse-registration order during `shutdown()`. Engine never spawns
threads for loaders — each owns its own pool.

Optional virtuals:

- `onShutdown(Engine&)` — reverse-registration teardown.
- `markStale(index, generation, type)` — dispatched by
  `Engine::markResourceStale<T>(id)`; loader publishes
  `AssetReloaded{old/new index+gen, type}` on `events<AssetReloaded>()`
  after installing the replacement. `AssetReloaded::matches<T>(id)`
  filters.
- `cancel(Engine&)` — called BEFORE `update()` so a loader can drop
  newly-stale requests in the same tick. `LoaderStats::cancelled`
  aggregates.
- `stats() → LoaderStats` (pending / inFlight / ready / failed +
  memory). `Engine::aggregateLoaderStats()` sums them.

`Engine::preloadUntil(predicate, timeout = 5s)` pumps every loader's
`update()` in a yield loop until the predicate returns true or the
timeout elapses. Simulation doesn't advance. Sim-thread only.

---

## 14. Built-in components and tag-only categories

| Bit | Type                | Dense? | Default `spawn()` attaches? | Notes |
|---|---|---|---|---|
| 0   | `Transform`         | yes    | yes                          | Always present; movement/hierarchy/render-prep all read it. |
| 1   | `Velocity`          | yes    | yes                          | Engine never integrates. |
| 2   | `RenderTag`         | yes    | iff `meshId >= 0`            | Engine-built `instances` lane filters on this bit. |
| 3   | `UserData`          | yes    | yes                          | 64 bits the engine never interprets. |
| 4   | `EntityStructural`  | no     | n/a                          | **Scheduling-only** — "this system spawns/destroys." Never in per-entity masks. |
| 5   | `Acceleration`      | yes    | yes                          | Engine never integrates. |
| 6   | `Parent`            | yes    | iff `parent.valid()`         | Drives `HierarchySystem`. |
| 7   | `Health`            | yes    | no (explicit opt-in)         | §3.1 batch-5 dense slot. |
| 8   | `Faction`           | yes    | no                           | §3.1 batch-5 dense slot. |
| 9   | `AnimationStateRef` | yes    | no                           | §3.1 batch-5 dense slot. |
| 10  | `PhysicsBodyRef`    | yes    | no                           | §3.1 batch-5 dense slot. |
| 11  | `NavAgentRef`       | yes    | no                           | §3.1 batch-5 dense slot. |
| 12  | `BoundingVolume`    | yes    | no                           | §3.1 batch-5 dense slot. |
| 13  | `StaticTag`         | no     | no                           | Tag-only; e.g. terrain static geometry. |
| 14  | `DisabledTag`       | no     | no                           | Tag-only; `buildRenderFrame` skips. |
| 15  | `DestroyedTag`      | no     | no                           | Tag-only; gameplay marker only — engine does NOT auto-destroy. |

`ComponentSet` is `std::uint64_t`; bits 0..15 are allocated. 48 spare
bits are available before another widening. User components claim bits
≥ 16, registration-order stable within a process. Serialization writes
the full 64-bit field; v1 32-bit snapshots are not loadable.

### Hierarchy

`makeHierarchySystem(HierarchyConfig{})` is the engine-provided system
that propagates `Parent`-attached entities' world `Transform`. Reads
`{Transform, Parent}`, writes `{Transform}`. Runs single-threaded via
`ctx.single()`; resolves multi-level chains in one pass via DFS-with-
memoization. Composition:

```
position    = parent.position + rotate(parent.orientation, local.position)
orientation = parent.orientation * local.orientation        (Hamilton product)
scale       = local.scale                                    (default)
            = parent.scale ⊙ local.scale                     (if HierarchyConfig::propagateScale = true)
```

Cycle-safety via an `onStack` guard; scratch is system-member, reused
across ticks. Register the hierarchy system **after** any system that
writes `Transform` so it lands in a later wave and observes their
commits. See `doc/hierarchy.md`.

---

## 15. Reservations and bulk-spawn

`Engine::reserveEntityHandle()` / `SystemContext::reserveHandle()` /
`EntityStorage::reserveHandle()` allocate a slot under
`reservationMtx_`, bump its generation, and mark
`reserved = true, alive = false`. The handle is valid for use in
`CommandBuffer::spawn(handle, ...)` overloads and as the target of a
`Parent{handle, ...}` so a single job can spawn a parent and its
children atomically.

**Batch form:** `Engine::reserveEntityHandles(count, span)` /
`SystemContext::reserveHandles(count, span)` /
`EntityStorage::reserveHandles` acquire the mutex once. Returns the
number actually written (`min(count, span.size())`).

During commit, `materializeReserved` populates dense arrays and flips
`reserved → alive`. Reservations not consumed by step end are reaped in
`discardAllReservations` (generation bumped again so the user's handle
stops validating).

This is the one explicit exception to the "workers don't mutate state"
invariant: reservation manipulates the slot allocator only, not dense
data — and it does so under its own mutex. Dense arrays still grow
single-threaded during commit.

---

## 16. Telemetry

### 16.1 Statistics

`EngineStats` (`Stats.hpp`) is the per-tick snapshot — `tick`,
`lastStepSeconds`, `avgStepSeconds` (EWMA α = 1/16),
`commitDurationSeconds`, `jobsSubmittedLastStep`,
`commandsCommittedLastStep`, `aliveEntities`, lifetime totals, the
`commitHash`, and the engine-vs-renderer time split
(`engineBuildRenderFrameSeconds` / `renderSubmitSeconds`).

`SystemStats` is one entry per registered system (in registration
order). Includes `lastUpdateSeconds`, `avgUpdateSeconds`,
`commandsCommittedLastStep`, `waitSeconds` (latch wait inside
`parallelFor` — `lastUpdateSeconds - waitSeconds` is the rough
"calling thread did work" estimate), `peakQueueDepth` (max
`JobSystem::outstanding()` sampled after each `parallelFor` submit),
`buildRenderFrameSeconds`, plus the adaptive-tuning pair
`avgSubJobMicros` (1/16-EWMA of per-sub-job lambda µs) and
`subJobsLastStep`.

`JobSystemStats` is aggregate worker-pool counters — `totalJobs`,
`ownPops`, `stolenJobs`, `workerCount`, and the 16-bin log2-µs
`jobDurationHistogram`. A high `stolenJobs / totalJobs` ratio means
workers were starving and stealing — either grain is too coarse or
there isn't enough total work.

`FrameSnapshot` (`Trace.hpp`) bundles `{EngineStats, span<const
SystemStats>, JobSystemStats}` consistently. `writeJsonLines(os, snap)`
emits one JSON-Lines record per tick with `"commit_hash": "0x…"`.
`ChromeTraceWriter` streams Chrome trace JSON; ctor writes `[`,
`emit(snap)` appends one `step` record + one per-system record,
dtor writes `]`. Loadable in `chrome://tracing` / Perfetto.

### 16.2 Sinks (`Telemetry.hpp`)

`ITraceSink::onFrame(FrameSnapshot)` — installed via
`Engine::setTraceSink`. Sim-thread, called after every `step()`.
The engine never takes ownership. Must be cheap (budget a few µs).

- **`FileTraceSink`** — rolling Chrome-trace JSON. Rotates when
  `Config::rotationBytes` exceeded (default 64 MiB). `setAsync(true)`
  spawns an internal writer thread + queue so file I/O moves off the
  sim thread (joined on `setAsync(false)`, `onShutdown`, or
  destruction). Silent no-op on open failure — check
  `bytesWrittenCurrent()` after a few ticks to detect a bad path.
- **`HudTraceSink`** — single-snapshot seqlock-protected sink. `tryGet`
  is lock-free and torn-write-free. The seqlock has an inherent
  TSAN-visible race on `data_` BY DESIGN; suppressed in
  `cmake/tsan.supp`. **Do not "fix" it with mutexes** — that defeats
  the purpose. Activate suppressions via
  `TSAN_OPTIONS=suppressions=<path>/cmake/tsan.supp`.
- **`FrameBudgetWatcher`** — `ISystem` whose `postStep` emits
  `BudgetExceeded` when `lastStepSeconds` > target.

### 16.3 Stall watchdog

`Engine::setStallTimeout(seconds)` spawns a background thread that
periodically checks how long the current tick has been running; if
exceeded AND no `EngineStall` has been emitted for the current tick, it
emits one via `events<EngineStall>()`. Events drain on the sim thread
at the usual boundary. `stallTimeoutSeconds_` is `std::atomic<double>`
(relaxed). Joined on `setStallTimeout(0.0)` or `~EngineImpl`. Zero
overhead when disabled.

### 16.4 Async snapshot

`Engine::snapshotAsync(callback)` captures `world().snapshot()`
synchronously on the sim thread (the snapshot copy MUST be sync —
`WorldView` caches chunk pointers, not contents, so a mid-snapshot
commit would race), then posts the user's callback onto a dedicated
engine-owned `snapshotWorker_` thread (lazily spawned, joined in
`shutdown`). Multiple in-flight callbacks queue in submission order.
The callback runs on the writer thread and must not call back into the
engine's mutation API.

---

## 17. Adaptive tuning (`Tuning.hpp` + `AdaptiveGrainPolicy.hpp`)

`ITuningPolicy` is the engine's pluggable tuner hook:

```cpp
class ITuningPolicy {
    virtual void observe(EngineStats, span<SystemStats>, JobSystemStats) = 0;
    virtual std::optional<TuningPatch> propose() = 0;
};
```

Engine calls in `step()` after the trace-sink callback (same data a
sink would see). Any non-empty patch is stored in
`pendingPatch_` and applied at the TOP of the NEXT `step()` BEFORE
`preStep` — never mid-wave. Patch application overwrites
`systemPreferredGrain_[i]` for each matched name (linear name scan);
unknown names log `[tuning] grain override for unknown system 'X'
ignored` at `Warn`. Applied changes log `[tuning] 'X' preferredGrain
old → new` at `Info`.

**`TuningMode`** is `Off` / `Active` / `Scripted`. Off matches v1.3
behavior exactly (single null check per tick). Active records every
applied patch to the attached `TuningTrace` (keyed by tick). Scripted
ignores `policy.propose()` and pulls patches from
`trace.tryGet(currentTick)` — networked / lockstep replay. Installing a
non-null policy implicitly transitions to Active; detaching (nullptr)
reverts to Off.

**`AdaptiveGrainPolicy`** (header-only) is the built-in policy.
Per-system EWMA of `avgSubJobMicros` drives `preferredGrain` toward a
hold band `[targetSubJobMicros/2, targetSubJobMicros*4]`. Hysteresis
via `minSamplesPerChange` same-direction samples + `cooldownTicks`
between consecutive fires. ε-greedy random one-step walks
(`explorationEpsilon`) escape local minima. RNG seeded as
`randomSeed ^ currentTick` so two runs against identical input
produce identical patch streams. Defaults are conservative
(`cooldownTicks = 60`, `targetSubJobMicros = 200`, `stepSize = 1.5`,
`initialGrain = 64`). Determinism contract: same input + same scripted
patch stream = bit-identical `commitHash` stream — tuning is a
scheduling knob and never touches storage or commit order.

---

## 18. Cancellation, budgets, priorities

`Engine::setTickBudget(seconds)` caps wall-clock per tick. The engine
samples after every wave; if elapsed > budget AND `setSkipPolicy(
SkipPolicy::Budget)` (the default), `overBudget_` flips and subsequent
waves' `ISystem::skippable()` systems are skipped. `preStep`,
`postStep`, `buildRenderFrame` are NEVER skipped.

`SkipPolicy::Scripted` ignores `tickBudget_` and consults
`scriptedSkips_` (populated via `Engine::pushScriptedSkip(tick, name)`).
Networked / lockstep games run `Budget` on the authoritative server,
drain `EventChannel<SystemSkipped>`, broadcast the log, clients replay
with `Scripted` — world hashes match.

`SystemContext::shouldYield()` exposes the over-budget flag — a single
atomic load, cheap to poll inside long `parallelFor` bodies.

`SystemSkipped { tick, systemName, reason }` event (`SkipPolicy.hpp`)
emitted on skip; `reason` is `"budget"` or `"scripted"`.

`JobPriority { High, Normal, Low }` (`System.hpp`). `parallelFor`
overloads accept it; no-priority overloads default to `Normal`. Workers
scan deques in priority order; backwards-compatible — collapses to
single-deque behavior when every job is `Normal`.

---

## 19. Pause and time-scale

`Engine::setTimeScale(s)` clamps negative `s` to zero; `dt` becomes
`fixedStepSeconds * s`. `tick()` and `simulationTime()` advance
independently of time scale — only what game logic computes from `dt`
changes.

`setPaused(true)` makes `step()` a no-op (zeroes per-tick stats so HUD
overlays don't show stale numbers); `run()` keeps re-submitting the
current front frame so the renderer doesn't freeze. `paused_` is
atomic so any thread can flip it (same shape as `requestQuit()`).

---

## 20. Serialization (`Serialization.hpp`)

Header-only. Per-component `serialize` / `deserialize` for every
built-in plus `Vec3`, `Quat`, `ComponentSet`, and the `WorldSnapshot`
POD. `World::snapshot()` copies the engine's dense arrays (sourced from
the stitched view) into a `WorldSnapshot`.

**Binary format.** `[magic 'SXMT'][version u32][count u64][N dense
arrays, each prefixed by u64 length]`. Deserialize rejects mismatched
magic/version. Current version is `kWorldSnapshotVersion = 2`.

Restoration is **game-side via `cb.spawn(...)`** — the engine never
bypasses commit. The reverse path is "iterate dense arrays from the
snapshot, emit a spawn per entity, optionally apply tags afterward."

The format is host-endian and intentionally unstable across runs of
different builds — it's the local-quicksave format, not a cross-process
or cross-version contract. Cross-build content stability is the game's
responsibility.

---

## 21. Logger (`Logger.hpp`)

`ILogger` is a single `log(LogLevel, std::string)` virtual. The engine
routes startup / shutdown / registration / loader-error messages
through whichever sink is installed via `Engine::setLogger`. `nullptr`
restores `DefaultLogger` (writes to `std::cerr` at `Warn+`).
`EngineImpl::logger()` returns the active sink. The engine doesn't take
ownership.

---

## 22. Examples

- **`examples/minimal/`** — headless integration smoke test.
  `threadmaxx_minimal [maxTicks]` runs the engine with a console
  renderer; successful runs print `[frame]` lines with monotonically
  increasing ticks and end with `[ConsoleRenderer] shutdown after N
  frames`. The smoke contract.
- **`examples/boids/`** — non-headless SDL2 example. Exercises the
  wave scheduler (steer and integrate land in distinct waves because
  their read/write sets conflict on `Velocity` and `Transform`) and
  shows a concrete `IRenderer` against a real windowing backend.
- **`examples/vulkan_renderer/`** — opt-in static library
  `threadmaxx::vulkan_renderer` + smoke binary. Requires Vulkan 1.3 +
  GLFW + glslc. Public header
  `include/threadmaxx_vk/VulkanRenderer.hpp`; `src/` is private.
  Vertex layout: binding 0 = cube vertex (pos[3] + normal[3], 24 B),
  binding 1 = per-instance `InstanceLayoutEntry` (128 B). Pipeline:
  `cullMode = VK_CULL_MODE_BACK_BIT`, `frontFace =
  VK_FRONT_FACE_COUNTER_CLOCKWISE` — OBJ assets MUST use CCW-from-
  outside winding. Pre-built SPIR-V embedded via
  `cmake/EmbedSpv.cmake`.
- **`examples/rpg_demo/`** — opt-in (built when `vulkan_renderer` is).
  Terrain + player + NPCs + pickups + particles + combat + HUD.
  Game-side only; nothing in `include/` or `src/` is touched.
  Demonstrates `UserComponent<T>` registration end-to-end.

---

## 23. Tests, benches, sanitizers

`tests/` is the contract pin. One executable per test via the no-
dependency `tests/Check.hpp` harness; non-zero exit = failure. Roughly
120+ tests at the time of writing — see `tests/COVERAGE_AUDIT.md` for
the public-API coverage record. New behavior should land with a test
in the same PR.

The integration smoke test is `examples/minimal/threadmaxx_minimal` —
a successful headless run is the bar.

**Opt-ins.**

- `-DTHREADMAXX_BUILD_BENCHMARKS=ON` — `bench/` binaries (commit,
  migration, rpg_stress, particle_storm, …). Not registered with
  ctest; run binaries directly.
- `-DTHREADMAXX_BUILD_LONG_SOAK=ON` — `tests/concurrency_soak_long.cpp`
  (50× the ticks of `concurrency_soak_test`, ~5-6 min).

**Sanitizer hygiene.** ASAN, UBSAN, TSAN trees are expected to pass
clean. TSAN needs
`TSAN_OPTIONS=suppressions=cmake/tsan.supp` for the HudTraceSink
seqlock. Sanitizer trees skip benches, the rpg_demo subfolder, and the
SIMD sibling library — "sanitizers run engine-side correctness only"
is the standing convention.

---

## 24. Memory ownership and lifetimes

| Object                    | Owned by                  | Lifetime                                   |
|---|---|---|
| `Engine`                  | game                      | game-controlled                            |
| `EngineImpl` (PImpl)      | `Engine`                  | `Engine` lifetime                          |
| `World` / `WorldImpl`     | `EngineImpl`              | `EngineImpl` lifetime                      |
| `EntityStorage`           | `WorldImpl`               | `WorldImpl` lifetime                       |
| `ArchetypeTable`          | `EntityStorage`           | `EntityStorage` lifetime                   |
| `ArchetypeChunk`s         | `ArchetypeTable::chunks_` | until `~ArchetypeTable`; chunks are stable across spawns / destroys, only NEW masks ever append |
| `JobSystem`               | `EngineImpl`              | `EngineImpl` lifetime; workers spawned at `initialize`, joined at `~EngineImpl` |
| `ResourceRegistry`        | `EngineImpl`              | `EngineImpl` lifetime; refcounted resources outlive the registry only if the user holds a `ResourceHandle<T>` past `~Engine` (which keeps the `shared_ptr<void>` alive)     |
| `IResourceLoader`         | `EngineImpl` (via unique_ptr) | torn down in reverse-registration order at `shutdown` |
| `UserComponentRegistry`   | `EngineImpl`              | `EngineImpl` lifetime; non-owning pointer wired into `ArchetypeTable` at `initialize` |
| `EventChannel<T>`         | `EngineImpl::eventChannels_` | `EngineImpl` lifetime; addresses are stable (held via `unique_ptr`) |
| `RenderFrame` storage     | `EngineImpl` (double-buf) | `EngineImpl` lifetime; spans into it valid until next frame swap |
| `CommandBuffer`           | per-system context (`SystemContextImpl::buffers_`) | wave lifetime; cleared between waves |
| `ScratchArena`            | per-system context (`SystemContextImpl::arenas_`) | wave lifetime; slabs reused across ticks |
| `Subscription` (RAII)     | game                      | holds `weak_ptr` to channel; safe to outlive |
| `ITraceSink` / `ILogger` / `IRenderer` / `ITuningPolicy` / `TuningTrace` | game | engine never takes ownership; must outlive engine |
| `ISystem` (registered)    | `EngineImpl` (via unique_ptr) | from `registerSystem` through `shutdown` (reverse order) |
| Stall watchdog thread     | `EngineImpl`              | spawned by `setStallTimeout(>0)`, joined by `setStallTimeout(0)` or `~EngineImpl` |
| Snapshot writer thread    | `EngineImpl`              | lazily spawned by first `snapshotAsync`, joined at `shutdown` |

---

## 25. Where to read next

- `CLAUDE.md` — daily contributor playbook (recipes, gotchas, "what
  to grep when"). The architectural reference distilled for hands-on
  changes.
- `CLAUDE_ARCHIVE.md` — every shipped batch's deep log.
- `SHARDED_OPTIMISATION.md` — the sharded-commit S0..S16 batch series
  notes. Particularly relevant for anyone touching `commitBuffersSharded`.
- `ADAPTIVE_TUNING.md` — the T1..T6 adaptive-tuner batch notes.
- `FUTURE_WORK.md` — planning doc; what's deferred or explicitly out
  of scope.
- `doc/index.md` — the multi-page Markdown user guide. New user-facing
  concepts get a page here.
- `doc/performance_tuning.md` — the public reference for `preferredGrain`,
  `singleThreadedCommit`, `legacyCommitHash`, `setTickBudget`, and the
  bench / diff / chrome://tracing workflow.
- `doc/migration_v1_to_v1_2.md` + `doc/migration_v1_2_to_v1_3.md` —
  version migration checklists.
- `tests/COVERAGE_AUDIT.md` — public-API coverage record.
- Doxygen-generated API reference: `cmake --build build --target doc`
  → `doc/generated/html/index.html`.
