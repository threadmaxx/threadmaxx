# `threadmaxx_physics` — Maintainer Guide

Internal documentation for engineers extending or debugging the
physics library. For external usage, see `USER_GUIDE.md`. For writing
a new backend adapter (Bullet / PhysX / custom), see
`BACKEND_PORTING_GUIDE.md`.

## Architecture overview

```
                ┌──────────────────────────────────────┐
                │  Consumer code                       │
                │  #include <threadmaxx_physics/...>   │
                └────────────────┬─────────────────────┘
                                 ▼
        ┌──────────────────────────────────────────────────┐
        │  Public surface (one header per concern)         │
        │                                                  │
        │   types.hpp       config.hpp     body.hpp        │
        │   shape.hpp       backend.hpp    step.hpp        │
        │   query.hpp       sync.hpp       constraints.hpp │
        │   contact.hpp     character.hpp  version.hpp     │
        │   stub_backend.hpp                jolt_backend.hpp │
        └────────────────┬─────────────────────────────────┘
                         ▼
            ┌────────────────────────────────────────┐
            │  IPhysicsBackend abstract class        │
            │  (the contract every adapter targets)  │
            └────────────────┬───────────────────────┘
                             ▼
       ┌─────────────────────────────────────────────────┐
       │  src/threadmaxx_physics/                        │
       │    backends/StubBackend.cpp    (always built)   │
       │    backends/JoltBackend.cpp    (gated)          │
       │    CharacterController.cpp     (free-standing)  │
       └─────────────────────────────────────────────────┘
                             ▼
            ┌────────────────────────────────────┐
            │  threadmaxx::threadmaxx (math PODs)│
            │  Vec3, Quat — only public dep      │
            └────────────────────────────────────┘
```

The library is a `STATIC` archive (`libthreadmaxx_physics.a`). The
CMake target `threadmaxx::physics` carries the include directory,
`cxx_std_20`, and a transitive link to `threadmaxx::threadmaxx`
(math PODs only — no engine subsystems).

When the Jolt gate is on, the static archive additionally privately
links the Jolt static library. `JoltBackend.cpp` is the only
translation unit that includes Jolt headers; the public headers never
reference any Jolt symbol.

## The `IPhysicsBackend` contract

`IPhysicsBackend` has 18 pure virtuals grouped into seven categories:

1. **World lifecycle** — `createWorld` / `destroyWorld`.
2. **Shape lifecycle** — `createShape` / `destroyShape` /
   `getShapeDesc` / `getShapeAabb`.
3. **Body lifecycle** — `createBody` / `destroyBody` /
   `getBodyState` / `setBodyTransform`.
4. **Stepping** — `stepWorld`.
5. **Batch sync** — `syncBodiesToGame`.
6. **Queries** — `raycast` / `sweep` / `overlap`.
7. **Constraints** — `createConstraint` / `destroyConstraint` /
   `getConstraint`.
8. **Contacts** — `setContactCallback`.

A new backend must implement every one of these. The contract is
explicit per-method in `backend.hpp` (Doxygen `@brief` plus
`@thread_safety` / `@pre` where applicable). Bugs in adapter code
usually fall into one of these traps:

- Forgetting that `BodyId`s are PER-WORLD scoped (the same numeric
  value can refer to different bodies across worlds).
- Forgetting that `ShapeId`s are BACKEND-WIDE (one registry for the
  whole adapter; Jolt actually scopes them per-world too because
  Jolt's `Shape` is process-wide; the adapter mediates).
- Returning stale `BodyState` after a `destroyBody` instead of
  `nullopt` (the contract requires `nullopt` on stale generation).
- Failing to fire End-on-destroy for active contacts before bumping a
  body's generation.

Lifecycle is managed by `std::unique_ptr<IPhysicsBackend>`. A backend
factory (`makeStubBackend` / `makeJoltBackend`) returns a new
instance; the host engine owns it for the lifetime of the simulation.

## Body / shape lifecycle invariants

Across both bundled backends:

- **`BodyId`** carries `(slot, generation)`. Destroying a body
  bumps its generation; the slot returns to a free list.
- **`ShapeId`** is refcounted. `createShape` returns 1 reference;
  every `createBody` that references the shape adds 1; every
  `destroyBody` releases 1; `destroyShape` removes the explicit
  "user holds it" reference. The slot frees + generation bumps when
  refcount hits zero.
- **Compound shapes** add 1 to each child's refcount when the parent
  is created; releasing the compound releases each child by 1.
- **Reserved zero**: every id type uses `value == 0` as "invalid".
  Failing `create*` calls return this; `if (id)` is the idiomatic
  null check.
- **Generation comparison** is the canonical "is this id still alive"
  test in both backends. Workload-side caches that hold stale ids
  must check via `getBodyState` / `getShapeDesc` / `getConstraint`
  before use.

## Hot-path allocation discipline

Adapter `stepWorld` and query implementations must not allocate per
tick — the steady-state cost reported in `bench/physics_jolt_bench`
depends on this. The discipline:

- **`overlap`** appends to a caller-owned `std::vector<BodyId>&`;
  the helper `overlapBodies` is the buffer-reuse path the engine
  should use.
- **`syncBodiesToGame`** writes into a caller-owned span; never
  allocates.
- **`raycast` / `sweep`** return by value (POD) — no allocation.
- **Stub backend** reuses a `vector<bool> activePairs_` scratch for
  contact event diffing; cleared in place each tick.
- **Jolt backend** uses Jolt's per-world `TempAllocator` for the
  solver's per-step scratch. Allocation happens in Jolt internally
  and is the documented design (a single bump allocator reset each
  tick).

Setup paths (`createWorld`, `createBody`, `createShape`,
`createConstraint`) MAY allocate — they're called at level load and
on rare gameplay events, not per tick.

## Stub backend internals

`src/threadmaxx_physics/backends/StubBackend.cpp` is the reference
implementation and the conformance baseline. Key structures:

- `WorldSlot { generation, alive, config, bodies, constraints, callback, ... }`
  — table of `WorldSlot`s indexed by `(slot, generation)` encoded
  into `PhysicsWorldId::value`.
- `ShapeSlot { generation, alive, refCount, desc, aabb }` —
  process-wide table.
- `BodySlot { generation, alive, type, layer, shapeIds, state, desc }` —
  per-world.
- `ConstraintSlot { generation, alive, desc }` — per-world.

`stepWorld` does kinematic integration: position advance + axis-angle
angular composition for kinematic + dynamic bodies. No collision
response, no gravity (stub never integrates forces — see DESIGN_NOTES
§5.1).

Contact-event diffing uses `activePairs_`: a flat `std::vector<bool>`
indexed by `(bodyAIndex * bodyCount) + bodyBIndex`. Each `stepWorld`
walks the AABB pair set, computes which pairs are currently
overlapping, diffs against the previous tick's active set, and fires
Begin / End events for the differences. Cost is `O(n²)`; the stub is
not a perf target.

## Jolt backend internals

`src/threadmaxx_physics/backends/JoltBackend.cpp` is the real-solver
adapter. The TU starts with
`#if !defined(THREADMAXX_PHYSICS_HAS_JOLT)` returning a stub
`makeJoltBackend() { return nullptr; }`; the `#else` arm contains the
full adapter. This keeps the gate-off build path identical to the
gate-on signatures.

Key structures (mirroring the Jolt object model):

- `BPLayerInterfaceImpl` + `ObjectVsBPLayerFilterImpl` +
  `ObjectLayerPairFilterImpl` — Jolt's broadphase layer scheme.
  Two-layer split (`STATIC=0`, `MOVING=1`); static-vs-static skipped.
- `WorldContactListener` — Jolt's `ContactListener` subclass.
  `OnContactAdded` → Begin, `OnContactRemoved` → End. Persist
  callbacks are silently dropped per the contact.hpp contract.
- `WorldSlot { generation, alive, config, PhysicsSystem, TempAllocator,
  JobSystem, layer interfaces, ContactListener, GroupFilterTable,
  ContactCallback, bodies + constraints + their free lists }`.

Global Jolt init (`RegisterDefaultAllocator`, `Factory::sInstance`,
`RegisterTypes`) runs once under `std::call_once(g_initFlag, ...)`.
Per-world resources (PhysicsSystem, allocator, job system) live on
the `WorldSlot`. Single-precision builds (default) have
`JPH::RVec3 == JPH::Vec3` — the adapter defines `toJolt(Vec3)` and
`fromJolt(JPH::Vec3)` only; flipping to double precision would
require explicit RVec3 overloads.

Layer filtering is post-broadphase: our 32-bit `BodyDesc::layer` is
QUERY-side filter only; broadphase still uses the two-layer Jolt
scheme. Query results are post-filtered by
`(1u << bodyLayer) & request.layerMask`.

Determinism profile (USER_GUIDE.md): Jolt's
`CROSS_PLATFORM_DETERMINISTIC = ON` is set by our FetchContent path.
Combined with `allowSolverThreading = false` and a fixed `dt`, two
side-by-side instances reach bit-identical state. Tested by the
conformance gate.

## Character controller internals

`src/threadmaxx_physics/CharacterController.cpp` sits ON TOP of the
backend's `sweep` + `raycast`. It does not register a body — the
character is invisible to its own queries. Step order in `move()`:

1. Compute horizontal velocity from `input.moveIntent` and
   `desc.maxMoveSpeed`.
2. Apply jump (if requested + grounded); integrate gravity (if not
   grounded).
3. Horizontal sweep — on hit, attempt step-up by retrying at
   `position + (0, stepHeight, 0)`; commit step on clear sweep + new
   floor found beneath.
4. Apply vertical delta from gravity / jump.
5. Ground probe — downward sphere sweep; snap to contact if found
   within range, set `grounded`.

`isSurfaceWalkable(desc, groundNormal)` is the standalone helper for
game-side slope checks (HUD logic, jump gating). The controller's
internal slope check uses the same math.

The controller is a `move`-able value type. Copying is deleted (the
backend pointer would alias without ref semantics). One controller
per game character is the expected pattern.

## Adding a new constraint type

Same recipe as the animation library's "add a new graph node":

1. **Append** the enum tag in `constraints.hpp`:
   ```cpp
   enum class ConstraintType : std::uint8_t {
       Fixed = 0, Hinge = 1, Slider = 2,
       BallSocket = 3, SixDOF = 4,
       MyNewType,  // append
   };
   ```
   Existing values stay stable — never reorder.

2. **Document** the per-type field interpretation in the
   `ConstraintDesc` doc block. Each constraint type reads a specific
   subset of `linearLimits` / `angularLimits` / `localAxisA/B`.

3. **Extend** `StubBackend::createConstraint`'s switch arm. The stub
   only records the descriptor verbatim; if the new type has stricter
   field validation, do it here.

4. **Extend** `JoltBackend::createConstraint`'s switch arm. Map to
   Jolt's `JPH::TwoBodyConstraintSettings` subclass:
   `JPH::FixedConstraintSettings`, `JPH::HingeConstraintSettings`,
   `JPH::SliderConstraintSettings`, `JPH::PointConstraintSettings`
   (BallSocket), `JPH::SixDOFConstraintSettings`. Set
   `EConstraintSpace::LocalToBodyCOM`. Apply per-axis limits via
   `SetLimitedAxis`. Add the new arm.

5. **Test** in `tests/physics/test_physics_constraint_*.cpp`. The
   trio: create round-trip (`getConstraint` returns matching desc),
   destroy (one of the bodies → returns `nullopt`), and the
   self-collision-disable check.

The compiler will warn about a missing switch arm via `-Wswitch`
once the enum tag exists.

## Adding a new shape type

1. Append the `ShapeType` enum tag in `shape.hpp`.
2. Document which existing fields are read for the new type (or
   extend `ShapeDesc` with new fields; this is BREAKING — bump
   MAJOR).
3. Extend `StubBackend::createShape`'s switch arm — compute the
   shape's local-space AABB. The stub's queries use AABB only.
4. Extend `JoltBackend::createShape`'s switch arm — construct the
   matching `JPH::ShapeSettings` subclass and store the
   `JPH::ShapeRefC` in the `ShapeSlot`.
5. Test in `tests/physics/test_physics_shape_*.cpp` — verify
   round-trip (`getShapeDesc` returns matching POD).

## Adding a new backend

Read `BACKEND_PORTING_GUIDE.md`. The short version: write a
`MyAdapter.cpp` under `src/threadmaxx_physics/backends/`, implement
all 18 virtuals, expose a `make<MyAdapter>Backend()` factory in
`include/threadmaxx_physics/<myadapter>_backend.hpp`, gate the build
on `find_package(<MyAdapter>)`.

## Versioning + ABI

The library produces a static archive; downstream callers recompile
against the headers, so source-ABI is what matters.

- **Public POD layouts** (`PhysicsConfig`, `BodyDesc`, `BodyState`,
  `ShapeDesc`, `ShapeAabb`, `RaycastRequest`, `RaycastHit`,
  `SweepRequest`, `SweepHit`, `OverlapRequest`, `ConstraintDesc`,
  `ConstraintLimit`, `CharacterControllerDesc`, `CharacterInput`,
  `CharacterState`, `ContactEvent`, `FixedStepAccumulator`) are
  stable. Layout changes are breaking (bump MAJOR).
- **`IPhysicsBackend` virtual signatures** are stable. Adding new
  virtuals at the END of the class is technically additive but breaks
  every out-of-tree adapter — treat it as MAJOR.
- **Enum values** (`BodyType`, `ShapeType`, `ConstraintType`,
  `ContactPhase`) — existing values stable; appending new values at
  the end is additive (MINOR).
- **`detail::*`** (none in v1.0 — reserved for future) is internal.
  Consumers should not include `detail/` headers directly.
- **Backend factories** (`makeStubBackend`, `makeJoltBackend`,
  `joltBackendAvailable`) are stable. Adding new factories is MINOR.

When evolving:

- Append new shape / constraint types via the workflows above. Never
  reorder.
- Add new public functions via overloads or new headers. Removing is
  breaking — deprecate first, remove in the next major.
- Pinned Jolt tag bumps (`THREADMAXX_PHYSICS_JOLT_TAG`) follow the
  upgrade recipe in `BACKEND_PORTING_GUIDE.md` (rerun smoke +
  conformance; adjust tolerances if drift crosses bounds).

## Library version (`version.hpp`)

```cpp
#define THREADMAXX_PHYSICS_VERSION_MAJOR 1
#define THREADMAXX_PHYSICS_VERSION_MINOR 0
#define THREADMAXX_PHYSICS_VERSION_PATCH 0
#define THREADMAXX_PHYSICS_VERSION (MAJOR*10000 + MINOR*100 + PATCH)

constexpr const char* version_string() noexcept;  // → "1.0.0"
```

When bumping, update **both** the macros AND the string literal
returned by `version_string()`. Also append a section to
`CHANGELOG.md`.

## Testing strategy

Three layers, all in `tests/physics/`:

1. **Unit / API surface** — one executable per public concept
   (types layout, shape create / share / compound, body lifecycle /
   teleport / sync batch, step linear / angular / determinism, every
   query flavor, every constraint variant, character move / step-up /
   slope / grounded, contact begin / end / stable).
2. **Backend conformance** — `test_physics_backend_conformance.cpp`
   exercises the API through the abstract `IPhysicsBackend*` pointer.
   Re-runnable against any future backend.
3. **Jolt conformance + smoke (gated)** —
   `test_physics_jolt_conformance.cpp` and
   `test_physics_jolt_smoke.cpp`. Only register with CTest when
   `THREADMAXX_PHYSICS_HAS_JOLT` is true.

All tests use the project-wide `Check.hpp` harness — one executable
per test, non-zero exit means failure.

### Werror tree

The library compiles clean under
`-DTHREADMAXX_WARNINGS_AS_ERRORS=ON` (which adds `-Wsign-conversion
-Wconversion -Wold-style-cast -Wshadow`). The build-werror tree is
the discipline gate when touching public surface.

```bash
cmake -B build-werror -DTHREADMAXX_WARNINGS_AS_ERRORS=ON
cmake --build build-werror -j
(cd build-werror && ctest -R '^physics\.' --output-on-failure)
```

Jolt's internal headers are pulled in as `SYSTEM` includes so our
`-Wconversion / -Wshadow / -Wold-style-cast` set doesn't flag every
line of Jolt's internals (see `src/threadmaxx_physics/CMakeLists.txt`
for the `target_include_directories(... SYSTEM ...)` line).

## Repository layout (engineer-facing)

```
include/threadmaxx_physics/
├── README.md                 # Top-level overview
├── CHANGELOG.md              # Per-release notes
├── DESIGN_NOTES.md           # Original spec (don't edit unless re-scoping)
├── FUTURE_WORK.md            # Batch-by-batch landed work + v1.x candidates
├── USER_GUIDE.md             # User-facing docs
├── MAINTAINER_GUIDE.md       # This file
├── BACKEND_PORTING_GUIDE.md  # How to write a new backend
├── threadmaxx_physics.hpp    # Umbrella include
├── version.hpp               # Library version macros + version_string()
├── types.hpp                 # PhysicsWorldId / BodyId / ShapeId / JointId
├── config.hpp                # PhysicsConfig
├── body.hpp                  # BodyType / BodyDesc / BodyState
├── shape.hpp                 # ShapeType / ShapeDesc / ShapeAabb
├── backend.hpp               # IPhysicsBackend abstract base
├── stub_backend.hpp          # makeStubBackend factory
├── jolt_backend.hpp          # makeJoltBackend + joltBackendAvailable
├── step.hpp                  # stepScene / stepSceneFixed / FixedStepAccumulator
├── sync.hpp                  # syncBodyStates
├── query.hpp                 # raycast / sweep / overlapBodies + requests
├── constraints.hpp           # ConstraintType / ConstraintDesc / createConstraint
├── contact.hpp               # ContactPhase / ContactEvent / setContactCallback
└── character.hpp             # CharacterController + isSurfaceWalkable

src/threadmaxx_physics/
├── CMakeLists.txt            # Jolt gate + STATIC archive
├── CharacterController.cpp
└── backends/
    ├── StubBackend.cpp       # Always built — deterministic reference
    └── JoltBackend.cpp       # Gated — real solver adapter

tests/physics/
├── CMakeLists.txt
├── test_physics_types_layout.cpp
├── test_physics_stub_*.cpp                  # P1
├── test_physics_backend_conformance.cpp     # P1
├── test_physics_shape_*.cpp                 # P2
├── test_physics_body_*.cpp                  # P3
├── test_physics_step_*.cpp                  # P4
├── test_physics_raycast_*.cpp               # P5
├── test_physics_sweep_*.cpp                 # P5
├── test_physics_overlap.cpp                 # P5
├── test_physics_constraint_*.cpp            # P6
├── test_physics_character_*.cpp             # P7
├── test_physics_contact_*.cpp               # P8
├── test_physics_jolt_smoke.cpp              # P9 (gated)
└── test_physics_jolt_conformance.cpp        # P9 (gated)

bench/
└── physics_jolt_bench.cpp                   # 1024-box throughput (gated)

examples/
└── physics_demo/                            # End-to-end scene
    ├── CMakeLists.txt
    └── main.cpp
```

## Common pitfalls

### "The Jolt build flags every line of `JoltBackend.cpp` with `-Wconversion`"

Jolt internals aren't wrap-clean for our strict warning set. We pull
Jolt's `INTERFACE_INCLUDE_DIRECTORIES` and pass them via
`target_include_directories(... SYSTEM PRIVATE ...)` so they're
treated as system headers and exempted from `-Wconversion /
-Wshadow / -Wold-style-cast`. The adapter's own code still has to
comply.

### "Adding a new built-in body field breaks every adapter"

Yes — `BodyDesc` is the create-time contract. Bump MAJOR for layout
changes; document the migration in `CHANGELOG.md`. Consider a
side-band registration path (e.g., a separate `BodyAdvancedDesc` POD
threaded through a different create overload) if the new field is
adapter-optional.

### "`getBodyState` returns garbage after `destroyBody`"

It should return `nullopt`. If it returns the stale state, the
adapter's generation check is broken. Recipe: on destroy, bump
`slot.generation`; on lookup, compare `slot.generation ==
id.encodedGeneration` before returning the state.

### "Contact End event never fires for one of the bodies after destroy"

Check the destroy-cascade order in the adapter. The contract:
End-on-destroy fires BEFORE the generation bump, so the callback
sees the still-valid id. Doing it the other way around means the
callback sees a stale id and skips the lookup.

### "Stub-backend tests pass but Jolt conformance fails on the same scene"

Jolt is a real solver — bit-equality with the Stub is NOT the gate;
behavioral equivalence within documented tolerances is. The tolerances
are per-test in `test_physics_jolt_conformance.cpp` — adjust there if
a tighter assertion is hitting solver drift.

### "Same Jolt scene, same seed, two runs diverge"

You've broken the determinism profile. Check:
- `PhysicsConfig::allowSolverThreading = false`
- Driving with a CONSTANT `dt` (no wall-clock variance reaching
  `stepWorld`)
- Jolt was built with `CROSS_PLATFORM_DETERMINISTIC = ON` (the
  FetchContent default; check `CMakeCache.txt` if you suspect
  override)

### "`createConstraint` returns 0 for a valid pair"

Check the same-body case: a body cannot be constrained to itself.
The factory returns 0 when `desc.bodyA == desc.bodyB`. Also check
generation freshness on both bodies.

### "Character controller falls through the floor on the first frame"

The controller computes `grounded` eagerly at construction via a
downward sweep. If the floor body is created AFTER the controller,
the initial sweep misses and the controller starts falling. Create
the world geometry before the controller.

## See also

- `DESIGN_NOTES.md` — original spec.
- `FUTURE_WORK.md` — batch-by-batch landed work + open follow-ups.
- `USER_GUIDE.md` — user-facing API reference.
- `BACKEND_PORTING_GUIDE.md` — full recipe for new adapters.
- `/CLAUDE.md` (repo root) — meta-instructions for AI-assisted
  development of `threadmaxx` and its sibling libraries.
