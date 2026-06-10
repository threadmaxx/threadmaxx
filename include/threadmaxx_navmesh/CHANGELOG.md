# Changelog

All notable changes to `threadmaxx_navmesh` are recorded here.
The format follows [Keep a Changelog](https://keepachangelog.com/);
the project adheres to [Semantic Versioning](https://semver.org/).
See [`MAINTAINER_GUIDE.md`](MAINTAINER_GUIDE.md) for the bump rules.

## [1.0.0] — 2026-06-10 — Production-ready close-out

### Added

- **`version.hpp`** — `THREADMAXX_NAVMESH_VERSION_MAJOR/MINOR/PATCH`
  macros, packed `THREADMAXX_NAVMESH_VERSION` integer, and
  `version_string()` for runtime logging. Re-exported by the
  umbrella `threadmaxx_navmesh.hpp`.
- **`README.md`** — top-level library overview with quick start +
  doc cross-references.
- **`USER_GUIDE.md`** — user-facing documentation (public surface
  inventory, end-to-end integration recipe, perf expectations).
- **`MAINTAINER_GUIDE.md`** — internal documentation (architecture,
  wire format, solver pipeline, registry threading contract,
  hot-path allocation discipline, how to add bake validation arms
  or wire-format fields, common pitfalls).
- **`CHANGELOG.md`** — this file.

### Closed-out roadmap

`FUTURE_WORK.md` v1.0 closure section documents what's shipped vs.
deferred to v1.x candidate batches (voxelized bake, tile
streaming, RVO local avoidance, agent-radius corridor shrink,
off-mesh links, reachability queries, region/island analysis).

---

## [0.9.0] — 2026-06-10 — Batch N9 — Offline bake tool

### Added

- **`bake.hpp`** — `BakeInputTriangle { a, b, c, areaTag }`,
  `BakeInput { vertices, triangles, name, tileId }`, `BakeError`
  enum (None / EmptyInput / InvalidIndex / DegenerateTriangle /
  NonManifoldEdge / TooManyPolygons), `BakeResult { blob, error,
  diagnostic }`, `bakeNavMesh(input) -> BakeResult` pure function.
- **`src/BakeTool.cpp`** — triangle-direct bake (one input
  triangle → one output 3-vertex polygon). Validates vertex
  bounds, repeated indices, XZ-area degeneracy, ≤ 2 incident
  polys per edge, per-tile polygon cap. Edge-incidence map keyed
  by sorted `(vmin, vmax)`; emits the v2 wire format the
  registry already reads.
- **`examples/navmesh_bake/`** — `threadmaxx_navmesh_bake` CLI
  driver. Reads stdin / `--in PATH` text format (`v X Y Z`,
  `t A B C [TAG]`, `name STRING`, `tile ID`, `#` comments);
  writes blob to stdout / `--out PATH`. Guarded on
  `THREADMAXX_BUILD_EXAMPLES`.
- Three test executables: `test_navmesh_bake_smoke` (L-shape
  silhouette baked → loaded → solved end-to-end),
  `test_navmesh_bake_validation` (every BakeError mode),
  `test_navmesh_bake_areas` (area-tag pass-through).

### Deferred to v1.x

- Heightfield voxelization (Recast-style bake).
- Automatic walkability classification.
- Mesh simplification.
- Multi-tile bake (current bake emits a single tile per call;
  callers split the world if needed).

---

## [0.8.0] — 2026-06-10 — Batch N8 — Dynamic obstacle overlay

### Added

- **`obstacle.hpp`** + **`src/ObstacleOverlay.cpp`** —
  `DynamicObstacle { center, halfExtents, height, areaMask }`,
  `ObstacleOverlay::add/update/remove/isBlocked`. Atomic bucket
  swap on `update` (no transient ghost cells); ids strictly
  monotonic.
- **`detail/spatial_hash.hpp`** — header-only
  `SpatialHashXZ<KeyT, ValueT>` with floor-to-negative-infinity
  cell math.
- **`PathRequest::obstacles`** — optional `const ObstacleOverlay*`
  the solver consults during edge expansion. Skip rule:
  obstacle.areaMask & (1 << nbrPoly.areaTag) ⇒ drop the neighbor.
- Three test executables: `test_navmesh_obstacle_blocks` (wall
  forces detour), `test_navmesh_obstacle_update` (moving obstacle
  reroutes), `test_navmesh_obstacle_remove` (removed obstacle
  restores direct path).

### Deferred to v1.x

- Full local-avoidance (RVO) against dynamic obstacles.
- Segment-vs-AABB edge sweep (centroid test is the v1.0
  contract).
- 3D queries against `halfExtents.y`.

---

## [0.7.0] — 2026-06-10 — Batch N7 — Steering + corridor following

### Added

- **`steering.hpp`** — header-only `FollowPathInput`,
  `FollowPathOutput`, `followPath(in) -> out` pure function.
  Acceleration-limited model: desired velocity =
  unit-direction-to-target × `maxSpeed`, clamped to
  `|Δv| ≤ maxAcceleration × dt`. Caller persists the segment
  cursor.
- Three test executables: `test_navmesh_follow_straight`
  (straight segment + accel cap), `test_navmesh_follow_corner`
  (angular-rate bound at L-corner across 1200 ticks),
  `test_navmesh_follow_finished` (arrival radius + degenerate
  single-waypoint corridor).

### Deferred

- Local avoidance / RVO — v1.x.
- Crowd density fields — v1.x.
- Agent-radius-aware corridor shrink — v1.x.

---

## [0.6.0] — 2026-06-10 — Batch N6 — Batch path solver

### Added

- **`crowd.hpp`** — `BatchPathRequest`, `BatchPathResult`,
  `BatchPathSolverConfig { workerThreads = 1 }`,
  `BatchPathSolver` with persistent worker pool. Effective
  parallelism = `workerThreads + 1` (producer participates).
- **`detail/solver.hpp`** + **`src/Solver.cpp`** — extracted
  shared solver internals (`PolyLocation`, `SolverScratch`,
  `PreparedRequest`, `locate`, `solvePrepared`) out of N5's
  anonymous namespace so both services consume one tested A* +
  funnel pipeline.
- **`bench/navmesh_batch_bench.cpp`** — throughput on a
  256-poly mesh. Measured: 57k qps (sync), 116k (1w), 166k
  (2w), 251k (4w), 287k (8w) on the reference host.
- Two test executables: `test_navmesh_batch_correctness`
  (batch matches sync), `test_navmesh_batch_determinism`
  (1/2/4 worker counts produce byte-identical results).

---

## [0.5.0] — 2026-06-10 — Batch N5 — Async path query service

### Added

- **`PathQueryServiceConfig { workerThreads = 1 }`**,
  `wait(id, timeout)`, `pendingCount()`, `workerCount()`,
  non-movable service. `workerThreads = 0` keeps the
  synchronous mode.
- **`src/PathQueryService.cpp`** — internal worker loop with
  tombstone-aware cancel/clear semantics; per-worker
  `SolverScratch` (A* + node index + centroids + funnel buf).
- Three test executables: `test_navmesh_query_async_smoke`
  (100 requests fan out → match reference sync results),
  `test_navmesh_query_cancel` (three cancel windows),
  `test_navmesh_query_clear` (in-flight drop + tombstone wakes).
- Six N3/N4 tests migrated from immediate `tryGet` to
  `wait(id, seconds{5})`.

---

## [0.4.0] — 2026-06-10 — Batch N4 — Funnel smoothing

### Added

- **`detail/funnel.hpp`** — header-only Simple Stupid Funnel.
  `FunnelPortal {left, right}` POD, `stringPullFunnel(portals,
  out)`. CCW-positive `cross2D`; LEFT/RIGHT picked from the
  FROM-poly edge index (matches CCW-from-above polygons).
- **`PathResult::waypoints`** is now the smoothed walking path
  (pre-N4 callers that relied on edge-midpoint spacing must
  compute their own midpoints from `corridor`).
- Three test executables: `test_navmesh_funnel_straight`
  (straight corridor → `[start, goal]`), `test_navmesh_funnel_corner`
  (L-portal pinch at inner corner), `test_navmesh_funnel_pinch`
  (area-mask detour double pinch).

### Note on the v1.0 contract

The polygon `corridor` shape is unchanged from N3 — only the
`waypoints` semantics changed.

---

## [0.3.0] — 2026-06-09 — Batch N3 — Single-path A\*

### Added

- **`query.hpp`** — `PathRequest`, `PathResult` (with
  `corridor` + `partial`), `PathQueryService` (synchronous in
  N3; async in N5), `PathRequestStatus`.
- **`detail/a_star.hpp`** — `NodeIndex` tile-prefix-sum
  encode/decode, `AStarOpenSet` (binary min-heap with lazy
  decrement-skip), reusable `AStarState` scratch.
- **`src/PathQueryService.cpp`** — even-odd XZ point-in-polygon
  locate, centroid-cost edges, euclidean heuristic, area-mask
  filter, best-h fallback for partial paths.
- Four test executables: `test_navmesh_astar_simple` (happy
  path), `test_navmesh_astar_unreachable` (no portals +
  pre-solve failures), `test_navmesh_astar_partial` (best-effort
  goal fallback), `test_navmesh_astar_area_mask` (water area
  filter).

### Out of scope (deferred to N4)

- Funnel smoothing.

---

## [0.2.0] — 2026-06-09 — Batch N2 — Tile model + adjacency

### Added

- **`NavTile`** — per-tile polygon storage with adjacency table
  (`neighborPolys` parallel to `vertexIndices`,
  `kInvalidPolyIndex` for borders) + tile AABB.
- **`NavPortal`** + per-mesh `portals()` / per-tile
  `portalsForTile()` / `crossTileNeighbor()` for cross-tile
  walking.
- Wire format bumped to v2 (cross-tile portal table appended
  after the per-tile section).
- Two test executables: `test_navmesh_tile_adjacency`
  (intra-tile neighbor walk), `test_navmesh_tile_traversal`
  (cross-tile portal walk).

---

## [0.1.0] — 2026-06-09 — Batch N1 — Foundations (registry + mesh load)

### Added

- **`types.hpp`** — `NavMeshId`, `NavTileId`, `NavPolyId`,
  `PathId`, `NavAgentId`, `NavMeshRef` (generation-tagged).
- **`config.hpp`** — `kNavMeshBlobMagic`, `kNavMeshBlobVersion`,
  `kNavMeshMaxTiles`, `kNavMeshMaxPolysPerTile`,
  `kNavMeshMaxPortals`, `NavMeshConfig`.
- **`mesh.hpp`** — `NavPoly`, `NavTile`, `NavMeshMeta`,
  `NavMesh`, `NavMeshLoadError`, `NavMeshRegistry`
  (thread-safe under internal mutex).
- **`src/NavMeshRegistry.cpp`** — blob parser + slot pool with
  generation-tagged refs.
- **`threadmaxx_navmesh.hpp`** umbrella header.
- Top-level CMake target `threadmaxx::navmesh` (STATIC archive).
- Three test executables: `test_navmesh_registry_load` (happy
  path), `test_navmesh_registry_unload` (ref-staleness on
  unload), `test_navmesh_registry_invalid` (every
  `NavMeshLoadError` mode).
- Test fixture: `tests/navmesh/fixtures/blob_builder.hpp` for
  hand-built test meshes.

---

## Pre-v1: Design phase

The library was scoped in `DESIGN_NOTES.md` before any code
landed. `FUTURE_WORK.md` broke that scope into the nine N-batches
above; each batch landed as a single coherent change with its own
test gate. See `FUTURE_WORK.md` for per-batch retrospectives.
