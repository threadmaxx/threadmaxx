# `threadmaxx_navmesh` — Maintainer Guide

Internal documentation for engineers extending or debugging the
navmesh library. For external usage, see `USER_GUIDE.md`.

## Architecture overview

```
                ┌──────────────────────────────────────┐
                │  Consumer code                       │
                │  #include <threadmaxx_navmesh/...>   │
                └────────────────┬─────────────────────┘
                                 ▼
        ┌──────────────────────────────────────────────────┐
        │  Public surface (one header per concern)         │
        │                                                  │
        │   types.hpp       config.hpp     version.hpp     │
        │   mesh.hpp        bake.hpp                       │
        │   query.hpp       crowd.hpp                      │
        │   steering.hpp    obstacle.hpp                   │
        └────────────────┬─────────────────────────────────┘
                         ▼
            ┌───────────────────────────────────────┐
            │  detail/                              │
            │    a_star.hpp      — open-set heap +  │
            │                      scratch state    │
            │    funnel.hpp      — Simple Stupid    │
            │                      Funnel kernel    │
            │    solver.hpp      — locate +         │
            │                      solvePrepared    │
            │    spatial_hash.hpp — XZ grid index   │
            │    bitset.hpp      — small bit ops    │
            └───────────────────────────────────────┘
                         ▼
            ┌───────────────────────────────────────┐
            │  threadmaxx::threadmaxx (math PODs)   │
            │  Vec3 — only dep at link              │
            └───────────────────────────────────────┘
```

The library is a `STATIC` archive (`libthreadmaxx_navmesh.a`).
The CMake target `threadmaxx::navmesh` carries the include
directory, `cxx_std_20`, and a transitive link to
`threadmaxx::threadmaxx` (math PODs only — no engine subsystems).

## Wire format (v2)

`bakeNavMesh()` produces — and `NavMeshRegistry::load()` parses —
the following byte stream (host-endian POD):

```
[magic 'NVMX' u32]              # kNavMeshBlobMagic = 0x584D564E
[version u32 = 2]               # kNavMeshBlobVersion
[nameLen u64][name bytes]
[tileCount u32]                 # ≤ kNavMeshMaxTiles
  per tile:
    [tileId u32]
    [vertexCount u32]           # ≤ kNavMeshMaxPolysPerTile * 6 (loose cap)
    [polyCount u32]             # ≤ kNavMeshMaxPolysPerTile
    [indexCount u32]            # Σ polygon.indexCount
    [Vec3 vertices[vertexCount]]
    [NavPoly polygons[polyCount]]
    [u32 vertexIndices[indexCount]]
    [u32 neighborPolys[indexCount]]  # kInvalidPolyIndex for borders
[portalCount u32]               # ≤ kNavMeshMaxPortals
[NavPortal portals[portalCount]]
```

All numeric fields are host-endian; cross-arch transfer is a v1.x
topic (byte-swapping reader gated on `version >= 3`). The reader
rejects bad magic OR bad version OR truncation OR out-of-range
indices OR per-load count limits via `NavMeshLoadError`.

Bumping `kNavMeshBlobVersion` is mandatory on any field addition.
Tests under `tests/navmesh/test_navmesh_registry_invalid.cpp` pin
the failure modes.

## The solver pipeline

```
PathRequest
    │
    ▼
locate(start) + locate(goal)     ← XZ even-odd point-in-poly
    │                               (Solver.cpp::locate)
    ▼
AStarOpenSet (binary min-heap)   ← detail/a_star.hpp
   + heuristic = euclidean dist
   + edge cost = centroid-to-centroid
   + area-mask filter
   + obstacle overlay gate (N8)
    │
    ▼
corridor (vector<PolyLocation>)  ← polygon-space path
    │
    ▼
stringPullFunnel(portals, out)   ← detail/funnel.hpp
    │
    ▼
PathResult { waypoints, corridor, cost, success, partial }
```

`detail/solver.hpp` is the single source of truth for the
solver. Both `PathQueryService` and `BatchPathSolver` consume
`solvePrepared(SolverScratch&, NavMesh, PreparedRequest&,
PathResult&)`. The differences between the two are entirely
queue / pool machinery — the solving logic is identical.

### A* scratch lifecycle

`AStarState` is a per-thread reusable POD holding the open-set
heap + the `cameFrom` / `gScore` / `hScore` flat vectors keyed
by tile-prefix-sum node index. The scratch is recycled across
requests on the same worker — `solvePrepared` clears it without
deallocating, so a steady stream of requests pays zero per-call
heap pressure after warmup.

### Funnel portal sequence

The corridor produced by A* is a sequence of polygon ids; the
funnel needs a sequence of `(left, right)` portal edges. The
sequence is built in `solvePrepared`:

- Start portal: `(start, start)` — degenerate "both endpoints =
  start" portal, anchors the funnel.
- One portal per intra-corridor step: `findEdgeTo(from, to)`
  returns the from-poly's edge index pointing at `to`; LEFT =
  `vertices[(e+1) % n]`, RIGHT = `vertices[e]`. This convention
  pairs with the standard CCW-positive `cross2D` used inside the
  funnel.
- Cross-tile transitions consult `NavMesh::crossTileNeighbor` to
  flip the FROM tile / poly across the portal before computing
  the edge index.
- End portal: `(goal, goal)`.

`stringPullFunnel(portals, out)` runs the standard Simple Stupid
Funnel walk over the resulting list and emits the smoothed
`[start, ..., goal]` waypoint sequence. Pinch-at-goal duplicates
are deduped so the final waypoint is never emitted twice.

## Registry threading contract

`NavMeshRegistry` is thread-safe across `load` / `unload` /
`find` / `meta` / `isValid` under a single internal mutex. The
expected engine integration is:

- **Setup / level-load (sim thread)**: `load(blob)`. Stash the
  returned `NavMeshRef` on game-side state.
- **Per-tick (any thread)**: `find(ref)` / `meta(ref)` — internal
  mutex acquire is cheap (no contention from worker reads, the
  bottleneck is the solver itself).
- **Shutdown / level-unload (sim thread)**: `unload(ref)`. Make
  sure no path queries against the mesh are in flight.

Generation-tagged refs catch use-after-unload: every `load` slot
records a monotonically-increasing generation; `unload` bumps it.
A stale ref's generation no longer matches; `find` returns
`nullptr`. The slot itself is recycled on the next load.

## `PathQueryService` machinery

`src/PathQueryService.cpp` is the async queue. The worker loop:

1. **Pop** the front request id under `mtx`.
2. **Cancellation check** against the `cancelled` set; if cancelled,
   drop and loop.
3. **Mesh resolve** under the registry's mutex; if stale, store a
   failed `PathResult` and signal `doneCv`.
4. **Unlock** (the solve runs without holding any service mutex).
5. **Solve** via `solvePrepared` against per-worker `SolverScratch`.
6. **Re-lock**; remove from `inFlight`; cancellation re-check; if
   not cancelled, store the result.
7. **Signal** `doneCv` so waiters wake.

Tombstones (cancelled / cleared ids) are stored in a separate set
so a late-arriving `wait` returns nullopt quickly via the
tombstone path rather than blocking until timeout.

`workerThreads = 0` skips the queue entirely — `request()` runs
the solve inline on the caller thread, stores the result, and
returns the id. Used by tests + sync-style callers.

## `BatchPathSolver` machinery

`src/BatchPathSolver.cpp` owns a persistent worker pool with the
same per-thread `SolverScratch` model as `PathQueryService`. The
producer thread participates so the effective parallelism is
`workerThreads + 1`.

Per-batch dispatch via:

- `batchGen` (monotonic counter) — workers wake on `batchCv`,
  check `batchGen != lastSeen`, jump into the work loop.
- `nextIndex` (atomic `fetch_add(1)`) — workers claim requests
  by atomically incrementing the cursor.
- `doneCount` (atomic) — workers `fetch_add(1)` after each
  request; the producer waits on `doneCv` until `doneCount ==
  requests.size()`.

The worker scratch is per-thread, recycled across batches —
hot-path allocation pressure is zero after warmup.

Concurrent `solve()` calls on the same instance are explicitly
undefined.

## `ObstacleOverlay` machinery

`src/ObstacleOverlay.cpp` is a thin shell around
`detail/spatial_hash.hpp` (`SpatialHashXZ<ObstacleId, Entry>`).
Each obstacle gets one entry per cell its XZ AABB covers; the
cell math is floor-to-negative-infinity to keep obstacles
straddling the origin from biasing into a single cell.

`update(id, obstacle)` rebuilds the obstacle's spatial-hash
buckets atomically: the call computes the new cell set, walks
the old cell set dropping the obstacle, then walks the new cell
set inserting it. No transient "ghost" cells are observable.

`isBlocked(xz, callerMask)` consults the matched cell, then runs
a precise AABB-vs-point test on each payload's stored AABB and
matches the area mask. Cheap enough to call from the solver's
per-edge expansion loop.

## Hot-path allocation discipline

The batch-solver bench (`bench/navmesh_batch_bench.cpp`)
measures steady-state qps. The discipline that keeps it
allocation-free in steady state:

- **`SolverScratch`** is per-worker and reused across requests.
  Clears never shrink capacity.
- **`PathQueryService`** queue / stored maps use `unordered_map`s
  that hold their capacity once warm.
- **`PathResult::waypoints` / `corridor`** are allocated per
  request — callers should move-construct them out of the result
  into agent state.
- **`BatchPathResult::results`** is sized once at the top of
  `solve()` and never re-allocated within the batch.

## Adding a new bake validation arm

The bake pipeline (`src/BakeTool.cpp`) validates input in order:
empty input → invalid index → degenerate triangle →
non-manifold edge → polygon cap. To add a new validation arm:

1. **Append a `BakeError` enum value** in `bake.hpp`. Existing
   values stay stable (consumers may switch on the enum).
2. **Add the check** in `bakeNavMesh` at the right phase. Each
   check returns a `BakeResult { error = ..., diagnostic = ...,
   blob = {} }` so the caller can report it.
3. **Test** in `tests/navmesh/test_navmesh_bake_validation.cpp` —
   one block per failure mode, asserting both the enum value and
   that the diagnostic is non-empty.

Bumping the enum is MINOR (additive) unless you remove or
reorder existing values.

## Adding a new wire-format field

The wire format is host-endian POD. To add a field:

1. **Bump `kNavMeshBlobVersion`** in `config.hpp`. Document the
   change in `CHANGELOG.md`.
2. **Extend the writer** in `src/BakeTool.cpp` (`appendBytes` /
   `writePod` / `writeString` / `writeVec` helpers).
3. **Extend the reader** in `src/NavMeshRegistry.cpp`. The
   reader rejects any version that isn't the current one with
   `NavMeshLoadError::UnsupportedVersion`.
4. **Test** the new field in
   `test_navmesh_registry_load.cpp` (round-trip) +
   `test_navmesh_registry_invalid.cpp` (truncation behavior on
   the new section).

The CHANGELOG entry must say "wire format v→N→N+1; reload after
upgrade."

## Adding a new public method on `PathQueryService` /
`BatchPathSolver`

1. **Method signature** in the public header (`query.hpp` /
   `crowd.hpp`). Add a Doxygen `@brief` + `@thread_safety` line.
2. **Implementation** in the matching `src/` file. If the method
   touches the queue / pool state, take `mtx` early and release
   it before any solver work.
3. **Test** in `tests/navmesh/`. The Werror tree
   (`-DTHREADMAXX_WARNINGS_AS_ERRORS=ON`) is the discipline gate
   for public-surface additions — it surfaces silent narrowing
   conversions that clang's default warnings miss.

Adding a new method is MINOR (additive). Changing an existing
signature is breaking — deprecate first, remove in the next
major.

## Testing strategy

Three layers, all in `tests/navmesh/`:

1. **Unit tests** — one executable per public concept (registry
   load/unload/invalid, tile adjacency, A* paths, funnel
   smoothing, async queue cancel/clear, batch correctness/
   determinism, steering straight/corner/finished, obstacle
   blocks/update/remove, bake smoke/validation/areas).
2. **Composition tests** — exercise the full pipeline end-to-end
   (bake → load → solve → follow). The bake smoke test is the
   reference.
3. **Determinism tests** — same input batch on different worker
   counts produces byte-identical results
   (`test_navmesh_batch_determinism.cpp`).

All tests use the project-wide `Check.hpp` harness — one
executable per test, non-zero exit means failure.

### Werror tree

The library compiles clean under
`-DTHREADMAXX_WARNINGS_AS_ERRORS=ON` (which adds
`-Wsign-conversion -Wconversion -Wold-style-cast -Wshadow`). Use
the `build-werror/` tree as the discipline gate when touching
public surface:

```
cmake -B build-werror -DTHREADMAXX_WARNINGS_AS_ERRORS=ON
cmake --build build-werror -j
(cd build-werror && ctest -R '^navmesh\.' --output-on-failure)
```

## Repository layout (engineer-facing)

```
include/threadmaxx_navmesh/
├── README.md                  # Top-level overview
├── CHANGELOG.md               # Per-release notes
├── DESIGN_NOTES.md            # Original spec (don't edit unless re-scoping)
├── FUTURE_WORK.md             # Batch-by-batch landed work + v1.x candidates
├── USER_GUIDE.md              # User-facing docs
├── MAINTAINER_GUIDE.md        # This file
├── threadmaxx_navmesh.hpp     # Umbrella include
├── version.hpp                # Library version macros + version_string()
├── types.hpp                  # NavMeshId / NavTileId / NavPolyId / NavMeshRef / PathId / NavAgentId
├── config.hpp                 # Wire-format magic / version / per-load limits + NavMeshConfig
├── mesh.hpp                   # NavMesh / NavTile / NavPoly / NavPortal / NavMeshRegistry
├── bake.hpp                   # BakeInput / BakeResult / bakeNavMesh
├── query.hpp                  # PathRequest / PathResult / PathQueryService
├── crowd.hpp                  # BatchPathRequest / BatchPathResult / BatchPathSolver
├── steering.hpp               # FollowPathInput / FollowPathOutput / followPath
├── obstacle.hpp               # DynamicObstacle / ObstacleOverlay
└── detail/
    ├── a_star.hpp             # AStarOpenSet + AStarState scratch
    ├── bitset.hpp             # small bit ops
    ├── funnel.hpp             # stringPullFunnel + FunnelPortal
    ├── solver.hpp             # PolyLocation / SolverScratch / PreparedRequest / solvePrepared
    └── spatial_hash.hpp       # SpatialHashXZ<KeyT, ValueT>

src/threadmaxx_navmesh/
├── CMakeLists.txt
├── NavMeshRegistry.cpp        # Blob parser + registry slot pool
├── Solver.cpp                 # locate + solvePrepared (shared by both services)
├── PathQueryService.cpp       # Async queue + worker loop
├── BatchPathSolver.cpp        # Persistent worker pool
├── ObstacleOverlay.cpp        # Spatial-hash overlay
└── BakeTool.cpp               # Pure-function bake

tests/navmesh/
├── CMakeLists.txt
├── fixtures/blob_builder.hpp                 # Hand-built test meshes
├── test_navmesh_registry_load.cpp            # Happy path load
├── test_navmesh_registry_unload.cpp          # Ref-staleness on unload
├── test_navmesh_registry_invalid.cpp         # Every NavMeshLoadError mode
├── test_navmesh_tile_adjacency.cpp           # Intra-tile neighbor walk
├── test_navmesh_tile_traversal.cpp           # Cross-tile portal walk
├── test_navmesh_astar_simple.cpp             # Happy-path single A*
├── test_navmesh_astar_unreachable.cpp        # Unreachable / off-mesh / invalid mesh
├── test_navmesh_astar_partial.cpp            # Best-effort partial path
├── test_navmesh_astar_area_mask.cpp          # Area-mask filter
├── test_navmesh_funnel_straight.cpp          # Funnel on straight corridor
├── test_navmesh_funnel_corner.cpp            # Funnel L-corner pinch
├── test_navmesh_funnel_pinch.cpp             # Funnel double pinch via masked detour
├── test_navmesh_query_async_smoke.cpp        # Async queue end-to-end
├── test_navmesh_query_cancel.cpp             # Cancel queued / mid-solve / stored
├── test_navmesh_query_clear.cpp              # Drop everything mid-flight
├── test_navmesh_batch_correctness.cpp        # Batch matches sync
├── test_navmesh_batch_determinism.cpp        # Worker count doesn't change output
├── test_navmesh_follow_straight.cpp          # Steering on straight segment
├── test_navmesh_follow_corner.cpp            # Steering angular-rate bound at corner
├── test_navmesh_follow_finished.cpp          # Arrival radius + degenerate corridor
├── test_navmesh_obstacle_blocks.cpp          # Obstacle forces detour
├── test_navmesh_obstacle_update.cpp          # Moving obstacle reroutes
├── test_navmesh_obstacle_remove.cpp          # Removed obstacle restores path
├── test_navmesh_bake_smoke.cpp               # Triangle soup → blob → solve
├── test_navmesh_bake_validation.cpp          # Every BakeError mode
└── test_navmesh_bake_areas.cpp               # Per-triangle area tags pass through

bench/
└── navmesh_batch_bench.cpp                   # Throughput on 256-poly mesh
```

## Library version (`version.hpp`)

The library exposes a semver version via macros and a constexpr
function:

```cpp
#define THREADMAXX_NAVMESH_VERSION_MAJOR 1
#define THREADMAXX_NAVMESH_VERSION_MINOR 0
#define THREADMAXX_NAVMESH_VERSION_PATCH 0
#define THREADMAXX_NAVMESH_VERSION (MAJOR*10000 + MINOR*100 + PATCH)

constexpr const char* version_string() noexcept;  // → "1.0.0"
```

When bumping, update **both** the macros AND the string literal
returned by `version_string()`. Also append a section to
`CHANGELOG.md`.

## Versioning / ABI

The library produces a static archive; downstream callers
recompile against the headers, so source-ABI is what matters.

- **Public POD layouts** (`NavMeshRef`, `NavPoly`, `NavPortal`,
  `NavTile`, `NavMeshMeta`, `PathRequest`, `PathResult`,
  `PathQueryServiceConfig`, `BatchPathRequest`, `BatchPathResult`,
  `BatchPathSolverConfig`, `FollowPathInput`, `FollowPathOutput`,
  `DynamicObstacle`, `ObstacleOverlayConfig`,
  `BakeInputTriangle`, `BakeInput`, `BakeResult`,
  `NavMeshConfig`) are stable. Layout changes are breaking
  (bump MAJOR).
- **Public method signatures** are stable. Adding overloads is
  fine; changing existing signatures is breaking.
- **Enum values** (`NavMeshLoadError`, `PathRequestStatus`,
  `BakeError`) — existing values are stable; appending new
  values at the end is additive (MINOR).
- **`detail::*`** is internal. Consumers should not include
  `detail/` headers directly; tests and bench are allowed to.
- **Wire format constants** (`kNavMeshBlobMagic`,
  `kNavMeshBlobVersion`) — magic is stable, version bumps on
  any wire-format field addition.

When evolving:

- Add new bake validation arms via the workflow above. Append
  the `BakeError` value; never reorder.
- Add new public functions via overloads or new headers.
  Removing is breaking — deprecate first, remove in the next
  major.
- Wire format changes bump `kNavMeshBlobVersion` and the
  reader's accept-list.

## Common pitfalls

### "My baked mesh loads but A* returns `StartNotOnMesh`"

The locate path is XZ-only — your start position's y component
doesn't matter, but its x/z must fall inside some polygon's XZ
projection. Polygons that look "above" each other on a 3D map
collapse to the same XZ rect; if you have overlapping floors
the locate picks the first match. Tile streaming (v1.x) plus a
y-band locator is the v1.x answer.

### "The corridor is correct but `waypoints` doesn't go where I expect"

The funnel walks polygon edges. The waypoint sequence is the
locally-shortest XZ path *constrained to corridor edges* — it
doesn't pull in by the agent radius (v1.x). If your agent's
visual width is wider than the corridor, you'll see clipping;
either widen the navmesh or wait for the v1.x corridor-shrink
work.

### "Async queries are returning nullopt forever"

Three possibilities, in order: (1) The id is unknown (you
saved a different id, or the request id is 0 = "failed
pre-solve"; check `lastRequestStatus()`). (2) The id was
cancelled (`cancel(id)` was called; `tryGet` returns nullopt
for cancelled ids). (3) The worker thread crashed during
solve — should never happen, but check stderr.

### "Batch solver determinism test fails on a new feature"

`test_navmesh_batch_determinism.cpp` solves the same batch on
1 / 2 / 4 workers and asserts byte-identical results. The
solver is structurally deterministic because each request is
solved end-to-end on one worker scratch — *order of completion*
varies but *per-index output* never does. If your test fails:
you've introduced cross-request shared state somewhere; track
it down before merging.

### "I added a new field to `PathRequest` and old code stopped compiling"

Aggregate initialization broke. Reorder so the new field is
last (additive) or default-construct it; either way the existing
field positions stay stable for callers that initialize
positionally. Same applies to `PathResult`, `BakeResult`, etc.

### "Obstacle overlay tests are flaky"

You're probably mutating the overlay from one thread while
querying from another. The mutation contract is single-threaded
— freeze the overlay during the solve, mutate during `preStep`.

### "Adding a new built-in component to `threadmaxx`'s core breaks navmesh tests"

Unlikely — the library depends on `Vec3` only. If a core change
touches `Vec3`, every sibling library will be affected; that's
an engine-wide breaking change.

## See also

- `DESIGN_NOTES.md` — original spec.
- `FUTURE_WORK.md` — batch-by-batch landed work + open follow-ups.
- `USER_GUIDE.md` — user-facing API reference.
- `/CLAUDE.md` (repo root) — meta-instructions for AI-assisted
  development of `threadmaxx` and its sibling libraries.
