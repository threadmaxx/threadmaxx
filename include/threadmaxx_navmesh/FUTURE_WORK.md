# `threadmaxx_navmesh` — batch plan

Sibling-library implementation plan. `DESIGN_NOTES.md` is the
authoritative spec; this doc breaks it down into shippable
test-driven batches.

Status: **N4 shipped (2026-06-10)** — Simple Stupid Funnel smoothing
on top of the N3 corridor. `PathResult::waypoints` is now the smoothed
walking path (`[start, ..., end]`) rather than the polygon-edge
midpoint zig-zag; the polygon corridor still rides in `corridor`.
Green on `build/` and `build-werror/` (12/12 navmesh tests). N5 → N9
remain 📋 planned. Sequencing follows the §10 "implementation order"
of the design notes, regrouped into shippable units that each carry
their own tests.

## Conventions

Each batch is independently shippable:

- **Goal** — what the batch accomplishes in one sentence.
- **Test gate** — assertions that prove the batch landed.
- **Files** — what's added / modified.
- **Risks** — what could break.
- **Out of scope** — explicitly deferred to a later batch.

Batches start red, go green, then refactor. The library produces a
static library `threadmaxx::navmesh` plus public headers under
`include/threadmaxx_navmesh/`. The runtime is header-light but
A* / funnel / bake have enough state to warrant `.cpp` files.

Query throughput is the perf invariant: batch path solving must
amortize down to roughly one A* per worker job at scale.

## Library structure (target end-state)

```
include/threadmaxx_navmesh/
  threadmaxx_navmesh.hpp   # umbrella
  config.hpp               # bake + runtime tolerances
  types.hpp                # NavMeshId / NavTileId / NavPolyId / PathId
  mesh.hpp                 # runtime navmesh model
  bake.hpp                 # bake inputs + outputs
  query.hpp                # PathRequest / PathResult / PathQueryService
  agent.hpp                # NavAgent state, AgentStatus
  steering.hpp             # FollowPathInput/Output
  crowd.hpp                # BatchPathSolver
  obstacle.hpp             # DynamicObstacle + overlay
  serialization.hpp        # save/load for baked nav assets
  diagnostics.hpp          # debug draw, validation reports
  detail/
    bitset.hpp
    funnel.hpp
    a_star.hpp
    spatial_hash.hpp
    ring_buffer.hpp
src/threadmaxx_navmesh/
  NavMeshRegistry.cpp
  PathQueryService.cpp
  BatchPathSolver.cpp
  ObstacleOverlay.cpp
  BakeTool.cpp             # optional / linked into a separate exe target
tests/navmesh/
  test_navmesh_*.cpp
bench/
  navmesh_*.cpp
```

## Batch N1 — Foundations (registry + mesh load) — ✅ shipped 2026-06-09

**Goal**: load a pre-baked navmesh blob into the registry,
hand back a `NavMeshRef`. No queries yet — just the data model.

**Test gate**:

- `test_navmesh_registry_load` — load a known-good 16-polygon
  flat-square test mesh; `meta()` returns expected polygon/tile
  counts; `isValid` returns true for the returned ref.
- `test_navmesh_registry_unload` — `unload` invalidates the ref;
  reload allocates a fresh generation.
- `test_navmesh_registry_invalid` — load a corrupted blob (wrong
  magic, truncated tile data) returns an invalid ref with a clear
  diagnostic.

**Files**: `types.hpp`, `mesh.hpp`, `config.hpp`, umbrella header,
`src/NavMeshRegistry.cpp`, three tests + a baked-mesh fixture
generator helper (lives under `tests/navmesh/fixtures/`).

**Risks**: locking in the binary format too early. Recommendation:
ship a v1 format with `magic + version + tile count + per-tile
{header, vertices, polygons, adjacency}`, and reject any other
version on load — same discipline as the engine's WorldSnapshot.

**Out of scope**: A* (N3), tile-based streaming (deferred to v1.x).

## Batch N2 — Tile model + adjacency — ✅ shipped 2026-06-09

**Goal**: per-tile polygon storage with adjacency + portal edges +
area cost tags. Internal — exposed only through `NavMesh` const
accessors. The fixture mesh from N1 grows from a single square to
4 tiles arranged in an L-shape.

**Test gate** (delivered):

- `test_navmesh_tile_adjacency` — every interior edge has a
  reciprocal portal; every L-shape boundary edge resolves to neither
  intra-tile neighbor nor cross-tile portal; `portalsForTile` counts
  per-tile portal incidence.
- `test_navmesh_tile_traversal` — a hand-written BFS walker seeded
  from every `(tile, poly)` in the L-shape reaches all 16 polygons
  across 4 tiles.

**Shipped**:

- `NavPortal` POD + `NavMesh::portals()` / `tileIndex()` /
  `portalsForTile()` / `crossTileNeighbor()` accessors.
- `NavMeshLoadError::{InvalidPortalCount, InvalidPortal}` diagnostics.
- v2 wire format (appends `[portalCount u32][NavPortal * N]`); v1
  blobs no longer load (rejected as UnsupportedVersion).
- `kNavMeshMaxPortals = 1 << 20` cap.
- `detail/bitset.hpp` (walker scratch storage; ready for N3 A*).
- `tests/navmesh/fixtures/blob_builder.hpp::make4TileLShape` —
  canonical L-shape with 6 portals.

**Out of scope**: streaming load (v1.x), tile-level invalidation
during edits (v1.x).

## Batch N3 — Single-path A* over polygon adjacency — ✅ shipped 2026-06-09

**Goal**: synchronous path query. `request()` returns a `PathId`,
`tryGet()` returns a ready `PathResult` on the same tick (because the
solve is synchronous in v1.0).

**Test gate** (delivered):

- `test_navmesh_astar_simple` — request a path across the L-shape
  fixture from T0 corner to T3 corner; corridor size = 7 polys, cost
  = 6.0, waypoints `[start, edge-midpoints×6, goal]`. Same-poly query
  degenerates to cost 0 + waypoints `[start, goal]`. `cancel` / `clear`
  / `tryGet(unknown)` round-trip correctly.
- `test_navmesh_astar_unreachable` — two-islands fixture, no portals;
  `allowPartial == false` → `success == false`, empty corridor /
  waypoints. Off-mesh start / goal + invalid ref each fail pre-solve
  via `lastRequestStatus()`.
- `test_navmesh_astar_partial` — same fixture with `allowPartial ==
  true`: success=true, partial=true, corridor ends at the in-T0
  polygon with smallest heuristic to the goal (T0:1, the +x neighbor).
  Last waypoint anchors on that poly's centroid.
- `test_navmesh_astar_area_mask` — 3×2 strip fixture with poly 1
  tagged area 1 (water). All-mask: corridor `[0,1,2]` cost 2.0.
  Mask `~(1<<1)`: corridor `[0,3,4,5,2]` cost 4.0.

**Shipped**:

- `query.hpp` — `PathRequest` / `PathResult` (with `corridor` +
  `partial`) / `PathQueryService` / `PathRequestStatus`.
- `detail/a_star.hpp` — `NodeIndex` (tile-prefix-sum encode/decode),
  `AStarOpenSet` (binary min-heap with lazy-decrement skip), reusable
  `AStarState` scratch.
- `src/PathQueryService.cpp` — synchronous solver: even-odd XZ
  point-in-polygon locate, centroid-cost edges, euclidean heuristic,
  area-mask filter, best-h fallback for partial, edge-midpoint
  waypoint reconstruction (handles intra-tile + cross-tile portals).
- Fixtures: `makeTwoDisconnectedTiles` (no portals — unreachable +
  partial) and `makeAreaMaskStrip` (3×2 grid, poly 1 = water tag 1).
- `Vec3` heuristic + cost are reused from the engine's `Components.hpp`
  so the library still has zero new math dependencies.

**Out of scope**: funnel smoothing (N4), async queries (N5).

## Batch N4 — Funnel smoothing — ✅ shipped 2026-06-10

**Goal**: convert the polygon-corridor output of A* into a
waypoint list via the Simple Stupid Funnel algorithm. Drops the
zig-zag that polygon-center routes produce.

**Test gate** (delivered):

- `test_navmesh_funnel_straight` — straight-line corridor across
  the 16-poly flat square (4 polys, cost 3.0) smooths to `[start,
  goal]`.
- `test_navmesh_funnel_corner` — hand-built L-shaped portal
  sequence drives `stringPullFunnel` directly. Asymmetric goal
  forces a pinch at the inner corner; expected output
  `[start, (1, 0, 1), goal]`. Also covers a straight 2-portal
  subcase. Direct unit test bypasses A* so the assertion is
  independent of tiebreaker.
- `test_navmesh_funnel_pinch` — area-mask strip with water
  masked: corridor `[0, 3, 4, 5, 2]` (cost 4.0) smooths to
  `[start, (1, 0, 1), (2, 0, 1), goal]` — two corner pinches
  bracketing the bypass.

**Shipped**:

- `detail/funnel.hpp` — header-only Simple Stupid Funnel
  implementation. `FunnelPortal {left, right}` POD,
  `cross2D` / `vequalXZ` helpers, `stringPullFunnel(portals, out)`
  entry point. Uses standard CCW-positive cross product
  (`(b.x-a.x)*(c.z-a.z) - (c.x-a.x)*(b.z-a.z)`); portal
  convention `LEFT = v[(e+1) % n], RIGHT = v[e]` matches our
  CCW-from-above polygons. Always emits `[start, end]` (the
  `size() == 1` guard handles same-poly degeneracy) and dedups a
  pinch-at-goal corner so the final waypoint is never duplicated.
- `src/PathQueryService.cpp` — `request()` now builds the portal
  sequence from the A* corridor (start/end portals book-ending
  inter-poly portals, each derived via `findEdgeTo` + the FROM
  poly's vertex order) and runs `stringPullFunnel` into
  `result.waypoints`. The polygon `corridor` shape is unchanged
  from N3.
- `Impl::portalBuf` — reused scratch vector, preserves capacity
  across solves.

**Note on the v1.0 contract**: `waypoints` is now the smoothed
walking path. Pre-N4 callers that relied on the edge-midpoint
spacing of v0.x must compute their own midpoints from `corridor`.
The existing N3 tests were updated in the same batch: the
L-shape "simple" test now asserts the loose contract
`2 ≤ waypoints.size() ≤ corridor.size() + 1` since funnel output
depends on which equal-cost corridor A* picked.

**Files**: `include/threadmaxx_navmesh/detail/funnel.hpp` (new),
`src/threadmaxx_navmesh/PathQueryService.cpp` (modified),
`tests/navmesh/test_navmesh_funnel_*.cpp` (3 new), three N3
tests updated for the smoothed-waypoints contract,
`include/threadmaxx_navmesh/threadmaxx_navmesh.hpp` unchanged
(the umbrella already pulled `query.hpp`).

**Out of scope**: agent-radius-aware path corridor shrinking (N7).

## Batch N5 — Async path query service

**Goal**: convert the synchronous N3 service to a worker-thread
queue. `request()` enqueues; the service drains on its own thread
or via an engine-job submit; `tryGet()` returns `nullopt` until
ready.

**Test gate**:

- `test_navmesh_query_async_smoke` — submit 100 requests over 10
  ticks; all complete with the same paths the synchronous N3 path
  would have produced.
- `test_navmesh_query_cancel` — cancel a pending request before
  it solves; `tryGet()` reflects cancellation; the solver doesn't
  crash.
- `test_navmesh_query_clear` — `clear()` drops all pending /
  ready entries; subsequent `tryGet()` returns `nullopt`.

**Files**: extension to `src/PathQueryService.cpp`, optional
`detail/ring_buffer.hpp` for the request queue.

**Risks**: the engine integration choice — does the service spawn
its own thread pool, or borrow from the engine's `JobSystem`?
Recommendation: borrow. Cleaner ownership, same worker accounting.

**Out of scope**: batch solver (N6).

## Batch N6 — Batch path solver

**Goal**: `BatchPathSolver::solve(BatchPathRequest)` for "100 NPCs
all asking at once" cases. Internally parallelizes via the same
worker pool as N5.

**Test gate**:

- `test_navmesh_batch_correctness` — 64 (start, goal) pairs solved
  via batch; each result matches the equivalent single-path query.
- `test_navmesh_batch_determinism` — same input batch → same output
  order across 2 runs.
- `bench/navmesh_batch_bench.cpp` — 1k requests on a 256-polygon
  mesh; report queries / second.

**Files**: `crowd.hpp`, `src/BatchPathSolver.cpp`, bench source.

**Out of scope**: agent steering (N7).

## Batch N7 — Steering + corridor following

**Goal**: `followPath` turns a waypoint list + agent state into a
`desiredVelocity`. Used by game-side movement to actually
navigate the path.

**Test gate**:

- `test_navmesh_follow_straight` — agent following a straight
  corridor produces velocity along the corridor direction at
  `maxSpeed`.
- `test_navmesh_follow_corner` — corridor with a 90° turn produces
  velocity that smoothly transitions; the test asserts the
  velocity-direction first derivative magnitude stays under a
  configured limit.
- `test_navmesh_follow_finished` — agent within `arrivalRadius` of
  the final waypoint reports `finished == true`.

**Files**: `steering.hpp`. Header-only.

**Out of scope**: local avoidance / RVO (v1.x), crowd density
fields (v1.x).

## Batch N8 — Dynamic obstacle overlay

**Goal**: temporary blockers without rebaking. Obstacles get added
to a spatial-hash overlay; A* consults the overlay during edge
expansion and skips edges crossing into blocked cells.

**Test gate**:

- `test_navmesh_obstacle_blocks` — add an obstacle that bisects
  the L-shape; the path now routes around it.
- `test_navmesh_obstacle_update` — moving an obstacle invalidates
  the affected cells; subsequent queries pick up the new state.
- `test_navmesh_obstacle_remove` — removing the obstacle restores
  the original path.

**Files**: `obstacle.hpp`, `src/ObstacleOverlay.cpp`,
`detail/spatial_hash.hpp`.

**Risks**: the obstacle-grid resolution. Too coarse and small
obstacles don't block anything; too fine and the overlay update
cost dominates. Ship a configurable cell size; default to
`max(agentRadius, tileSize/8)`.

**Out of scope**: full local-avoidance steering against dynamic
obstacles (v1.x).

## Batch N9 — Bake tool

**Goal**: an offline executable that consumes triangle mesh input
+ bake parameters and produces the v1 baked-blob format. Lives in
its own CMake target (`threadmaxx_navmesh_bake`), not linked into
the runtime library.

**Test gate**:

- `test_navmesh_bake_smoke` — bake a known-good triangle soup
  (10 quads forming the L-shape) and validate the runtime can
  load the result.
- `test_navmesh_bake_validation` — bake input with non-manifold
  geometry returns a validation error, not a runtime crash.
- `test_navmesh_bake_areas` — bake input with per-triangle area
  tags preserves them in the baked output; A* sees them.

**Files**: `bake.hpp`, `src/BakeTool.cpp`,
`examples/navmesh_bake/main.cpp` (the executable).

**Risks**: voxel-based bake (Recast-style) is the right algorithm
but a big implementation effort. Recommendation for v1.0: ship a
**triangle-direct** bake that assumes input geometry is already
planar and walkable, with explicit per-triangle area tags. Real
Recast-style voxelization is a v1.x candidate.

**Out of scope**: heightfield voxelization, automatic walkability
classification, mesh simplification (all v1.x).

## v1.0 close-out criteria

- ✓ Every batch N1–N9 landed and tested.
- ✓ End-to-end: bake a hand-authored level → load → solve paths
  for 100 agents → follow paths to completion.
- ✓ Bench `navmesh_batch_bench.cpp` reports ≥10k path queries / sec
  on a 256-poly mesh, 4 workers.
- ✓ Docs: README, USER_GUIDE, MAINTAINER_GUIDE.
- ✓ ctest 100% on `build/` and `build-werror/`.
- ✓ Version stamped at 1.0.0 in
  `include/threadmaxx_navmesh/version.hpp`.

## v1.x candidate batches (not blocking v1.0)

### v1.x — Voxelized bake (Recast-style)

Heightfield voxelization → walkable-surface classification →
polygon extraction. The "real" bake. Standard algorithm,
multi-page implementation.

### v1.x — Tile streaming

Load/unload individual tiles at runtime. Required for large worlds
(>1km²). Depends on the registry handling partial mesh state
gracefully.

### v1.x — Local avoidance (RVO)

Reciprocal Velocity Obstacles for crowd-density agent-vs-agent
avoidance. Distinct from the N8 dynamic-obstacle overlay (which
handles static-ish blockers, not other agents).

### v1.x — Path corridor with agent radius

Currently the funnel walks polygon edges. With agent radius, the
funnel should pull in by the radius — gives more realistic paths
that don't clip walls. Bolt on after a real game's pathing reveals
the visual problem.

### v1.x — Off-mesh links

Jumps, teleporters, ladders. The bake format already reserves a
slot for them; runtime A* needs to traverse them.

### v1.x — Connectivity / reachability queries

`reachable(start, goal)` (cheaper than full path solve, just a
DFS over portal adjacency). Useful for AI "can I path to player?"
checks that don't need the actual route.

### v1.x — Region / island analysis

Disconnected-component analysis baked into the mesh metadata so
runtime reachability is a constant-time mask check.

## Out of scope for the whole library

Per DESIGN_NOTES §1 — none of this lands at any batch:

- Rigid body / collision physics
- Collision resolution authority
- Animation IK
- Rendering
- Matchmaking / networking
- Editor UI
- ECS storage ownership
- Pathfinding baked into engine core
