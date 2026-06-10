# `threadmaxx_navmesh` — User Guide

Engine-agnostic navmesh + pathfinding: triangle-direct bake,
polygon-graph A\*, funnel smoothing, async + batch query
services, acceleration-clamped steering, dynamic obstacles. All
for the `threadmaxx` engine's POD model.

## When to use this library

Reach for `threadmaxx_navmesh` when you have:

- A walkable surface you can express as a triangle soup
  (hand-authored, exported from a level editor, or produced by
  your own offline pipeline).
- Tens to thousands of agents you want to route per tick.
- A movement system that consumes per-agent `desiredVelocity`.
- A renderer that draws the world — the library does not draw
  the navmesh; debug visualization is the consumer's job.

It is NOT a Recast clone (no heightfield voxelization in v1.0 —
see `FUTURE_WORK.md`), it is NOT a crowd simulator (no RVO; the
v1.0 obstacle overlay only blocks static-ish obstructions), and
it does NOT own entity state — your engine drives the registry,
services, and `followPath` once per tick.

## Quick start

```cpp
#include <threadmaxx_navmesh/threadmaxx_navmesh.hpp>

using namespace threadmaxx::navmesh;

// 1. Bake a triangle soup (one-time, offline).
std::vector<Vec3> verts = ...;
std::vector<BakeInputTriangle> tris = ...;
BakeResult baked = bakeNavMesh(BakeInput{verts, tris, "level", 0});
assert(baked.error == BakeError::None);

// 2. Load at startup.
NavMeshRegistry reg;
NavMeshRef mesh = reg.load(baked.blob);

// 3. Solve a path (any thread).
PathQueryService svc(reg);
PathRequest req{mesh, agent.pos, target.pos};
PathId id = svc.request(req);
auto res = svc.wait(id, std::chrono::seconds{2}).value();

// 4. Steer per tick (any thread).
FollowPathInput in;
in.corridor       = res.waypoints;
in.currentPosition = agent.pos;
in.currentVelocity = agent.vel;
in.dt              = dt;
FollowPathOutput out = followPath(in);
agent.vel = out.desiredVelocity;
```

## Build setup

Add the dependency:

```cmake
target_link_libraries(my_target PRIVATE threadmaxx::navmesh)
```

The CMake target carries the include directory, `cxx_std_20`, and
a transitive link to `threadmaxx::threadmaxx` (for the math PODs).
The library produces one static archive
(`libthreadmaxx_navmesh.a` on POSIX).

### Build option

The library is opt-in via:

```
cmake -B build -DTHREADMAXX_BUILD_NAVMESH=ON   # default ON
```

Setting `OFF` drops the `threadmaxx::navmesh` target.

The offline bake CLI is an `examples/navmesh_bake` target, built
when `THREADMAXX_BUILD_EXAMPLES=ON` (the default).

## Public surface inventory

All types live in namespace `threadmaxx::navmesh`. Each header is
a clean unit — include just what you need (or the umbrella
`threadmaxx_navmesh.hpp`).

### Types + handles (`types.hpp`)

| Type                 | Purpose                                          |
|----------------------|--------------------------------------------------|
| `NavMeshId`          | `uint64_t` opaque registry id (0 = invalid)      |
| `NavTileId`          | `uint32_t` per-tile id, scoped to a mesh         |
| `NavPolyId`          | `uint32_t` per-polygon id, scoped to a tile      |
| `PathId`             | `uint64_t` opaque path-request id                |
| `NavMeshRef`         | `{ id, generation }` handle; survives unrelated unloads |

### Config + wire format (`config.hpp`)

| Name                          | Purpose                                          |
|-------------------------------|--------------------------------------------------|
| `kNavMeshBlobMagic`           | 4-byte 'NVMX' magic prefix on every baked blob   |
| `kNavMeshBlobVersion`         | Current wire format version (v2)                 |
| `kNavMeshMaxTiles`            | Per-mesh tile cap (65536)                        |
| `kNavMeshMaxPolysPerTile`     | Per-tile polygon cap (65535)                     |
| `kNavMeshMaxPortals`          | Per-mesh portal cap (1 << 20)                    |
| `NavMeshConfig`               | Library-wide tolerances (currently `epsilon`)    |

### Runtime mesh + registry (`mesh.hpp`)

| Type / method                                | Purpose                                          |
|----------------------------------------------|--------------------------------------------------|
| `NavPoly`                                    | Polygon header (indexStart / indexCount / areaTag) |
| `kInvalidPolyIndex`                          | Sentinel for "no neighbor across this edge"      |
| `NavPortal`                                  | Cross-tile portal: `(tileA,polyA,edgeA)↔(tileB,polyB,edgeB)` |
| `NavTile`                                    | Per-tile geometry + adjacency + AABB             |
| `NavMeshMeta`                                | `{ name, tileCount, polygonCount, vertexCount }` |
| `NavMesh::tiles()`                           | `span<const NavTile>` — load-order matches index |
| `NavMesh::findTile(id)`                      | Pointer to tile or `nullptr`                     |
| `NavMesh::portals()` / `portalsForTile(id)`  | Flat portal span + per-tile portal index list    |
| `NavMesh::crossTileNeighbor(t, p, e)`        | Optional cross-tile neighbor through an edge     |
| `NavMeshLoadError` enum                      | Why a load failed                                |
| `NavMeshRegistry::load(span<byte>)`          | Parse blob → `NavMeshRef`                        |
| `NavMeshRegistry::unload(ref)`               | Release; ref slots recycle with fresh generation |
| `NavMeshRegistry::isValid(ref)`              | Did `ref` survive a matching unload?             |
| `NavMeshRegistry::meta(ref)`                 | `optional<NavMeshMeta>` snapshot                 |
| `NavMeshRegistry::find(ref)`                 | `const NavMesh*` or `nullptr`                    |
| `NavMeshRegistry::lastLoadError()`           | Diagnostic for the most recent load              |

Thread-safe across `load` / `unload` / `find` / `meta` / `isValid`
under an internal mutex. Read accessors on the borrowed
`NavMesh*` are safe to dereference for as long as the matching
`NavMeshRef` stays valid.

### Path query service (`query.hpp`)

| Type / method                                    | Purpose                                          |
|--------------------------------------------------|--------------------------------------------------|
| `PathRequest`                                    | `{ mesh, start, goal, agentRadius, agentHeight, areaMask, allowPartial, obstacles }` |
| `PathResult`                                     | `{ id, ready, success, partial, waypoints, corridor, cost }` |
| `PathResult::CorridorEntry`                      | `{ tileId, polyId }` per corridor step           |
| `PathRequestStatus` enum                         | Why a `request` failed pre-solve                 |
| `PathQueryServiceConfig { workerThreads = 1 }`   | `0` = synchronous mode                           |
| `PathQueryService::request(req) -> PathId`       | Validate then enqueue (or run inline if `workerThreads == 0`) |
| `PathQueryService::tryGet(id)`                   | `optional<PathResult>` — nullopt while queued    |
| `PathQueryService::wait(id, timeout)`            | Block until ready / cancelled / timeout          |
| `PathQueryService::cancel(id)`                   | Drop pending / mid-solve / stored result         |
| `PathQueryService::clear()`                      | Drop every stored + every queued request         |
| `pendingCount` / `storedCount` / `workerCount`   | Diagnostics                                      |
| `PathQueryService::lastRequestStatus()`          | Per-thread last-request status                   |

### Batch path solver (`crowd.hpp`)

| Type / method                                    | Purpose                                          |
|--------------------------------------------------|--------------------------------------------------|
| `BatchPathRequest { vector<PathRequest> requests }` | One batch worth of independent requests       |
| `BatchPathResult { vector<PathResult> results }` | 1:1 result list, index-aligned to `requests`     |
| `BatchPathSolverConfig { workerThreads = 1 }`    | `0` = inline on caller thread                    |
| `BatchPathSolver::solve(batch)`                  | Blocks until every entry has a result            |
| `BatchPathSolver::workerCount()`                 | Configured worker count                          |

Effective parallelism is `workerThreads + 1` — the producer
thread participates. Concurrent `solve()` calls on the same
instance are undefined behavior.

### Steering (`steering.hpp`)

| Type / function                          | Purpose                                          |
|------------------------------------------|--------------------------------------------------|
| `FollowPathInput`                        | `{ corridor (span), pos, vel, maxSpeed, maxAccel, arrivalRadius, dt, segmentIndex }` |
| `FollowPathOutput`                       | `{ desiredVelocity, nextTarget, segmentIndex, finished }` |
| `followPath(in) -> out`                  | Pure, allocation-free, safe from any thread      |

Acceleration-limited model: desired = unit-direction × `maxSpeed`,
clamped to `|Δv| ≤ maxAccel × dt`. Planar (XZ); the y component
on input is ignored, output y is forced to 0. Caller persists
`FollowPathOutput::segmentIndex` back into the next call.

### Dynamic obstacles (`obstacle.hpp`)

| Type / method                            | Purpose                                          |
|------------------------------------------|--------------------------------------------------|
| `DynamicObstacle`                        | `{ center, halfExtents, height, areaMask }` AABB blocker |
| `ObstacleId`                             | `uint64_t` (monotonic, never reused)             |
| `ObstacleOverlayConfig { cellSize=1.0f }`| Spatial-hash grid resolution                     |
| `ObstacleOverlay::add(obstacle)`         | Insert → fresh non-zero id                       |
| `ObstacleOverlay::update(id, obstacle)`  | Atomic bucket swap (no ghost cells)              |
| `ObstacleOverlay::remove(id)`            | Drop; subsequent `isBlocked` ignores it          |
| `ObstacleOverlay::isBlocked(xz, mask)`   | True if any obstacle's `areaMask & mask` covers `xz` |
| `ObstacleOverlay::obstacleCount`         | Live obstacle count                              |

Single-threaded mutation contract; safe to query from any thread
as long as no concurrent mutation is in flight. The typical
recipe: update during `preStep`, freeze for the rest of the tick.

### Offline bake (`bake.hpp`)

| Type / function                          | Purpose                                          |
|------------------------------------------|--------------------------------------------------|
| `BakeInputTriangle { a, b, c, areaTag }` | One input triangle                               |
| `BakeInput { vertices, triangles, name, tileId }` | One tile worth of input                  |
| `BakeError` enum                         | None / EmptyInput / InvalidIndex / DegenerateTriangle / NonManifoldEdge / TooManyPolygons |
| `BakeResult { blob, error, diagnostic }` | Bake output                                      |
| `bakeNavMesh(input) -> BakeResult`       | Pure; allocates only inside the returned blob    |

One input triangle → one output 3-vertex polygon. Adjacency is
derived from shared edges; open edges become walkable boundaries
(stamped `kInvalidPolyIndex` in the neighbor table).

### CLI bake driver (`examples/navmesh_bake`)

Minimal text-format reader around `bakeNavMesh()`:

```
threadmaxx_navmesh_bake [--in PATH] [--out PATH] [--name NAME] [--tile ID]
```

Input format (one directive per line):

```
v X Y Z          # vertex
t A B C [TAG]    # triangle (0-based vertex ids, optional area tag)
name STRING      # asset name
tile ID          # tile id stamped on the output
# comment        # ignored
```

Writes the v2 blob to stdout (or `--out PATH`); diagnostics +
counts go to stderr.

## End-to-end integration

The standard recipe wires the four phases through an engine
system. The recommended ordering is `preStep` → `update` →
`postStep`:

- **Setup**: load a navmesh into the registry (`reg.load(blob)`),
  create one `PathQueryService` (or one `BatchPathSolver` for
  crowd-style fanouts), create one `ObstacleOverlay`.
- **`preStep`**: update the obstacle overlay from game state
  (spawn / move / despawn blockers).
- **`update`**: submit `PathRequest`s as agents need re-routing.
  Drain finished requests with `tryGet(id)` and stash the
  resulting `PathResult` on the agent.
- **`postStep`**: call `followPath(...)` once per agent with the
  stashed corridor + current state → apply the
  `desiredVelocity` through your movement system.

Path requests are independent. A typical mix: per-agent requests
flow through `PathQueryService`; a batched re-routing pass (e.g.
"everyone re-path after the player moved") goes through
`BatchPathSolver` to amortize the synchronization cost.

## Conventions

### Allocation policy

- **Per-tick (steady state)**: `followPath` allocates nothing.
  `PathQueryService::request` allocates only when the internal
  queue grows past its capacity high-water mark; reuse across
  thousands of ticks reaches a steady state.
  `PathResult::waypoints` / `corridor` are owned by the result —
  callers should move-construct them into agent state rather than
  copy.
- **Setup**: `NavMeshRegistry::load`, `PathQueryService` ctor,
  `BatchPathSolver` ctor, `ObstacleOverlay::add` all allocate.
- **Bake**: `bakeNavMesh` allocates only inside the returned
  `BakeResult`. Pure function — safe to call concurrently from
  any thread.

### Threading

- `NavMeshRegistry` — internal mutex; `load` / `unload` from any
  thread, `find` / `meta` / `isValid` from any thread. Don't
  unload a mesh while a path query against it is in flight.
- `PathQueryService` — `request` / `tryGet` / `wait` / `cancel` /
  `clear` are safe from any thread. The service owns one or more
  worker threads (`workerThreads = 0` for synchronous mode).
- `BatchPathSolver` — `solve()` is single-producer (concurrent
  calls on the same instance are undefined). The persistent
  worker pool stays alive between calls.
- `ObstacleOverlay` — single-threaded mutation contract. Query
  (`isBlocked`) is safe from any thread once mutation has
  quiesced.
- `followPath` — pure function, safe from any thread.
- `bakeNavMesh` — pure function, safe from any thread.

### Empty inputs

- Empty corridor in `followPath` → `finished = true`, zero
  output velocity.
- Empty batch in `BatchPathSolver::solve` → empty result.
- Empty triangles or empty vertices in `bakeNavMesh` → returns
  `BakeError::EmptyInput`.

### Coordinate convention

- World transforms are 3D (`Vec3`).
- Navigation is XZ-planar throughout. The y axis is ignored by
  steering, locate, area-mask filtering, and the obstacle overlay
  (3D queries are a v1.x candidate).

## Performance expectations

Measured on a recent x86_64 desktop with `-O3`, single-thread
sync + multi-worker batch modes, from
`bench/navmesh_batch_bench` on a 256-poly mesh:

| Mode                            | Throughput |
|---------------------------------|-----------:|
| Synchronous (`workerThreads=0`) | 57k qps    |
| 1 worker                        | 116k qps   |
| 2 workers                       | 166k qps   |
| 4 workers                       | 251k qps   |
| 8 workers                       | 287k qps   |

Well above the v1.0 close-out gate of ≥10k qps @ 4 workers.
`--grid=N` / `--requests=N` flags let the bench scale further.

### When to expect a perf cliff

- **Mesh size > 10k polygons** — A* heap operations dominate.
  Tile streaming (v1.x) is the right knob.
- **Heavy obstacle churn** — `ObstacleOverlay::update` rebuilds
  spatial-hash buckets for the obstacle; thousands of moving
  obstacles per tick is the cliff. Use static obstacles where
  possible.
- **Per-request large `agentRadius`** — reserved for v1.x; the
  v1.0 solver ignores `agentRadius` (corridor-shrink is a v1.x
  candidate).

## Restrictions / non-goals

Per `DESIGN_NOTES.md` §1, the library does NOT:

- Voxelize heightfields or classify walkability automatically
  (Recast-style bake is a v1.x candidate).
- Resolve collisions or simulate physics.
- Animate agents or own animation state.
- Render the navmesh (debug viz is consumer-side).
- Replicate state over a network.
- Provide editor UI.
- Stream tiles at runtime (currently the whole mesh loads at
  once; streaming is a v1.x candidate).

If you need any of the above, build it as a separate sibling
library or in your game-side code.

## Library version

```cpp
#include <threadmaxx_navmesh/version.hpp>

// Compile-time:
static_assert(THREADMAXX_NAVMESH_VERSION_MAJOR == 1);
#if THREADMAXX_NAVMESH_VERSION >= 10100  // require ≥ 1.1.0
   // ...
#endif

// Runtime:
std::printf("threadmaxx_navmesh v%s\n",
            threadmaxx::navmesh::version_string());
```

Version bumps follow [semver](https://semver.org/). See
`CHANGELOG.md` for the release history and `MAINTAINER_GUIDE.md`
for the full lifecycle policy.

## See also

- `README.md` — top-level overview.
- `DESIGN_NOTES.md` — the original spec.
- `FUTURE_WORK.md` — batch-by-batch landed work + v1.x candidates.
- `CHANGELOG.md` — per-release notes.
- `MAINTAINER_GUIDE.md` — solver internals, wire format, how to
  add a new bake validation arm or query helper.
- `tests/navmesh/*.cpp` — example usage of every public API.
- `bench/navmesh_batch_bench.cpp` — batch-solver perf harness.
