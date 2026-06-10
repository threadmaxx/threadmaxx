# `threadmaxx_navmesh`

Engine-agnostic navmesh + pathfinding library for the `threadmaxx`
engine. **Status**: v1.0.0 — production-ready.

## What

A polygon-graph pathfinding stack that turns a baked navmesh + a
`(start, goal)` pair into a smoothed walkable corridor, then lets
the agent follow it. Designed to plug into the engine's
`forEachChunk`-style iteration without owning entities or
component storage.

The library covers nine pillars:

- **Registry** — `NavMeshRegistry` loads / unloads baked blobs,
  hands back generation-tagged `NavMeshRef` handles.
- **Tile + adjacency model** — per-tile polygon storage, intra-
  tile neighbor table, cross-tile portals.
- **A\* path search** — polygon-graph A* with area-mask filtering
  + best-effort partial paths when the goal is unreachable.
- **Funnel smoothing** — Simple Stupid Funnel turns the polygon
  corridor into a tight waypoint list.
- **Async query service** — `PathQueryService` enqueues onto an
  internal worker thread; `tryGet` / `wait` / `cancel` / `clear`
  are safe from any thread. `workerThreads = 0` keeps the
  synchronous mode for tests.
- **Batch solver** — `BatchPathSolver::solve` fans a list of
  requests across a persistent worker pool; the producer
  participates for `workerThreads + 1` effective parallelism.
- **Steering** — header-only `followPath` turns a waypoint list
  + agent state into an acceleration-clamped `desiredVelocity`.
- **Dynamic obstacles** — `ObstacleOverlay` lets game code drop
  AABB blockers without rebaking; the solver consults the overlay
  during edge expansion.
- **Offline bake** — `bakeNavMesh()` consumes a triangle soup +
  per-triangle area tags and emits a v2 blob the registry loads
  verbatim. CLI driver `threadmaxx_navmesh_bake` is the same code
  wrapped behind a text-format reader.

The library is engine-agnostic at link time — it pulls in
`threadmaxx::threadmaxx` only for the math PODs (`Vec3`). It does
NOT own entities, components, or simulation time; the host engine
drives the registry / services / `followPath` once per tick and
pipes the result into its movement system.

## Quick start

```cmake
target_link_libraries(my_target PRIVATE threadmaxx::navmesh)
```

```cpp
#include <threadmaxx_navmesh/threadmaxx_navmesh.hpp>

using namespace threadmaxx::navmesh;

// 1. Bake a triangle soup → blob.
std::vector<Vec3> verts = ...;
std::vector<BakeInputTriangle> tris = ...;
BakeInput bake{verts, tris, "level_01", /*tileId=*/0};
BakeResult baked = bakeNavMesh(bake);
assert(baked.error == BakeError::None);

// 2. Load into the registry.
NavMeshRegistry reg;
NavMeshRef mesh = reg.load(baked.blob);
assert(mesh);

// 3. Solve a path.
PathQueryService svc(reg);
PathRequest req;
req.mesh   = mesh;
req.start  = Vec3{ 0.5f, 0, 0.5f};
req.goal   = Vec3{14.5f, 0, 14.5f};
PathId id = svc.request(req);
auto res = svc.wait(id, std::chrono::seconds{5}).value();

// 4. Steer the agent per tick.
FollowPathInput in;
in.corridor       = res.waypoints;
in.currentPosition = agent.pos;
in.currentVelocity = agent.vel;
in.dt              = 1.0f / 60.0f;
FollowPathOutput out = followPath(in);
agent.vel = out.desiredVelocity;
```

## Documentation

| Document | Audience | Purpose |
|----------|----------|---------|
| [`README.md`](README.md) | Everyone | Top-level overview (this file) |
| [`USER_GUIDE.md`](USER_GUIDE.md) | Consumers | Public surface inventory, integration patterns, perf expectations |
| [`MAINTAINER_GUIDE.md`](MAINTAINER_GUIDE.md) | Library devs | Architecture, solver internals, how to add bake validation arms or wire-format fields |
| [`DESIGN_NOTES.md`](DESIGN_NOTES.md) | Anyone | Original spec (frozen reference) |
| [`FUTURE_WORK.md`](FUTURE_WORK.md) | Library devs | Batch-by-batch landed work + v1.x candidates |
| [`CHANGELOG.md`](CHANGELOG.md) | Everyone | Per-release notes |

## Scope

- ✓ Triangle-direct bake (one input triangle → one output polygon).
- ✓ Registry + load/unload of a v2 wire-format blob.
- ✓ Polygon-graph A* with area-mask filter + best-effort partial.
- ✓ Funnel smoothing of the polygon corridor.
- ✓ Async path queries (single-result `wait`, batched `solve`).
- ✓ Steering (`followPath` acceleration-clamped pure function).
- ✓ Dynamic obstacle overlay (centroid-blocking, area-mask aware).
- ✓ Engine-agnostic API; the engine pulls in this library, not
  the other way around.

Out of scope (per `DESIGN_NOTES.md` §1): rigid body / collision
physics, collision resolution authority, animation IK, rendering,
matchmaking / networking, editor UI, ECS storage ownership,
pathfinding baked into engine core. Voxelization (Recast-style
bake), tile streaming, local-avoidance RVO, agent-radius-aware
corridor shrink, off-mesh links, reachability queries, region /
island analysis are all v1.x candidates — see `FUTURE_WORK.md`.

## Status: production-ready

- 26 dedicated test executables registered with CTest (100%
  passing on `build/` and `build-werror/` trees).
- Batch path solver benchmark (`bench/navmesh_batch_bench.cpp`)
  on a 256-poly mesh @ 4 workers measured at 251k qps on the
  reference host — 25× the v1.0 close-out gate of ≥10k qps.
- All nine N-batches (N1–N9) landed and reviewed — see
  `FUTURE_WORK.md` for the per-batch retrospectives.
- Versioning policy documented (semver, lifecycle in
  `MAINTAINER_GUIDE.md`).

See [`FUTURE_WORK.md`](FUTURE_WORK.md) for v1.x candidate work
(voxelized bake, tile streaming, RVO, agent-radius corridors,
off-mesh links, reachability queries, region/island analysis —
none of which blocks v1.0 production use).

## License

Same as the parent `threadmaxx` project.
