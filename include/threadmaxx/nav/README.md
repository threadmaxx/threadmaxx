# `nav` — navmesh and pathfinding sibling library

## 1. Purpose

`nav` provides navigation data structures and path queries for games built on `threadmaxx`.

It is for:

* navmesh baking and loading,
* path queries over static or streamed navigation data,
* agent steering,
* corridor following and path smoothing,
* avoidance against dynamic obstacles,
* crowd-level local movement,
* query scheduling across worker threads.

It is **not** for:

* physics simulation,
* rigid body collision,
* animation blending,
* rendering,
* owning ECS storage,
* replacing the engine’s job system,
* changing the engine’s component model.

That fits the roadmap boundary exactly: navigation belongs in a sibling library, while the engine only needs to expose scheduling and the `NavAgentRef` integration point. 

## 2. Design principles

1. **Above the engine.** No core engine changes required beyond the `NavAgentRef` hook and access to scheduling.
2. **Span-first API.** Queries operate on contiguous ranges and chunk spans.
3. **Asynchronous by default.** Path requests can run on worker threads.
4. **Deterministic when needed.** Same inputs, same navmesh version, same path result.
5. **No ownership of movement.** The library computes routes; game code applies them.
6. **No physics solver dependency.** Dynamic obstacles are inputs, not world authority.
7. **Static and streamed worlds both supported.**
8. **Small, explicit public surface.**
9. **Header-first runtime.** Runtime queries should be lightweight and easy to include.
10. **Optional bake tool.** Offline baking can live in a separate tool target.

## 3. Package layout

```text id="p8v1xq"
include/threadmaxx/nav/
  nav.hpp               // umbrella include
  config.hpp            // feature flags, tolerances, bake options
  types.hpp             // NavMeshId, NavTileId, NavAgentId, PathId
  mesh.hpp              // loaded runtime navmesh representation
  bake.hpp              // bake inputs, bake outputs, validation
  query.hpp             // path, reachability, raycast-on-navmesh
  agent.hpp             // NavAgent state and motion intent
  steering.hpp          // corridor following, local avoidance helpers
  crowd.hpp             // multi-agent batch updates
  obstacle.hpp          // dynamic obstacle overlay
  serialization.hpp     // save/load for nav assets and caches
  diagnostics.hpp       // debug draw data, stats, validation reports
  detail/
    bitset.hpp
    funnel.hpp
    a_star.hpp
    spatial_hash.hpp
    ring_buffer.hpp
```

If you want to split runtime and tools, use:

```text id="a1l7bk"
src/nav/
  bake_tool.cpp
  mesh_import.cpp
  mesh_validate.cpp
```

## 4. Core data model

### 4.1 Navmesh assets

A navmesh asset should be the primary runtime unit.

```cpp id="f2jv4m"
namespace threadmaxx::nav {

using NavMeshId = std::uint64_t;
using NavTileId = std::uint32_t;
using NavPolyId = std::uint32_t;
using PathId = std::uint64_t;

struct NavMeshRef {
    NavMeshId id{};
    std::uint32_t generation{};
};

struct NavMeshMeta {
    std::string name;
    float cellSize{};
    float cellHeight{};
    std::uint32_t tileCount{};
    std::uint32_t polygonCount{};
};

} // namespace threadmaxx::nav
```

### 4.2 Tile-based runtime storage

Use a tiled navmesh so streaming and invalidation stay local.

Each tile owns:

* polygons,
* adjacency,
* portal edges,
* area cost tags,
* off-mesh links,
* local bounding volume.

That makes it easy to stream large worlds, which is exactly the kind of large-world support the roadmap is aiming at. 

### 4.3 Agent state

The engine already has a `NavAgentRef` slot; this library should define the runtime state behind it.

```cpp id="y0m3cr"
namespace threadmaxx::nav {

enum class AgentStatus : std::uint8_t {
    Idle,
    Querying,
    Moving,
    Repathing,
    Stuck,
    Failed
};

struct NavAgent {
    NavAgentId id{};
    NavMeshRef mesh{};
    Vec3 position{};
    Vec3 desiredVelocity{};
    Vec3 actualVelocity{};
    NavPolyId currentPoly{};
    NavPolyId goalPoly{};
    PathId activePath{};
    AgentStatus status{AgentStatus::Idle};
};

} // namespace threadmaxx::nav
```

## 5. Public API

### 5.1 Mesh management

```cpp id="qk9d3t"
namespace threadmaxx::nav {

class NavMeshRegistry {
public:
    NavMeshRef load(std::span<const std::byte> bakedData);
    void unload(NavMeshRef mesh);

    bool isValid(NavMeshRef mesh) const noexcept;
    std::optional<NavMeshMeta> meta(NavMeshRef mesh) const;
};

} // namespace threadmaxx::nav
```

### 5.2 Path queries

```cpp id="v6x8ad"
namespace threadmaxx::nav {

struct PathRequest {
    NavMeshRef mesh{};
    Vec3 start{};
    Vec3 goal{};
    float agentRadius{};
    float agentHeight{};
    std::uint32_t areaMask{0xFFFFFFFFu};
    bool allowPartial{true};
};

struct PathResult {
    PathId id{};
    bool ready{};
    bool success{};
    std::vector<Vec3> waypoints;
    float cost{};
};

class PathQueryService {
public:
    PathId request(const PathRequest& request);
    std::optional<PathResult> tryGet(PathId id) const;
    void cancel(PathId id);
    void clear();
};

} // namespace threadmaxx::nav
```

### 5.3 Batch path solving

The key gameplay use case is many agents asking for paths in the same frame.

```cpp id="m5r4az"
namespace threadmaxx::nav {

struct BatchPathRequest {
    NavMeshRef mesh{};
    std::span<const Vec3> starts;
    std::span<const Vec3> goals;
    float agentRadius{};
    float agentHeight{};
};

class BatchPathSolver {
public:
    std::vector<PathResult> solve(const BatchPathRequest& request);
};

} // namespace threadmaxx::nav
```

### 5.4 Steering and corridor following

Pathfinding gives you a route; steering makes the agent move smoothly along it.

```cpp id="x7p2jc"
namespace threadmaxx::nav {

struct FollowPathInput {
    std::span<const Vec3> corridor;
    Vec3 currentPosition{};
    Vec3 currentVelocity{};
    float maxSpeed{};
    float maxAcceleration{};
    float dt{};
};

struct FollowPathOutput {
    Vec3 desiredVelocity{};
    Vec3 nextTarget{};
    bool finished{};
};

FollowPathOutput followPath(const FollowPathInput& input);

} // namespace threadmaxx::nav
```

### 5.5 Dynamic obstacle overlay

The library should support temporary blockers without rebaking the whole mesh.

```cpp id="c3n1qa"
namespace threadmaxx::nav {

struct DynamicObstacle {
    Vec3 center{};
    Vec3 halfExtents{};
    float height{};
    std::uint32_t areaMask{};
};

class ObstacleOverlay {
public:
    std::uint64_t add(DynamicObstacle obstacle);
    void remove(std::uint64_t obstacleId);
    void update(std::uint64_t obstacleId, DynamicObstacle obstacle);
};

} // namespace threadmaxx::nav
```

This is still not physics. It is just navigation input.

## 6. Integration with `threadmaxx`

The library should integrate through the engine’s existing scheduling and component hooks, not through private storage access. The roadmap explicitly says the engine only needs to expose scheduling primitives and the `NavAgentRef` slot for this kind of system. 

### 6.1 System model

A navigation system in a game should look like this:

```cpp id="b9t8fw"
class NavigationSystem final : public threadmaxx::ISystem {
public:
    void update(threadmaxx::SystemContext& ctx) override {
        ctx.forEachChunk<threadmaxx::NavAgentRef, threadmaxx::Transform>(
            [&](auto& chunk) {
                auto agents = chunk.span<threadmaxx::NavAgentRef>();
                auto transforms = chunk.span<threadmaxx::Transform>();

                // feed agent positions into threadmaxx::nav
                // request paths, steer, write back desired velocity
            }
        );
    }
};
```

The important part is the shape: chunk spans in, navigation queries out, movement written back in the game system.

### 6.2 Background work

Path solving, tile validation, and crowd updates should run on worker threads using the engine’s existing job scheduling. That is exactly the sort of “background work” the roadmap says the engine should support rather than own. 

### 6.3 Event hooks

Use events for navigation state changes:

* path completed,
* path failed,
* agent stuck,
* mesh invalidated,
* overlay changed,
* repath requested.

That keeps the navigation layer decoupled from gameplay reactions.

## 7. Bake pipeline

The library should separate **baking** from **runtime use**.

### 7.1 Bake inputs

Accept:

* triangle meshes,
* collision meshes,
* area tags,
* off-mesh links,
* agent radius/height presets,
* tile size / cell size / slope limits.

### 7.2 Bake outputs

Produce:

* compact runtime navmesh blobs,
* tile headers,
* adjacency tables,
* serialization metadata,
* validation reports.

### 7.3 Validation

Bake-time validation should check:

* disconnected islands,
* non-manifold geometry,
* too-small portals,
* unreachable goals,
* degenerate triangles,
* invalid off-mesh links.

A failure here should be a content error, not a runtime crash.

## 8. Runtime behaviors

### 8.1 A* over navmesh

Use A* over polygon adjacency for the global route.

### 8.2 Funnel smoothing

Use the funnel algorithm to turn polygon corridors into clean waypoints.

### 8.3 Repathing

Repath when:

* the goal changes,
* the mesh version changes,
* the corridor is blocked,
* the agent strays too far from the path.

### 8.4 Local avoidance

For local avoidance, keep it simple and bounded:

* velocity obstacle style steering,
* short horizon sampling,
* obstacle cost fields,
* priority-based separation.

Do **not** turn this into a full physics solver.

## 9. What the library should not do

* no rigid-body physics,
* no collision resolution authority,
* no animation IK,
* no rendering,
* no matchmaking,
* no network replication,
* no editor UI,
* no ECS ownership,
* no pathfinding baked into the engine core.

That matches the roadmap’s boundary: navigation is a sibling library, not a core engine feature. 

## 10. Implementation order

1. navmesh asset format,
2. runtime registry and handles,
3. single-path A*,
4. funnel smoothing,
5. batch path solving,
6. dynamic obstacle overlay,
7. agent steering helpers,
8. crowd updates,
9. serialization and validation,
10. bake tool.

## 11. Tests to add

* pathfinding over a fixed known mesh,
* unreachable goal handling,
* partial path handling,
* corridor smoothing correctness,
* tile invalidation and repath,
* streamed mesh load/unload,
* batch solver consistency,
* obstacle overlay updates,
* large-agent crowd stability,
* deterministic repeatability for identical inputs.
