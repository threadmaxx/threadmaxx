# `threadmaxx_physics` — User Guide

Backend-agnostic rigid-body physics: world / body / shape lifecycle,
queries, joints, character controller, and contact events for the
`threadmaxx` engine's POD model.

## When to use this library

Reach for `threadmaxx_physics` when you have:

- Rigid-body simulation (boxes, spheres, capsules, convex hulls, static
  meshes — soft-body / cloth is out of scope).
- Gameplay queries (raycasts for line-of-sight, sphere sweeps for
  movement, overlap probes for triggers).
- A character controller you can drive with intent vectors instead of
  applying forces by hand.
- Joints between bodies (doors, vehicle hinges, ragdolls).
- The need to swap solvers later — bundled Stub and Jolt today; Bullet
  / PhysX adapters follow the same recipe (see
  `BACKEND_PORTING_GUIDE.md`).

It is NOT a renderer, an entity manager, or a physics-aware ECS. The
engine owns entities + components; physics owns its own world handles
and exposes per-tick `BodyState` for the engine to copy back into
`Transform`. Solver execution lives behind `IPhysicsBackend`.

## Quick start

```cpp
#include <threadmaxx_physics/threadmaxx_physics.hpp>

using namespace threadmaxx::physics;

// 1. Pick a backend. Jolt when available, Stub when not.
std::unique_ptr<IPhysicsBackend> backend = makeJoltBackend();
if (!backend) backend = makeStubBackend();

// 2. Create a world with fixed-step settings.
PhysicsConfig cfg;
cfg.fixedTimestep         = 1.0f / 60.0f;
cfg.allowSolverThreading  = false;   // deterministic profile
PhysicsWorldId world = backend->createWorld(cfg);

// 3. Register shapes once at level load.
ShapeDesc boxDesc;
boxDesc.type        = ShapeType::Box;
boxDesc.halfExtents = Vec3{0.5f, 0.5f, 0.5f};
ShapeId boxShape = backend->createShape(boxDesc);

// 4. Spawn bodies referencing the shared shape.
BodyDesc body;
body.position = Vec3{0.0f, 10.0f, 0.0f};
ShapeId boxShapes[1] = {boxShape};
BodyId b = backend->createBody(world, body,
                               std::span<const ShapeId>(boxShapes, 1));

// 5. Per tick: step + sync.
backend->stepWorld(world, cfg.fixedTimestep);
auto state = backend->getBodyState(world, b);
// state->position is the new world-space pose.
```

## Build setup

Add the dependency:

```cmake
target_link_libraries(my_target PRIVATE threadmaxx::physics)
```

The CMake target carries the include directory, `cxx_std_20`, and a
transitive link to `threadmaxx::threadmaxx` (for the math PODs). The
library produces one static archive (`libthreadmaxx_physics.a` on
POSIX).

### Build options

| Option | Default | Purpose |
|---|---|---|
| `THREADMAXX_BUILD_PHYSICS` | `ON` | Top-level switch — `OFF` drops the `threadmaxx::physics` target |
| `THREADMAXX_PHYSICS_FETCH_JOLT` | `OFF` | When `ON` and `find_package(Jolt)` fails, FetchContent the pinned upstream tag |
| `THREADMAXX_PHYSICS_JOLT_TAG` | `v5.3.0` | Pinned Jolt upstream tag used by FetchContent |
| `THREADMAXX_BUILD_BENCHMARKS` | `OFF` | Builds `bench/physics_jolt_bench` when Jolt is on |

```bash
# Stub only (default — Jolt is not required for v1.0)
cmake -B build -DTHREADMAXX_BUILD_PHYSICS=ON

# Stub + Jolt (FetchContent fallback)
cmake -B build -DTHREADMAXX_PHYSICS_FETCH_JOLT=ON
```

Check at configure time whether the Jolt path was actually wired:

```cpp
if constexpr (joltBackendAvailable()) {
    // Jolt-only setup
}
```

`joltBackendAvailable()` is a `constexpr` over the library's compile
definition — it's the source of truth, not the value of any CMake
variable in the consumer's tree.

## Public surface inventory

All types live in namespace `threadmaxx::physics`. Each header is a
clean unit — include just what you need (or the umbrella
`threadmaxx_physics.hpp`).

### Identifiers (`types.hpp`)

| Type | Purpose |
|---|---|
| `PhysicsWorldId` | Opaque world handle (zero = invalid) |
| `BodyId` | Per-world body handle |
| `ShapeId` | Backend-wide shape handle (refcounted) |
| `JointId` | Per-world constraint handle |

Every id is a one-`uint64_t` POD with `operator==` and an explicit
`bool` conversion (`if (id)` ↔ `id.value != 0`).

### Backend (`backend.hpp`, `stub_backend.hpp`, `jolt_backend.hpp`)

| Function / type | Purpose |
|---|---|
| `IPhysicsBackend` | Abstract base — 18 pure virtuals covering world / shape / body / step / query / constraint / contact |
| `makeStubBackend()` | Deterministic null backend; always available |
| `makeJoltBackend()` | Real Jolt-backed adapter; returns `nullptr` when Jolt absent |
| `joltBackendAvailable()` | `constexpr` flag — true when library was built with Jolt |

A backend is owned by `std::unique_ptr<IPhysicsBackend>` and lives for
the duration of the simulation. Multiple worlds can be created behind
one backend; ids are scoped per-world.

### Config (`config.hpp`)

| Field | Default | Purpose |
|---|---|---|
| `fixedTimestep` | `1/60` | Outer cadence the engine drives `stepWorld` at |
| `maxSubSteps` | `4` | Cap on substeps a single `stepWorld` call may take |
| `gravityX/Y/Z` | `(0, -9.81, 0)` | Default gravity vector for dynamic bodies |
| `allowSolverThreading` | `true` | Set `false` for deterministic / lockstep profile |

### Shapes (`shape.hpp`)

| Type | Fields read |
|---|---|
| `ShapeType::Box` | `halfExtents` |
| `ShapeType::Sphere` | `radius` |
| `ShapeType::Capsule` | `radius`, `height` (cylinder length between caps, +Y axis) |
| `ShapeType::ConvexHull` | `vertices` (backend computes hull) |
| `ShapeType::Mesh` | `vertices`, `indices` (triangle list) |
| `ShapeType::Compound` | `children` (previously-registered `ShapeId`s) |

Compounds refcount their children — destroying a child while a parent
holds it is safe; the actual free happens when the last referent
(body OR parent) is gone.

### Bodies (`body.hpp`)

| Type | Purpose |
|---|---|
| `BodyType::Static` | Never moves; backend may skip integration |
| `BodyType::Dynamic` | Full simulation (forces, gravity, collisions) |
| `BodyType::Kinematic` | Script-driven; obeys `linearVelocity` but never forces |
| `BodyDesc` | Create-time blueprint (transform, velocities, mass, friction, restitution, CCD, sleeping, layer) |
| `BodyState` | Per-tick read-back (`position`, `rotation`, `linearVelocity`, `angularVelocity`) |

A body is bound to ≥ 1 shapes at creation. Set
`BodyDesc::enableCCD = true` for fast-moving objects that would
otherwise tunnel through walls (real-backend cost; stub no-op).

### Stepping (`step.hpp`)

| Helper | Purpose |
|---|---|
| `stepScene(backend, world, dt)` | Single world, variable dt |
| `stepScenes(backend, span<worlds>, dt)` | Multi world, same dt |
| `stepSceneFixed(backend, world, dt, fixedStep, maxSubSteps, &accumulator)` | Fixed-timestep accumulator pattern |

The `FixedStepAccumulator` pattern is the recommended profile for
gameplay determinism — feed wall-clock `dt`, the helper drains it
into N fixed sub-steps capped at `maxSubSteps`, and returns the
sub-step count actually executed.

### Queries (`query.hpp`)

| Function | Purpose |
|---|---|
| `raycast(backend, world, req)` | Closest-hit ray |
| `sweep(backend, world, req)` | Closest-hit sphere-cast |
| `overlapBodies(backend, world, req, &outVec)` | Buffer-reuse overlap |
| `overlapBodies(backend, world, req) -> vector<BodyId>` | Convenience form |

Every request carries a 32-bit `layerMask`. A body is considered iff
`(1u << body.layer) & request.layerMask != 0`. Default mask
`0xFFFFFFFF` matches every layer; use this for "world geometry only"
or "characters only" filters by partitioning the 32 layer slots in
your game-side enum.

### Sync (`sync.hpp`)

| Helper | Purpose |
|---|---|
| `syncBodyStates(backend, world, span<bodies>, span<outStates>)` | Fill caller-owned buffer |
| `syncBodyStates(backend, world, span<bodies>) -> vector<BodyState>` | Allocating convenience |

The span form is the steady-state path — a system keeps a scratch
`std::vector<BodyState>` sized once at level load, `assign(size, {})`s
it each tick, and lets the backend fill it in place.

### Constraints (`constraints.hpp`)

| Type | DOF | Reads |
|---|---|---|
| `ConstraintType::Fixed` | 0 | anchors |
| `ConstraintType::Hinge` | 1 rot | anchors, axis, `angularLimits[0]` |
| `ConstraintType::Slider` | 1 trans | anchors, axis, `linearLimits[0]` |
| `ConstraintType::BallSocket` | 3 rot | anchors |
| `ConstraintType::SixDOF` | 6 | anchors, axis (frame X), `linearLimits[3]`, `angularLimits[3]` |

`disableCollisionBetweenLinkedBodies = true` tells the backend's
broadphase to skip the pair — use this for "wheel inside axle" or
"ragdoll bones meeting at a joint" geometries.

Destroying either body invalidates the constraint silently
(`getConstraint` returns `nullopt` without the host needing to call
`destroyConstraint` first).

### Character controller (`character.hpp`)

| Type / function | Purpose |
|---|---|
| `CharacterControllerDesc` | Capsule dimensions, step-up height, slope limit, move speed, gravity, layer mask |
| `CharacterInput` | Per-tick `moveIntent` + `jump` + `jumpSpeed` |
| `CharacterState` | `position`, `rotation`, `velocity`, `grounded` |
| `CharacterController(backend, world, desc)` | Owns the per-character state |
| `controller.move(input, dt)` | Advance one tick |
| `isSurfaceWalkable(desc, groundNormal)` | Free helper for game-side slope checks |

The controller is a kinematic capsule driven through sphere-sweeps
against the world. It does NOT register as a backend body — other
queries (`raycast` against the world) won't hit the character unless
you spawn a matching kinematic body alongside it.

### Contact events (`contact.hpp`)

| Type / function | Purpose |
|---|---|
| `ContactPhase::Begin` / `::End` | Phase enum (no Persist) |
| `ContactEvent { phase, bodyA, bodyB }` | Canonicalized so `bodyA.value < bodyB.value` |
| `ContactCallback` | `std::function<void(const ContactEvent&)>` |
| `setContactCallback(backend, world, cb)` | Install / replace |
| `clearContactCallback(backend, world)` | Detach |

Continuing-overlap notifications are intentionally NOT emitted — game
code that wants per-tick contact processing runs `overlapBodies` on
its own schedule. Destroying a body involved in an active contact
fires an End event for every active pair touching it BEFORE the body
generation bumps; the callback sees the still-valid pre-destroy id.

## Two backend choices

### `StubBackend` — `makeStubBackend()`

- Deterministic by construction.
- `stepWorld` performs kinematic integration only: `position +=
  linearVelocity * dt` and angular composition. No gravity, no
  collision response.
- Queries use the world-space AABB of every body's attached shapes;
  rotation is ignored (no true OBB narrowphase).
- Constraints are recorded but not enforced.
- Contact events fire on AABB overlap transitions.

Use the stub when:

- You're running tests that need bit-stable behavior.
- The game's "physics disabled" mode still needs every API call to
  succeed (the engine keeps allocating `PhysicsBodyRef` slots).
- You're authoring a new backend and want a reference for the API
  surface contract.

### `JoltBackend` — `makeJoltBackend()`

- Real broadphase + narrowphase + iterative constraint solver.
- Pinned to upstream Jolt `v5.3.0`. Bumping the pin is a documented
  upgrade in `BACKEND_PORTING_GUIDE.md`.
- Determinism profile: `Jolt` built with `CROSS_PLATFORM_DETERMINISTIC
  = ON` (default in our FetchContent path) + fixed `dt` +
  `PhysicsConfig::allowSolverThreading = false` → bit-identical runs.
- Returns `nullptr` if the library was built without Jolt.

Use Jolt for production scenes. The conformance test
(`tests/physics/test_physics_jolt_conformance.cpp`) re-runs a subset
of the P1–P8 invariants against Jolt with documented tolerances; the
smoke test (`test_physics_jolt_smoke.cpp`) drops a sphere on a ground
plate and asserts gravity + non-penetration.

## Determinism profile

For deterministic / lockstep / replay use cases:

1. `PhysicsConfig::allowSolverThreading = false`
2. Drive with `stepSceneFixed` against a constant `fixedStep` (no
   variable `dt` reaching the backend).
3. Use `JoltBackend` built with `CROSS_PLATFORM_DETERMINISTIC = ON`
   (the default when the library FetchContents Jolt).
4. Avoid hardware FMA divergence — Jolt's deterministic profile pins
   this internally; don't override `INTERPROCEDURAL_OPTIMIZATION`.

Same code + same input under that profile → bit-identical state
across two side-by-side runs.

## Integration with the engine

The engine's `PhysicsBodyRef` component is a game-side id — typically
`BodyId::value` cast to whatever the engine stores. The recommended
wiring is:

```cpp
class PhysicsSystem : public threadmaxx::ISystem {
public:
    ComponentSet reads()  const noexcept override {
        return ComponentSet{Component::Transform, Component::PhysicsBodyRef};
    }
    ComponentSet writes() const noexcept override {
        return ComponentSet{Component::Transform};
    }

    void preStep(threadmaxx::SystemContext& ctx) override {
        // Sim-thread serial — drive the backend forward.
        backend_->stepWorld(world_, static_cast<float>(ctx.dt()));
    }

    void update(threadmaxx::SystemContext& ctx) override {
        // Parallel — sync BodyState back into Transform.
        threadmaxx::forEachChunk<PhysicsBodyRef, Transform>(ctx,
            [&](std::span<const EntityHandle> es,
                std::span<const PhysicsBodyRef> refs,
                std::span<const Transform> /*read*/,
                CommandBuffer& cb) {
                for (size_t i = 0; i < es.size(); ++i) {
                    auto st = backend_->getBodyState(world_, BodyId{refs[i].id});
                    if (st) cb.setTransform(es[i], Transform{st->position, st->rotation, ...});
                }
            });
    }

private:
    IPhysicsBackend* backend_;
    PhysicsWorldId   world_;
};
```

`preStep` runs serially on the sim thread — exactly where backend
state mutation is safe. `update` is parallel; each worker only reads
`BodyState`s, which is safe because `stepWorld` already returned.
Writes go through the engine's `CommandBuffer` machinery.

For controllers, drive them from `preStep` too (same serialization
requirement as `stepWorld`).

## Conventions

### Allocation policy

- **Per-tick**: `stepWorld` / `getBodyState` / `syncBodiesToGame` /
  `raycast` / `sweep` do not allocate.
- **Per-tick `overlapBodies` and crowd character controllers**:
  caller-owned buffers — pass the same `std::vector<BodyId>` every
  tick to amortize the allocation away.
- **Setup**: `createWorld` / `createShape` / `createBody` /
  `createConstraint` allocate; do them at level load.
- **Destruction**: backends are free to defer freeing under refcount
  (compound shapes, deferred-destroy shapes referenced by alive
  bodies). Never assume immediate release.

### NaN / invalid inputs

- A query against an invalid `PhysicsWorldId` returns `nullopt` /
  empty.
- A body with no shapes degenerates to a zero-extent AABB at its
  position; queries still hit it as a point.
- Backends are NOT required to sanitize NaN inputs — the host owns
  validity. Failing here is undefined behavior on the real backend.

### Threading

- Backends are sim-thread-only by default. Concurrent calls into
  different methods on one backend instance are undefined unless the
  implementation explicitly documents otherwise.
- Workers MAY read `BodyState` after `stepWorld` returns (the state is
  copy-out — `getBodyState` returns by value).
- Multiple worlds behind one backend are independently lockable in
  principle but the current implementations do not parallelize across
  worlds — drive them sequentially from `preStep`.

## Performance expectations

Measured on a recent x86_64 desktop, single-thread, medians from
`bench/physics_jolt_bench`:

| Scenario | Per-tick cost |
|---|---:|
| 1024 dynamic boxes falling on ground, Jolt, single-threaded | ~0.8 ms |
| 1024 dynamic boxes falling on ground, max-tick spike | ~7.8 ms |
| 60-tick burn-in, Jolt | ~48 ms total |

Stub-backend numbers are dominated by the kinematic integration loop
plus the `O(bodies)` AABB sweep used by queries; expect microseconds
per body. Real workloads should pick Jolt — the stub is an API
correctness reference, not a performance target.

### When to expect a perf cliff

- **Mesh / ConvexHull shapes** — narrowphase cost scales with vertex
  count. Pre-decimate.
- **Allow-solver-threading off** — the deterministic profile is
  slower; expect ~1.5–2× over the threaded profile on 1k-body scenes.
- **CCD-on bodies** — Jolt runs a swept-volume narrowphase per CCD
  body per tick. Reserve for projectiles / fast-moving characters.

## Restrictions / non-goals

Per `DESIGN_NOTES.md` §1, the library does NOT:

- Own ECS components or entity state (game-side concern).
- Replace the engine's simulation loop.
- Render meshes.
- Simulate cloth, vehicles, fluids.
- Replicate state over a network (lives in `threadmaxx_network`).
- Generate navmeshes.
- Provide editor UI.

If you need any of the above, build it as a separate sibling library
or in your game-side code.

## Library version

```cpp
#include <threadmaxx_physics/version.hpp>

// Compile-time:
static_assert(THREADMAXX_PHYSICS_VERSION_MAJOR == 1);
#if THREADMAXX_PHYSICS_VERSION >= 10100  // require ≥ 1.1.0
    // ...
#endif

// Runtime:
std::printf("threadmaxx_physics v%s\n",
            threadmaxx::physics::version_string());
```

Version bumps follow [semver](https://semver.org/). See
`CHANGELOG.md` for the release history and `MAINTAINER_GUIDE.md` for
the full lifecycle policy.

## See also

- `README.md` — top-level overview.
- `DESIGN_NOTES.md` — the original spec.
- `FUTURE_WORK.md` — batch-by-batch landed work + v1.x candidates.
- `CHANGELOG.md` — per-release notes.
- `MAINTAINER_GUIDE.md` — architecture, lifecycle invariants,
  extending the surface internally.
- `BACKEND_PORTING_GUIDE.md` — how to write a new
  `IPhysicsBackend` adapter (Bullet / PhysX / custom).
- `tests/physics/*.cpp` — example usage of every public API.
- `examples/physics_demo/main.cpp` — end-to-end scene: character
  controller, obstacles, dynamic boxes, contact callbacks.
- `bench/physics_jolt_bench.cpp` — 1024-box throughput harness.
