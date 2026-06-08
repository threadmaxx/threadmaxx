# Changelog

All notable changes to `threadmaxx_physics` are recorded here. The
format follows [Keep a Changelog](https://keepachangelog.com/); the
project adheres to [Semantic Versioning](https://semver.org/). See
[`MAINTAINER_GUIDE.md`](MAINTAINER_GUIDE.md) for the bump rules.

## [1.0.0] — 2026-06-08 — Production-ready close-out

### Added

- **`version.hpp`** — `THREADMAXX_PHYSICS_VERSION_MAJOR/MINOR/PATCH`
  macros, packed `THREADMAXX_PHYSICS_VERSION` integer, and
  `version_string()` for runtime logging.
- **`README.md`** — top-level library overview with quick start +
  doc cross-references.
- **`USER_GUIDE.md`** — user-facing documentation (public surface
  inventory, two backend choices, determinism profile, integration
  patterns, perf expectations).
- **`MAINTAINER_GUIDE.md`** — internal documentation (architecture,
  `IPhysicsBackend` contract, Stub + Jolt internals, lifecycle
  invariants, how to add new shape / constraint types).
- **`BACKEND_PORTING_GUIDE.md`** — step-by-step recipe for new
  `IPhysicsBackend` adapters (Bullet / PhysX / custom solver).
  Includes the 18-virtual checklist and a shipping-ready checklist.
- **`CHANGELOG.md`** — this file.
- **`examples/physics_demo/`** — end-to-end demo: character
  controller walking across an obstacle course with dynamic boxes
  raining onto a ground plate and contact-event callbacks
  printing the begin / end stream. Prefers `JoltBackend` when
  available, falls back to `StubBackend` automatically.

### Closed-out roadmap

`FUTURE_WORK.md` v1.0 closure section documents what's shipped vs.
deferred to v1.x candidate batches (Bullet / PhysX adapters, soft
body / cloth, vehicle physics, GPU collision queries, deterministic-
profile docs / lockstep gates).

---

## [0.9.0] — 2026-06-08 — Batch P9 — Jolt backend adapter

### Added

- **`jolt_backend.hpp`** + **`backends/JoltBackend.cpp`** — real-
  solver adapter against Jolt Physics. Gated by
  `find_package(Jolt CONFIG QUIET)` (primary) or
  `THREADMAXX_PHYSICS_FETCH_JOLT=ON` (FetchContent fallback). Pinned
  to upstream `v5.3.0`.
- **`joltBackendAvailable()`** — `constexpr` flag exposing the
  library-side gate.
- **`test_physics_jolt_smoke.cpp`** — sphere-on-plate gravity
  smoke test, gated.
- **`test_physics_jolt_conformance.cpp`** — P3 / P4 / P5 / P6 / P8
  invariants re-run against Jolt with documented per-check
  tolerances, gated.
- **`bench/physics_jolt_bench.cpp`** — 1024 dynamic boxes / 60 ticks
  / 60 Hz / single-threaded. Reports ~0.8 ms/tick avg on the
  reference dev box.

### Engineering notes

- Two-broad-phase-layer scheme (`STATIC=0`, `MOVING=1`); static-vs-
  static skipped at the broadphase. Layer mask filtering happens
  post-broadphase against our 32-bit `BodyDesc::layer`.
- `CROSS_PLATFORM_DETERMINISTIC=ON` is the default in our
  FetchContent path. Combined with `allowSolverThreading = false`
  and a fixed `dt`, two side-by-side instances reach bit-identical
  state.
- Global Jolt init (`RegisterDefaultAllocator`, `Factory::sInstance`,
  `RegisterTypes`) runs once via `std::call_once`.
- Jolt's internal headers are pulled in as `SYSTEM` includes so our
  `-Wconversion / -Wshadow / -Wold-style-cast` set doesn't flag
  every line.

---

## [0.8.0] — 2026-06-08 — Batch P8 — Contact events

### Added

- **`contact.hpp`** — `ContactPhase { Begin, End }`, `ContactEvent`
  POD with canonicalized `bodyA.value < bodyB.value`,
  `ContactCallback`, `setContactCallback` / `clearContactCallback`.
- Stub backend contact diffing via `activePairs_` flat scratch; AABB-
  pair overlap drives Begin / End transitions.
- End-on-destroy cascade — `destroyBody` fires End events for every
  active pair touching the body BEFORE bumping its generation.
- Three test executables: `test_physics_contact_begin`,
  `test_physics_contact_end`, `test_physics_contact_stable`.

### Deferred

- Persist callbacks — explicitly NOT emitted. Game code that wants
  per-tick contact processing runs `overlapBodies` on its own
  schedule.
- Trigger volumes — overlap-without-collision-response is treated by
  Stub as contact events with `impulse == 0`; real backends use
  solver-specific hooks.

---

## [0.7.0] — 2026-06-08 — Batch P7 — Character controller

### Added

- **`character.hpp`** + **`CharacterController.cpp`** — capsule-based
  controller with step-up, slope limit, ground detect, and gravity
  integration. Built ON TOP of `IPhysicsBackend::sweep` + `raycast`
  rather than registering as a body.
- `isSurfaceWalkable(desc, normal)` standalone helper for game-side
  slope checks.
- Four test executables: `test_physics_character_move_flat`,
  `test_physics_character_step_up`,
  `test_physics_character_slope_limit`,
  `test_physics_character_grounded`.

### Deferred

- Crouching / swimming / climbing — game-side state machines on top
  of the controller.

---

## [0.6.0] — 2026-06-08 — Batch P6 — Constraints (joints)

### Added

- **`constraints.hpp`** — five constraint types (`Fixed`, `Hinge`,
  `Slider`, `BallSocket`, `SixDOF`) with per-axis linear / angular
  limits and `disableCollisionBetweenLinkedBodies`.
- `createConstraint` / `destroyConstraint` / `getConstraint` free
  helpers + the matching `IPhysicsBackend` virtuals.
- Destroy cascade — `getConstraint` returns `nullopt` when EITHER
  coupled body has been destroyed, without the host needing to
  release the joint explicitly first.
- Three test executables: `test_physics_constraint_create`,
  `test_physics_constraint_destroy`,
  `test_physics_constraint_disable_self_collision`.

### Deferred

- Motorized constraints (driven joints) — v1.x.
- Soft-body constraints — out of scope for the whole library.

---

## [0.5.0] — 2026-06-08 — Batch P5 — Queries (raycast / sweep / overlap)

### Added

- **`query.hpp`** — `RaycastRequest` / `RaycastHit`,
  `SweepRequest` / `SweepHit`, `OverlapRequest` PODs, plus the
  `raycast` / `sweep` / `overlapBodies` free helpers.
- 32-bit `layerMask` on every request; default `0xFFFFFFFF` matches
  every layer.
- Stub backend queries against the world-space AABB of each body's
  attached shapes (rotation ignored — no true OBB narrowphase).
- Five test executables: `test_physics_raycast_hit`,
  `test_physics_raycast_miss`,
  `test_physics_raycast_layer_filter`,
  `test_physics_sweep_clear_path`, `test_physics_overlap`.

### Deferred

- CCD enforcement — `BodyDesc::enableCCD` is wired through to the
  Stub but does not change query semantics (no real narrowphase to
  CCD against); real backend (P9) honors it.

---

## [0.4.0] — 2026-06-08 — Batch P4 — World stepping

### Added

- **`step.hpp`** — `stepScene` (single world, variable `dt`),
  `stepScenes` (multi-world span), `stepSceneFixed`
  (`FixedStepAccumulator`-driven fixed-step pattern).
- Stub backend kinematic integration:
  `position += linearVelocity * dt` plus angular composition via
  axis-angle. No collision response, no gravity.
- Three test executables: `test_physics_step_linear`,
  `test_physics_step_angular`,
  `test_physics_step_determinism` (bit-stable across two
  side-by-side Stub instances).

### Engineering notes

- The determinism profile (single-threaded solver + fixed timestep +
  no FMA divergence) becomes the explicit invariant the P9 Jolt
  adapter inherits.

---

## [0.3.0] — 2026-06-08 — Batch P3 — Body lifecycle + state sync

### Added

- **`body.hpp`** (P1 deferred fields filled in) +
  **`sync.hpp`** — `BodyDesc` create-time blueprint, `BodyState`
  per-tick read-back, `syncBodyStates` span and allocating overloads.
- `setBodyTransform` — kinematic teleport that writes BOTH the desc
  and the live state.
- Three test executables: `test_physics_body_lifecycle`,
  `test_physics_body_kinematic_teleport`,
  `test_physics_body_sync_batch`.

---

## [0.2.0] — 2026-06-08 — Batch P2 — Shape registry

### Added

- **`shape.hpp`** — `ShapeType` enum (`Box`, `Sphere`, `Capsule`,
  `ConvexHull`, `Mesh`, `Compound`), `ShapeDesc` POD, `ShapeAabb`.
- Stub backend refcounted shape table; compounds add 1 ref per
  child. Deferred-destroy contract pinned by tests.
- Three test executables: `test_physics_shape_create`,
  `test_physics_shape_share`, `test_physics_shape_compound`.

### Deferred

- Convex hull cooking (game-side input format) — backends consume
  pre-cooked data.
- Per-child local transforms in `Compound::children` — flat
  vector for v1.0; transforms in v1.x.

---

## [0.1.0] — 2026-06-08 — Batch P1 — Foundations

### Added

- **`types.hpp`** — `PhysicsWorldId`, `BodyId`, `ShapeId`, `JointId`
  trivially-copyable id PODs.
- **`config.hpp`** — `PhysicsConfig` (timestep, sub-step cap, gravity,
  threading toggle).
- **`backend.hpp`** — `IPhysicsBackend` abstract class with the full
  18-virtual surface (signatures present from day one; later batches
  fill in implementations).
- **`stub_backend.hpp`** + **`backends/StubBackend.cpp`** —
  deterministic baseline implementation; P1 ships with `stepWorld`
  as a no-op.
- **`threadmaxx_physics.hpp`** umbrella header.
- Top-level CMake target `threadmaxx::physics` (STATIC archive).
- Four test executables: `test_physics_types_layout`,
  `test_physics_stub_create_destroy`,
  `test_physics_stub_step_noop`,
  `test_physics_backend_conformance`.

---

## Pre-v1: Design phase

The library was scoped in `DESIGN_NOTES.md` before any code landed.
`FUTURE_WORK.md` broke that scope into the nine P-batches above; each
batch landed as a single coherent change with its own test gate. See
`FUTURE_WORK.md` for per-batch retrospectives.
