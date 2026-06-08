# `threadmaxx_physics`

Backend-adapter physics library for the `threadmaxx` engine. **Status**:
v1.0.0 — production-ready.

## What

A solver-agnostic `IPhysicsBackend` contract plus two bundled
implementations:

- **StubBackend** — deterministic, no real physics. Bodies stay where
  they were spawned; `stepWorld` performs kinematic integration only
  (`position += linearVelocity * dt`). Used by every conformance test
  as the reference and by games for a "physics-disabled" mode.
- **JoltBackend** — gated by `find_package(Jolt)` (or the opt-in
  `THREADMAXX_PHYSICS_FETCH_JOLT` FetchContent fallback). Real
  broadphase + narrowphase + constraint solver, pinned to upstream
  `v5.3.0`.

The library covers nine pillars (one per shipped batch):

- **Foundations** — `PhysicsWorldId` / `BodyId` / `ShapeId` / `JointId`
  trivially-copyable handles plus the `IPhysicsBackend` interface.
- **Shape registry** — Box / Sphere / Capsule / ConvexHull / Mesh /
  Compound shapes with backend-side refcounting and shared ownership.
- **Body lifecycle** — `BodyDesc` create / destroy + `BodyState`
  per-tick read-back, kinematic teleport, span-based batch sync.
- **Stepping** — `stepScene` + `stepSceneFixed` accumulator-driven
  fixed-step path for deterministic profiles.
- **Queries** — closest-hit raycast, sphere sweep, sphere overlap with
  32-bit layer-mask filtering.
- **Constraints** — Fixed / Hinge / Slider / BallSocket / SixDOF joints
  with per-axis limits and self-collision disable.
- **Character controller** — capsule-based step-up / slope-limit /
  ground-detect controller built on top of the query API.
- **Contact events** — synchronous Begin / End callbacks with
  canonicalized pair ordering.
- **Jolt adapter** — gated real-solver backend that maps every public
  type onto Jolt's `BodyInterface` / `NarrowPhaseQuery` /
  `ContactListener`.

The library does NOT own ECS state, the engine's simulation loop, the
renderer, network replication, or navmesh / animation math. The host
engine drives `stepScene` once per tick and copies `BodyState`s back
into its own `Transform` components via the `sync.hpp` helpers.

## Quick start

```cmake
target_link_libraries(my_target PRIVATE threadmaxx::physics)
```

```cpp
#include <threadmaxx_physics/threadmaxx_physics.hpp>

using namespace threadmaxx::physics;

// 1. Pick a backend.
auto backend = makeJoltBackend();
if (!backend) backend = makeStubBackend();

// 2. Create a world.
PhysicsConfig cfg;
cfg.fixedTimestep = 1.0f / 60.0f;
PhysicsWorldId world = backend->createWorld(cfg);

// 3. Drop a dynamic box onto a static ground plate.
ShapeDesc groundDesc; groundDesc.halfExtents = Vec3{50, 0.5f, 50};
ShapeId ground = backend->createShape(groundDesc);

ShapeDesc boxDesc;  boxDesc.halfExtents = Vec3{0.5f, 0.5f, 0.5f};
ShapeId box = backend->createShape(boxDesc);

BodyDesc floorBody;
floorBody.type = BodyType::Static;
floorBody.position = Vec3{0, -0.5f, 0};
backend->createBody(world, floorBody, std::span{&ground, 1});

BodyDesc dynBody;
dynBody.position = Vec3{0, 10, 0};
BodyId b = backend->createBody(world, dynBody, std::span{&box, 1});

// 4. Step.
for (int i = 0; i < 60; ++i) {
    backend->stepWorld(world, cfg.fixedTimestep);
}

auto state = backend->getBodyState(world, b);
// state->position.y ≈ 0.5 (resting on the ground plate).
```

## Documentation

| Document | Audience | Purpose |
|----------|----------|---------|
| [`README.md`](README.md) | Everyone | Top-level overview (this file) |
| [`USER_GUIDE.md`](USER_GUIDE.md) | Consumers | Public surface inventory, integration patterns, determinism profile |
| [`MAINTAINER_GUIDE.md`](MAINTAINER_GUIDE.md) | Library devs | Architecture, lifecycle invariants, adding new backends / shape types / constraints |
| [`BACKEND_PORTING_GUIDE.md`](BACKEND_PORTING_GUIDE.md) | Adapter authors | Step-by-step recipe for a new `IPhysicsBackend` (Bullet / PhysX / custom) |
| [`DESIGN_NOTES.md`](DESIGN_NOTES.md) | Anyone | Original spec (frozen reference) |
| [`FUTURE_WORK.md`](FUTURE_WORK.md) | Library devs | Batch-by-batch landed work + v1.x candidates |
| [`CHANGELOG.md`](CHANGELOG.md) | Everyone | Per-release notes |

## Scope

- ✓ Backend-agnostic rigid-body API (every solver hides behind
  `IPhysicsBackend`).
- ✓ Bundled deterministic `StubBackend` for tests + physics-disabled
  mode.
- ✓ Bundled `JoltBackend` adapter for production use.
- ✓ Shape registry with refcounted shared ownership.
- ✓ Closest-hit ray / sweep / overlap queries with layer-mask filter.
- ✓ Five joint types (Fixed / Hinge / Slider / BallSocket / SixDOF).
- ✓ Capsule character controller with step-up / slope-limit /
  ground-detect.
- ✓ Begin / End contact events with canonical pair ordering.

Out of scope (per `DESIGN_NOTES.md` §1): ECS storage ownership, the
engine's simulation loop, rendering, network replication, navmesh /
pathfinding, animation blending, editor UI, hardcoding a single
solver's world-ownership model. Soft body / cloth, vehicle physics,
and GPU collision queries are tracked as v1.x candidates in
`FUTURE_WORK.md`.

## Status: production-ready

- 30 dedicated test executables registered with CTest (100% passing
  on `build/` and `build-werror/` trees; +2 gated Jolt tests when the
  Jolt gate is on).
- Per-batch retrospectives in `FUTURE_WORK.md` for P1–P9.
- Bundled `bench/physics_jolt_bench` exercises 1024 dynamic boxes at
  60 Hz (~0.8 ms/tick on the reference dev box, single-threaded).
- End-to-end demo at `examples/physics_demo/` drives a character
  controller across an obstacle course with dynamic boxes + contact
  callbacks.
- Versioning policy documented (semver, lifecycle in
  `MAINTAINER_GUIDE.md`).

See [`FUTURE_WORK.md`](FUTURE_WORK.md) for v1.x candidate work (Bullet
/ PhysX adapters, soft body, vehicle physics, GPU queries — none of
which blocks v1.0 production use).

## License

Same as the parent `threadmaxx` project.
