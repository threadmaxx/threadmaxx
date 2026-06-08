# `threadmaxx_physics` — batch plan

Sibling-library implementation plan. `DESIGN_NOTES.md` is the
authoritative spec; this doc breaks it down into shippable
test-driven batches.

Status: **in progress**. P1 + P2 + P3 + P4 + P5 landed 2026-06-08.
Remaining batches (P6–P9) are 📋 planned. Sequencing follows the §8
"implementation order" of the design notes, regrouped into shippable
units that each carry their own tests.

## Conventions

Each batch is independently shippable:

- **Goal** — what the batch accomplishes in one sentence.
- **Test gate** — assertions that prove the batch landed.
- **Files** — what's added / modified.
- **Risks** — what could break.
- **Out of scope** — explicitly deferred to a later batch.

This library is a **backend adapter**, not a solver. The hot work
(broadphase, narrowphase, constraint solving, integration) all
lives behind `IPhysicsBackend`. v1.0 ships with two backends:
`StubBackend` (deterministic, used by tests + as a documented null
implementation) and `JoltBackend` (the recommended real solver
adapter, gated by `find_package(Jolt)` at configure).

## Library structure (target end-state)

```
include/threadmaxx_physics/
  threadmaxx_physics.hpp   # umbrella
  config.hpp               # solver options
  types.hpp                # PhysicsWorldId / BodyId / ShapeId / JointId
  body.hpp                 # BodyDesc / BodyState / BodyType
  shape.hpp                # ShapeDesc / ShapeType
  query.hpp                # RaycastRequest / SweepRequest / overlap
  step.hpp                 # simulation step interface
  sync.hpp                 # engine ↔ physics sync helpers
  constraints.hpp          # ConstraintDesc / ConstraintType
  character.hpp            # CharacterControllerDesc / moveCharacter
  contact.hpp              # ContactEvent / callback
  debug.hpp                # debug-draw data
  serialization.hpp        # scene state save/load
  backend.hpp              # IPhysicsBackend
  detail/
    broadphase.hpp
    filter.hpp
    island.hpp
    motion.hpp
    cache.hpp
src/threadmaxx_physics/
  PhysicsScene.cpp
  CharacterController.cpp
  backends/
    StubBackend.cpp        # deterministic; used by all tests
    JoltBackend.cpp        # gated by find_package(Jolt)
tests/physics/
  test_physics_*.cpp
bench/
  physics_*.cpp
```

## Batch P1 — Foundations (types + IPhysicsBackend + StubBackend)  ✅ landed 2026-06-08

**Goal**: data model, backend contract, and a deterministic
`StubBackend` that does no real physics but answers calls in a
testable way. The stub is the documented null implementation —
games can use it for "physics-disabled" mode, and tests use it as
the reference behavior.

**Test gate**:

- `test_physics_types_layout` — `BodyDesc` and `BodyState` are
  trivially copyable; PODs round-trip through memcpy.
- `test_physics_stub_create_destroy` — create world, create body,
  destroy body, destroy world; no asserts fire, no leaks.
- `test_physics_stub_step_noop` — step a dynamic body for 1
  second; `bodyState` reports no position change (stub doesn't
  integrate).
- `test_physics_backend_conformance` — same test driven through
  the `IPhysicsBackend*` interface; verifies the contract is
  callable without StubBackend specifics.

**Files**: `types.hpp`, `body.hpp`, `shape.hpp`, `backend.hpp`,
`config.hpp`, `src/backends/StubBackend.cpp`, four tests.

**Risks**: the backend interface is the long-term contract.
Locking it in early is correct — design notes §5.1 already
specifies it; just enforce that signature.

**Out of scope**: real stepping (P4), queries (P5).

## Batch P2 — Shape registry  ✅ landed 2026-06-08

**Goal**: `ShapeId` lifecycle. Create box/sphere/capsule/mesh
shapes through the backend; reference-count them across bodies.

**Test gate**:

- `test_physics_shape_create` — create a 1m³ box; `getDesc` returns
  the input descriptor.
- `test_physics_shape_share` — two bodies referencing the same
  ShapeId; destroying one body leaves the shape valid for the
  other; destroying both destroys the shape.
- `test_physics_shape_compound` — compound shape composed of
  multiple primitives; bounds match the union.

**Files**: extension to `shape.hpp`, extension to
`src/backends/StubBackend.cpp`.

**Out of scope**: convex hull / mesh shape generation (game-side
input format — game owns the cooking).

## Batch P3 — Body create / destroy / state sync  ✅ landed 2026-06-08

**Goal**: `BodyId` lifecycle + `BodyState` read-back. With Stub
this is just a memory-backed table; with real backends it's the
genuine simulation handle.

**Test gate**:

- `test_physics_body_lifecycle` — create body, read state, destroy
  body; second read returns nullopt.
- `test_physics_body_kinematic_teleport` — set position directly
  on a kinematic body; subsequent state read returns the new
  position.
- `test_physics_body_sync_batch` — `syncBodiesToGame` with 256
  body handles; output `BodyState` span aligned with input.

**Files**: extension to `body.hpp`, `sync.hpp`, extension to Stub.

**Out of scope**: world stepping (P4) — bodies are inert until P4.

## Batch P4 — World stepping (Stub kinematic-only)  ✅ landed 2026-06-08

**Goal**: `PhysicsScene::step(world, dt)` drives the backend's
`stepWorld`. Stub implements a **kinematic-only integrator**:
positions advance by `linearVelocity * dt`, orientations by
`angularVelocity * dt` composed as axis-angle. No collision, no
gravity. Real-backend tests gated by `find_package(Jolt)`.

**Test gate**:

- `test_physics_step_linear` — body at (0,0,0) with linearVel
  (1,0,0), step 1s → position (1,0,0) ± 1e-6.
- `test_physics_step_angular` — body with angularVel
  (0, π, 0), step 1s → orientation matches 180°-Y rotation
  composed from (0,0,0,1) identity.
- `test_physics_step_determinism` — same body, same input, two
  scenes run side-by-side step 60 ticks → final state identical
  byte-for-byte.

**Files**: `step.hpp`, extension to Stub.

**Risks**: the determinism gate is the strongest constraint for
the real backend (P9) — Jolt is deterministic only under specific
conditions (fixed timestep, no cross-thread interaction with the
solver). Document those constraints prominently.

**Out of scope**: gravity, collision response (Stub doesn't do
either; real-backend P9 will).

## Batch P5 — Queries (raycast / sweep / overlap)  ✅ landed 2026-06-08

**Goal**: synchronous queries against the world. Stub answers
queries against the kinematic body positions from P4 (no real
narrowphase — sphere-AABB tests against shape bounds).

**Test gate**:

- `test_physics_raycast_hit` — ray origin (-5,0,0), direction
  (1,0,0), hits a unit box at origin; hit position is on the box
  surface; normal points back toward the ray.
- `test_physics_raycast_miss` — ray pointing away from any body
  returns nullopt.
- `test_physics_raycast_layer_filter` — layerMask excluding the
  body's layer skips it.
- `test_physics_sweep_clear_path` — sphere sweep along a clear
  corridor returns no hit; sweep into a body returns the entry
  point.
- `test_physics_overlap` — overlap query at a body's position
  returns that body; query in empty space returns empty list.

**Files**: `query.hpp`, extension to Stub.

**Out of scope**: continuous collision detection (CCD) — toggled
via `BodyDesc::enableCCD` and forwarded to real backend in P9.

## Batch P6 — Constraints (joints)

**Goal**: fixed / hinge / slider / ball-socket / 6DOF constraints
between bodies. Stub records constraint descriptors but doesn't
enforce them; real backend (P9) does the work.

**Test gate**:

- `test_physics_constraint_create` — create a hinge between two
  bodies; `getConstraint` returns the input desc.
- `test_physics_constraint_destroy` — destroying either body
  invalidates the constraint without crashing.
- `test_physics_constraint_disable_self_collision` —
  `disableCollisionBetweenLinkedBodies = true` is forwarded to
  the backend's collision filter.

**Files**: `constraints.hpp`, extension to Stub.

**Out of scope**: motorized constraints, soft-body constraints.

## Batch P7 — Character controller

**Goal**: capsule-based character controller with step-up, slope
limit, and grounded-detect. Sits on top of the body / query APIs.

**Test gate**:

- `test_physics_character_move_flat` — character on a flat floor
  receiving forward intent moves forward at `maxMoveSpeed`.
- `test_physics_character_step_up` — character hitting an obstacle
  of height ≤ `stepHeight` climbs it; > `stepHeight` blocks.
- `test_physics_character_slope_limit` — slope ≤ `slopeLimit` is
  walkable; > slope is treated as a wall (slides off).
- `test_physics_character_grounded` — `grounded == true` on a
  floor; `grounded == false` mid-jump.

**Files**: `character.hpp`, `src/CharacterController.cpp`.

**Risks**: character controllers are notoriously fiddly. The Stub
needs enough geometry to make the test cases meaningful — ship a
simple AABB-floor-and-AABB-obstacle fixture in the test harness.

**Out of scope**: crouching, swimming, climbing — game-side state
machines on top of the controller.

## Batch P8 — Contact events

**Goal**: begin/end contact callbacks. Stub fires synthetic events
based on overlap state changes; real backend forwards solver
contacts.

**Test gate**:

- `test_physics_contact_begin` — two bodies moved into overlap
  fire a `began == true` event.
- `test_physics_contact_end` — moving them apart fires
  `ended == true`.
- `test_physics_contact_stable` — bodies in continuous overlap
  don't re-fire `began` events per tick.

**Files**: `contact.hpp`.

**Out of scope**: trigger volumes (often games want
overlap-without-collision-response — Stub treats triggers as
contact events with `impulse == 0`; real backend uses solver
hooks).

## Batch P9 — Jolt backend adapter

**Goal**: integrate the Jolt physics solver as the recommended
real backend. CMake `find_package(Jolt)` gates it; missing →
backend silently absent, but the StubBackend keeps the library
usable.

**Test gate**:

- `test_physics_jolt_smoke` (gated): create world, drop a sphere
  on a static plane, step 60 ticks; final position is below the
  starting height (gravity worked).
- `test_physics_jolt_conformance`: re-run a subset of the P1–P8
  test suite against JoltBackend. Allowed tolerances are documented
  per-test (real physics is not bit-deterministic against Stub).
- `bench/physics_jolt_bench.cpp`: 1024 dynamic boxes falling into
  a static ground; report step ms/tick at 60 Hz.

**Files**: `src/backends/JoltBackend.cpp`,
`tests/physics/jolt_*.cpp`. CMake gate code in
`src/threadmaxx_physics/CMakeLists.txt`.

**Risks**: ABI / version skew between Jolt versions. Pin a
specific Jolt commit / release tag in `cmake/Findjolt.cmake` and
document the upgrade path.

**Out of scope**: Bullet / PhysX adapters (same template, separate
batches if needed).

## v1.0 close-out criteria

- ✓ Every batch P1–P9 landed and tested.
- ✓ Stub backend passes all P1–P8 tests deterministically.
- ✓ Jolt backend (when built) passes the conformance subset and
  the smoke test.
- ✓ End-to-end demo (lives in `examples/physics_demo/` or wired
  into RPG demo D17) shows a character controller walking around a
  Jolt-backed scene with ground + obstacles + a few dynamic boxes.
- ✓ Docs: README, USER_GUIDE, MAINTAINER_GUIDE, plus a
  `BACKEND_PORTING_GUIDE.md` for future adapter authors.
- ✓ ctest 100% on `build/` and `build-werror/`.
- ✓ Version stamped at 1.0.0 in
  `include/threadmaxx_physics/version.hpp`.

## v1.x candidate batches (not blocking v1.0)

### v1.x — Bullet / PhysX adapters

Same template as JoltBackend. Add when a project actually needs
them (Bullet for source-available cross-platform; PhysX for
specific platform integration). Both have non-trivial license
considerations — the library code itself doesn't ship the solver
binaries; games pick.

### v1.x — Soft body / cloth

Out of scope for the whole library per design notes §1, but the
solver-agnostic backend pattern means a `JoltSoftBodyBackend`
could ship as a separate sibling library.

### v1.x — Read-only world snapshot for physics

Mentioned in DESIGN_NOTES §6.2 — physics consumes a snapshot of
the game world for filtering / triggers / kinematic queries.
Hooks ship in P3's sync.hpp, but the read-only snapshot pattern
itself depends on engine work (mostly already done via
`World::snapshot()`). Worth a future batch to make the integration
ergonomic.

### v1.x — Determinism profile

Document and test "deterministic mode" guarantees for Jolt
(single-threaded solver, fixed timestep, no FMA). Required for
lockstep netcode (see `threadmaxx_network` FUTURE_WORK).

### v1.x — GPU collision queries

Long-range bulk raycasts dispatched to GPU. Niche but useful for
gameplay queries like AI line-of-sight at scale.

### v1.x — Vehicle physics

Wheel constraints, suspension, tire friction model. Big topic;
likely a separate sibling library (`threadmaxx_vehicle`) rather
than part of the core physics surface.

## Out of scope for the whole library

Per DESIGN_NOTES §1 — none of this lands at any batch:

- Solver hardcoding in engine core
- Physics-owned ECS
- Renderer integration
- Navmesh generation
- Networking / rollback (lives in `threadmaxx_network`)
- Animation math
- Editor UI
- Implicit scene graph
- Hidden ownership of game objects
