# `threadmaxx_physics` — Backend Porting Guide

How to write a new `IPhysicsBackend` adapter (Bullet, PhysX, custom
solver) for `threadmaxx_physics`. v1.0 ships with two reference
implementations:

- **`StubBackend`** (`src/threadmaxx_physics/backends/StubBackend.cpp`)
  — deterministic, no real physics. The contract baseline.
- **`JoltBackend`** (`src/threadmaxx_physics/backends/JoltBackend.cpp`)
  — Jolt v5.3.0 adapter. The production-shaped reference.

Read both before writing a new adapter. The Stub demonstrates the
lifecycle invariants in isolation; Jolt demonstrates how to map them
onto a real solver's object model.

## Why backend adapters look the way they do

`IPhysicsBackend` is renderer-style: a thin C-API-shaped abstract class
that real solvers wrap. The shape is deliberate:

- **No virtual templates** — every method is a plain virtual taking
  PODs / spans / shared_ptrs. Stays ABI-stable, costs one vcall per
  operation (negligible vs solver work).
- **Per-method opaque ids** — `PhysicsWorldId` / `BodyId` / `ShapeId`
  / `JointId` are `uint64_t` PODs. Each adapter encodes `(slot,
  generation)` however it likes; the engine never inspects bits.
- **Synchronous everywhere** — `stepWorld`, `raycast`, etc. return
  before they return. No futures, no callbacks except the per-world
  `ContactCallback`. Cleanest for fixed-step gameplay loops.

The engine assumes a single backend instance is sim-thread-only.
Parallel queries / parallel stepping across multiple worlds are not in
v1.0; if your solver supports it, expose it through a separate
top-level helper and document the threading contract.

## The 18 virtuals at a glance

Group by concern. Implementing them in this order minimises rework:

| Phase | Methods | Notes |
|---|---|---|
| 1. World | `createWorld`, `destroyWorld` | Bare table; backend-specific config goes in `WorldSlot` |
| 2. Shape | `createShape`, `destroyShape`, `getShapeDesc`, `getShapeAabb` | Refcounted; compounds add 1 ref per child |
| 3. Body | `createBody`, `destroyBody`, `getBodyState`, `setBodyTransform` | Per-world; bumps body slot generation on destroy |
| 4. Step | `stepWorld` | Real work happens here |
| 5. Sync | `syncBodiesToGame` | Span-out batch read — usually a loop over `getBodyState` |
| 6. Query | `raycast`, `sweep`, `overlap` | Layer-mask filter post-broadphase |
| 7. Constraint | `createConstraint`, `destroyConstraint`, `getConstraint` | Solver-owned; destroy cascade on body destroy |
| 8. Contact | `setContactCallback` | Begin/End only; canonicalize `bodyA.value < bodyB.value` |

Get each phase passing the corresponding test subset before moving on
— there's no "almost works" middle state with physics bugs.

## Step 0 — copy the file structure

Create your adapter file under
`src/threadmaxx_physics/backends/<MyAdapter>Backend.cpp` and a header
`include/threadmaxx_physics/<myadapter>_backend.hpp` exposing the
factory:

```cpp
// include/threadmaxx_physics/<myadapter>_backend.hpp
#pragma once
#include "threadmaxx_physics/backend.hpp"
#include <memory>

namespace threadmaxx::physics {
std::unique_ptr<IPhysicsBackend> makeMyAdapterBackend();
constexpr bool myAdapterBackendAvailable() noexcept {
#if defined(THREADMAXX_PHYSICS_HAS_MYADAPTER)
    return true;
#else
    return false;
#endif
}
} // namespace threadmaxx::physics
```

Add the header to `THREADMAXX_PHYSICS_PUBLIC_HEADERS` in
`src/threadmaxx_physics/CMakeLists.txt`. Add the source file to
`THREADMAXX_PHYSICS_SOURCES`.

Gate the adapter on the CMake side (mirror the Jolt pattern):

```cmake
find_package(MyAdapter CONFIG QUIET)
if (MyAdapter_FOUND)
    set(THREADMAXX_PHYSICS_HAS_MYADAPTER ON)
    target_compile_definitions(threadmaxx_physics PRIVATE THREADMAXX_PHYSICS_HAS_MYADAPTER=1)
    target_link_libraries(threadmaxx_physics PRIVATE MyAdapter::MyAdapter)
endif()
set(THREADMAXX_PHYSICS_HAS_MYADAPTER ${THREADMAXX_PHYSICS_HAS_MYADAPTER} PARENT_SCOPE)
```

The TU should start with the gate split so the gate-off build still
compiles cleanly:

```cpp
// src/threadmaxx_physics/backends/MyAdapterBackend.cpp
#include "threadmaxx_physics/<myadapter>_backend.hpp"

#if !defined(THREADMAXX_PHYSICS_HAS_MYADAPTER)
namespace threadmaxx::physics {
std::unique_ptr<IPhysicsBackend> makeMyAdapterBackend() { return nullptr; }
} // namespace threadmaxx::physics
#else

// ... full adapter implementation ...

#endif
```

## Step 1 — World lifecycle

The simplest piece. `createWorld` allocates a `WorldSlot`, records the
`PhysicsConfig`, and returns a non-zero `PhysicsWorldId`. The encoding
of slot + generation in `PhysicsWorldId::value` is YOUR choice — the
engine never inspects bits.

```cpp
struct WorldSlot {
    std::uint32_t generation{1};
    bool          alive{false};
    PhysicsConfig config;
    // ... solver-specific state (PhysicsSystem, allocators, etc) ...
};

PhysicsWorldId createWorld(const PhysicsConfig& config) override {
    auto idx = allocateWorldSlot();
    auto& w  = worlds_[idx];
    w.alive  = true;
    w.config = config;
    // Init solver-specific state here.
    return encodeWorldId(idx, w.generation);
}

void destroyWorld(PhysicsWorldId world) override {
    auto [idx, gen] = decodeWorldId(world);
    if (!validWorld(idx, gen)) return;
    auto& w = worlds_[idx];
    // Tear down solver state.
    w.alive = false;
    ++w.generation;
    freeWorldSlot(idx);
}
```

The `validWorld(idx, gen)` check is critical — return false on stale
generation, and downstream methods should return `nullopt` / no-op.

## Step 2 — Shape lifecycle

Shapes are backend-wide (not per-world). The Jolt adapter still stores
the table on the backend instance — multiple worlds share it. This
matches `JPH::Shape`'s process-wide refcount model.

Refcounting rules:

- `createShape` returns the new `ShapeId` with refcount = 1 ("the user
  holds it").
- `createBody` ADDs the referenced shapes' refcounts by 1 each.
- `createShape(ShapeDesc{type=Compound, children})` adds the child
  shapes' refcounts by 1 each.
- `destroyShape` decrements the user-held reference. `destroyBody`
  decrements each shape that body referenced.
- When a shape's refcount hits zero, free its slot and bump its
  generation.

The "deferred-destroy" contract: calling `destroyShape` while bodies
still hold the shape decrements but does NOT free. The shape becomes
unreachable to NEW `createBody` calls (you can simulate this with an
`alive` flag distinct from `refCount > 0`), but stays usable by
existing bodies until they're destroyed.

For Compound shapes the AABB is the UNION of children's local AABBs
at the origin (per-child local transforms are deferred to v1.x —
`ShapeDesc::children` is a flat `vector<ShapeId>` without transforms).

## Step 3 — Body lifecycle

Per-world table. `BodyId::value` encodes `(world, slot, generation)`
or just `(slot, generation)` plus a backend-internal world lookup.
Jolt does the latter — the `BodyId.value` encodes Jolt's `JPH::BodyID`
plus our slot generation; world disambiguation happens by which
`PhysicsSystem` you call the lookup on.

Key contract points:

- `getBodyState` returns `nullopt` on stale generation.
- `setBodyTransform` writes BOTH the `BodyDesc::position/rotation`
  (so a future respawn from the desc picks up the new pose) AND the
  live `BodyState`. Real solvers usually expose this as
  `BodyInterface::SetPositionAndRotation`.
- `setBodyTransform` on a Dynamic body skips physically-plausible
  motion. Document it; don't try to be clever. The host owns the
  call.

For body-shape associations, store a `std::vector<ShapeId>` on the
`BodySlot`. On `destroyBody`, decrement each shape's refcount; if
refcount hits zero AND the user has already called `destroyShape`,
free the shape.

## Step 4 — Stepping

The real work. Each `stepWorld(world, dt)`:

1. Validate `world`; no-op on stale id.
2. Read `PhysicsConfig::fixedTimestep` + `maxSubSteps`; decide how
   many internal sub-steps to run. (Or run a single integration of
   `dt` if the solver doesn't want substepping.)
3. Drive the solver. For Jolt:
   `system->Update(dt, collisionSteps=1, tempAllocator, jobSystem)`.
4. Drain solver-side contact events into the user's
   `ContactCallback` if installed.

The synchronization point that matters for the engine: `stepWorld`
returns BEFORE it returns. Workers can read `BodyState` immediately
after.

Determinism profile: if the solver has a "deterministic mode" toggle,
key it off `PhysicsConfig::allowSolverThreading == false`. For Jolt
this is `CROSS_PLATFORM_DETERMINISTIC` (build-time) +
single-threaded `JobSystem` (runtime). Document the profile in your
adapter's header doc block.

## Step 5 — Batch sync

`syncBodiesToGame(world, span<bodies>, span<outStates>)` is a loop
over `getBodyState`. The two spans MUST match in size; misaligned
sizes should be a documented no-op. Real backends often have a
specialized batch read (Jolt's `BodyInterface::GetBodiesInActiveLayer`,
PhysX's `PxScene::getActiveActors`); use them if they're cheaper than
N individual reads.

## Step 6 — Queries

Three flavors:

- **Raycast** — `request.origin + request.direction * t` for
  `t ∈ [0, maxDistance]`. Return the closest hit's body, position,
  normal, and parametric distance.
- **Sweep** — sphere of `request.radius` centered at `request.start`,
  swept along `request.direction`. Return body + sphere-center
  position at first contact + normal + distance.
- **Overlap** — append every body whose collision volume intersects
  the query sphere to `outBodies`. Clear `outBodies` BEFORE
  populating. No ordering guarantee.

Layer filtering: every request carries a `layerMask`. Post-filter the
broadphase result with `(1u << body.layer) & request.layerMask`. The
Jolt adapter does this post-broadphase because Jolt's own layer
system is a two-class (`STATIC` vs `MOVING`) broadphase split — our
32-bit query mask is layered ON TOP.

Surface normals: by convention, point BACK toward the half-space the
query came from. Real backends usually provide this directly (Jolt's
`Body::GetWorldSpaceSurfaceNormal`); if not, compute it from the hit
geometry.

## Step 7 — Constraints

Each constraint type maps to a solver subclass:

| ConstraintType | Jolt | Bullet | PhysX |
|---|---|---|---|
| Fixed | `FixedConstraintSettings` | `btFixedConstraint` | `PxFixedJoint` |
| Hinge | `HingeConstraintSettings` | `btHingeConstraint` | `PxRevoluteJoint` |
| Slider | `SliderConstraintSettings` | `btSliderConstraint` | `PxPrismaticJoint` |
| BallSocket | `PointConstraintSettings` | `btPoint2PointConstraint` | `PxSphericalJoint` |
| SixDOF | `SixDOFConstraintSettings` | `btGeneric6DofConstraint` | `PxD6Joint` |

Constraint frame convention: `localAxisA` / `localAxisB` for Hinge /
Slider is the axis itself. SixDOF interprets it as the constraint
frame's local X (Y / Z derived by the adapter — Jolt uses Gram-Schmidt
in the settings constructor).

Per-axis limits:

- `ConstraintLimit { min = 1, max = -1 }` (default — `min > max`) →
  "free", DOF unrestricted.
- `min == max` → DOF locked at that value.
- `min < max` → DOF clamped to `[min, max]`.

For Hinge, read `angularLimits[0]`. For Slider, `linearLimits[0]`.
For SixDOF, all six. Other types ignore limits.

`disableCollisionBetweenLinkedBodies = true` → tell the broadphase to
skip the pair. Jolt's mechanism: `GroupFilterTable::SetGroupID` per
constraint, then `DisableCollision(groupA, groupB)`. PhysX has a per-
joint flag. Bullet uses constraint flags on `addConstraint`.

The destroy cascade: when EITHER body is destroyed, the constraint
must invalidate without an explicit `destroyConstraint` call. The
contract: subsequent `getConstraint(joint)` returns `nullopt`. Real
solvers usually auto-destroy joints when an attached body is removed;
mirror that in your `getConstraint` by checking both bodies' aliveness
before returning the desc.

## Step 8 — Contacts

Hook into the solver's contact listener:

- Jolt: `class WorldContactListener : public JPH::ContactListener`
  with `OnContactAdded` / `OnContactRemoved`. Persist
  (`OnContactPersisted`) is intentionally dropped.
- Bullet: walk `dispatcher->getNumManifolds()` each step, diff
  against last tick's set.
- PhysX: `class : public PxSimulationEventCallback` with
  `onContact(... events)` filtering by `PxPairFlag::eNOTIFY_TOUCH_FOUND`
  / `eNOTIFY_TOUCH_LOST`.

Canonicalization rule: `event.bodyA.value < event.bodyB.value`. The
contract guarantees a single Begin per overlap episode regardless of
which order the solver visits the pair across ticks.

End-on-destroy: when the host calls `destroyBody`, walk every active
contact touching the body, fire End events, THEN bump the generation.
The callback sees the still-valid pre-destroy id.

Layer filtering for contacts is OPTIONAL at the backend level — game
code can filter inside the callback. Real backends usually wire their
own layer-pair matrix; the v1.0 contract doesn't require it.

## Step 9 — Tests + bench

For each new backend:

1. Add a smoke test
   (`tests/physics/test_physics_<myadapter>_smoke.cpp`) — minimal
   "spawn a body, step, verify position" pin.
2. Add a conformance test
   (`tests/physics/test_physics_<myadapter>_conformance.cpp`) —
   re-run the P1–P8 invariants against the new backend with
   documented tolerances. Real solvers won't bit-match the Stub;
   document the tolerance per check.
3. Register both tests in `tests/physics/CMakeLists.txt` gated on
   `THREADMAXX_PHYSICS_HAS_MYADAPTER`.
4. Optional: a bench at `bench/physics_<myadapter>_bench.cpp` mirroring
   the Jolt bench's 1024-box workload. Register in
   `bench/CMakeLists.txt` gated on the same flag.

The smoke + conformance gate is what proves the adapter is on the
contract. Without it, the adapter ships as "untrusted."

## Pinned dependency versions

When your adapter ties to a specific upstream tag (Jolt does — pinned
to `v5.3.0`), expose the pin as a CMake cache var matching the Jolt
pattern:

```cmake
set(THREADMAXX_PHYSICS_<MYADAPTER>_TAG "v1.2.3" CACHE STRING
    "Pinned upstream <MyAdapter> tag for FetchContent fallback")
```

Document the upgrade path in this guide AND in the factory header's
file-level Doxygen block:

- Bump the tag in `CMakeLists.txt`.
- Rerun smoke + conformance tests.
- If real-physics drift crosses the per-check tolerance, EITHER adjust
  the tolerance (small drift, retained behavior) OR investigate the
  upstream change (potential regression).
- Commit the pin bump + any tolerance changes in the same PR.

## Common pitfalls

### "My adapter compiles but every test segfaults"

Check the `<myadapter>_backend.hpp` header — it MUST NOT include any
adapter-specific solver header. The header is part of the public
surface and ships even in builds where the solver isn't installed.
All solver includes go INSIDE the gated `#else` arm of the .cpp.

### "Refcount goes negative"

You decremented twice. Common culprit: forgetting that the user-held
reference from `createShape` is just one of the references — bodies
that reference the shape via `createBody` add another. The user's
`destroyShape` decrements ONE, not all.

### "Compound shape with two children — one child decrement gets skipped"

Walk every `desc.children` entry in `createShape`, not just the first
one. Easy to miss when copy-pasting from the Box arm.

### "Body destroyed but `BodyId` still resolves to it"

You forgot to bump `slot.generation`. The id stays "valid" by raw
slot index; it should become invalid by generation mismatch. The
adapter's `validBody(idx, gen)` check is the canonical gate.

### "Stale ids after destroy crash inside my solver"

Validate at the adapter boundary, not the solver's. Every method
that takes a `BodyId` / `ShapeId` / `JointId` should be `nullopt` /
no-op on stale id BEFORE touching any solver state.

### "End contact never fires when I `destroyBody`"

The destroy cascade order matters: walk active pairs, fire End events,
THEN bump generation. Reverse that order and the callback sees a
stale id.

### "Constraint persists after I destroy one of its bodies"

You forgot the destroy cascade. The adapter must invalidate the
constraint when EITHER body is destroyed, even without an explicit
`destroyConstraint` call. The `getConstraint` check should test both
bodies' aliveness via their generation.

### "Determinism profile produces different results across runs"

Three common causes: (a) variable `dt` reaching `stepWorld`, (b)
solver threading on, (c) FMA reordering in `-O3` (build the solver
with `INTERPROCEDURAL_OPTIMIZATION OFF` if it's not handled by the
solver's deterministic profile).

### "Adapter silently drops queries when the layer mask is set"

The post-broadphase filter is `(1u << body.layer) & request.layerMask`.
A body with `layer = 5` and a query with `layerMask = 0x01` (bit 0
only) is correctly filtered OUT — that's intentional behavior.
Re-check the layer values your game is writing into `BodyDesc::layer`.

## Checklist

Before shipping a new adapter:

- [ ] `<myadapter>_backend.hpp` factory header in
      `include/threadmaxx_physics/` with `makeMyAdapterBackend()` +
      `myAdapterBackendAvailable()`.
- [ ] Header included from `threadmaxx_physics.hpp` umbrella +
      `THREADMAXX_PHYSICS_PUBLIC_HEADERS` list.
- [ ] `MyAdapterBackend.cpp` under
      `src/threadmaxx_physics/backends/`, gated on
      `THREADMAXX_PHYSICS_HAS_MYADAPTER`.
- [ ] All 18 `IPhysicsBackend` virtuals implemented (search the
      `.hpp` for `virtual` — none should be missing).
- [ ] `find_package` + optional FetchContent fallback in
      `src/threadmaxx_physics/CMakeLists.txt`.
- [ ] Gate variable bubbled up via `set(... PARENT_SCOPE)` for
      tests / bench.
- [ ] Smoke + conformance tests in `tests/physics/`, gated on the
      same flag.
- [ ] Optional bench in `bench/`.
- [ ] `THREADMAXX_PHYSICS_<MYADAPTER>_TAG` cache var if pinning an
      upstream version.
- [ ] Doc block on the factory header covering availability,
      determinism profile, and version-pin upgrade path.
- [ ] `CHANGELOG.md` entry for the new factory.
- [ ] `FUTURE_WORK.md` updated — close the matching v1.x batch.

## See also

- `MAINTAINER_GUIDE.md` — library architecture, lifecycle invariants.
- `USER_GUIDE.md` — what consumers see.
- `DESIGN_NOTES.md` §5 — the original backend contract spec.
- `tests/physics/test_physics_backend_conformance.cpp` — the
  abstract-interface test that any future backend will need to pass.
- `src/threadmaxx_physics/backends/StubBackend.cpp` — minimal
  in-tree reference.
- `src/threadmaxx_physics/backends/JoltBackend.cpp` — production-shaped
  in-tree reference.
