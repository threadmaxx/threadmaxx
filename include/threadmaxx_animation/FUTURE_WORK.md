# `threadmaxx_animation` — batch plan

Sibling-library implementation plan. `DESIGN_NOTES.md` is the
authoritative spec; this doc breaks it down into shippable
test-driven batches.

Status: **A1 landed (2026-06-07)**. A2–A8 are 📋 planned. Sequencing
follows the §9 "implementation order" of the design notes, regrouped
into shippable units that each carry their own tests.

## Conventions

Each batch is independently shippable:

- **Goal** — what the batch accomplishes in one sentence.
- **Test gate** — assertions that prove the batch landed.
- **Files** — what's added / modified.
- **Risks** — what could break.
- **Out of scope** — explicitly deferred to a later batch.

Batches start red, go green, then refactor. The library produces a
single static library `threadmaxx::animation` plus its public
headers under `include/threadmaxx_animation/`. Detail-namespace code
under `detail/` is implementation surface and may churn between
batches. Public headers are the contract.

When a batch lands, this doc gets edited in place (mark ✅ landed,
add per-batch retrospective notes — the same pattern the simd
library follows). Don't delete batch sections after they ship;
they're the historical record.

## Library structure (target end-state)

```
include/threadmaxx_animation/
  threadmaxx_animation.hpp   # umbrella include
  types.hpp                  # SkeletonId / ClipId / GraphId / PoseId / JointId
  skeleton.hpp               # SkeletonDesc / Joint / SkeletonRef
  clip.hpp                   # ClipDesc / ClipSample / EventTrackEvent
  pose.hpp                   # JointPose / Pose / PoseSpan / PoseBuffer
  graph.hpp                  # AnimationGraph, GraphNodeId, NodeType
  blend.hpp                  # layered + additive blend helpers
  ik.hpp                     # IKTarget / IKChain / solveIK
  warp.hpp                   # WarpRequest / applyWarp
  cloth.hpp                  # attachment hooks
  retarget.hpp               # skeleton retargeting helpers
  eval.hpp                   # Animator / EvalContext / EvalResult
  serialization.hpp          # save/load
  diagnostics.hpp            # debug poses, timing, validation
  detail/
    hash.hpp
    curve_eval.hpp
    pose_math.hpp
    graph_eval.hpp
    job_batch.hpp
src/threadmaxx_animation/
  Registry.cpp
  Animator.cpp
  GraphEval.cpp
  IK.cpp
  Warp.cpp
  Retarget.cpp
tests/animation/
  test_animation_*.cpp       # one binary per concern
bench/
  animation_*.cpp            # opt-in via -DTHREADMAXX_BUILD_BENCHMARKS=ON
```

## Batch A1 — Foundations (registry + pose math) ✅ landed 2026-06-07

**Goal**: header-only skeleton/clip registry, a usable `PoseBuffer`,
and the per-joint compose/blend primitives in `detail/pose_math.hpp`.
First end-to-end test of the data model. No graph or animator yet.

**Test gate**:

- `test_animation_skeleton` — register a 3-joint chain
  (root → spine → head); `getSkeleton` round-trip preserves bind
  poses; `isValid` rejects stale ids.
- `test_animation_clip` — register a clip with 4 keyframes; metadata
  round-trips through `getClip`; duration matches input.
- `test_animation_pose_buffer` — resize behavior; `localPose()` span
  size matches; multiple buffers don't alias.
- `test_animation_pose_math` — per-joint `compose(parent, local)`
  matches manual quaternion composition; `lerp_pose` at α=0/1 hits
  the endpoints; weighted blend of two poses (50/50) on a 4-joint
  rig matches scalar reference within 1e-6.

**Files**: `types.hpp`, `skeleton.hpp`, `clip.hpp`, `pose.hpp`,
`detail/pose_math.hpp`, umbrella `threadmaxx_animation.hpp`,
`src/Registry.cpp`, four test executables.

**Risks**: per-joint memory layout (AoS `JointPose`) may need to
flip to SoA later for SIMD-friendly blend kernels; lock in the
public POD shape early but keep `detail/pose_math.hpp` free to
restructure internally.

**Out of scope**: clip sampling (A2), graph (A3), IK (A5), batch
evaluation (A7).

### A1 retrospective (2026-06-07)

- **Shipped**: `types.hpp`, `pose.hpp`, `skeleton.hpp`, `clip.hpp`,
  `registry.hpp` (small plan extension — DESIGN_NOTES §5.1 puts
  `AnimationRegistry` here without naming the header), umbrella
  `threadmaxx_animation.hpp`, `detail/pose_math.hpp`,
  `src/threadmaxx_animation/Registry.cpp`. Top-level CMake gains
  `THREADMAXX_BUILD_ANIMATION` (default ON) + static lib
  `threadmaxx::animation`. Install rules mirror simd.
- **Mat4 deferral**: `Joint::bindLocal` is `JointPose`, not `Mat4`.
  DESIGN_NOTES §4.1 specifies Mat4; we use the JointPose TRS form
  for symmetry with the rest of the data model and to keep the lib
  free of a Mat4 dependency until a real consumer (A5 IK, retarget)
  asks for one. Documented in `skeleton.hpp`.
- **Composition convention**: `detail::compose(parent, local)` is
  the canonical skeletal-animation formula — full scale propagation
  (componentwise) **diverges from** `HierarchySystem`'s default
  `scale = local.scale`. Bone chains universally want
  parent-scale-propagated children. Quat mul + `rotate(q, v)` match
  HierarchySystem byte-for-byte so renderer-side composition stays
  consistent.
- **Re-exports**: `pose.hpp` aliases `threadmaxx::Vec3` / `Quat`
  into the `threadmaxx::animation` namespace so consumers that
  `using namespace threadmaxx::animation` see them without a
  second using-decl.
- **Skeleton id encoding**: low 32 bits = slot index, high 32 bits
  = generation tag. Clip ids are bare slot indices in A1 (no clip
  removal API yet — A2+ revisits if it lands).
- **Test gate**: all four executables pass on first build under
  `tests/animation/` (CTest names `animation.test_animation_*`).
  Full ctest 165/165 green.
- **Soul check**: zero allocation in the hot path (`PoseBuffer`
  caller-owned), AoS POD chosen to match the SIMD library's
  `simd_batchable<JointPose>` extension path (A1.x v1.x), span-based
  kernel signatures (`std::span<JointPose>`) so the engine's
  `forEachChunk` integration in A7 can hand worker-private buffers
  in without a copy.

## Batch A2 — Clip sampling + event tracks

**Goal**: time-keyed sampling of a clip into a `PoseBuffer`. Looping,
clamp-at-end, and event-track firing on time-window crossings.

**Test gate**:

- `test_animation_clip_sampling` — sample a 1-second clip at t=0,
  t=0.5, t=1.0; results match the corresponding keyframe (linear
  interp between authored keys); time outside `[0, duration]`
  clamps unless `looping == true`.
- `test_animation_event_tracks` — events fire exactly once per
  crossing in non-looping mode; events fire each loop in looping
  mode; rewinding (time jumps backward) doesn't replay events
  unless explicitly requested.

**Files**: `detail/curve_eval.hpp` (header-only curve sampler),
`src/Animator.cpp` skeleton (just clip sampling for now).

**Risks**: choosing the interpolation curve representation
(`std::vector<JointPose>` keyframes vs. per-channel SoA tracks).
Recommendation: start with per-clip uniformly-keyed
`std::vector<Pose>`; refactor to per-channel curves only when a
profiled bench shows the AoS path is the bottleneck.

**Out of scope**: graph (A3), additive layers (A4).

## Batch A3 — Graph + Animator + Clip/Output nodes

**Goal**: minimal `AnimationGraph` with `Clip` and `Output` node
types. `Animator::evaluate` walks the graph and produces a pose.
No blends yet — just clip → output.

**Test gate**:

- `test_animation_graph_basic` — create graph with one Clip node
  connected to Output; `evaluate` produces the same pose as direct
  clip sampling.
- `test_animation_graph_param` — `setParameter` updates a node's
  playback rate; verify the output pose advances at the scaled
  rate.
- `test_animation_graph_dirty` — `EvalResult::dirty == false` when
  inputs are unchanged across calls.

**Files**: `graph.hpp`, `eval.hpp`, `detail/graph_eval.hpp`,
`src/GraphEval.cpp`.

**Risks**: graph evaluation order — start with a topological walk
each frame; cache it later only after profiling.

**Out of scope**: Blend1D/Blend2D (A4), IK node (A5), Layer node
(A4 → can defer).

## Batch A4 — Blend nodes + additive layers

**Goal**: Blend1D and Blend2D nodes for locomotion-style transitions;
Additive node for upper-body overlays; Layer node for masked
blending.

**Test gate**:

- `test_animation_blend1d` — three input clips at parameter values
  0.0 / 0.5 / 1.0; query at intermediate values produces correctly
  weighted blend.
- `test_animation_blend2d` — 4-corner grid; centroid query produces
  uniform 0.25/0.25/0.25/0.25 weights.
- `test_animation_additive` — base + additive layer composition;
  removing the additive layer returns to base; weight=0 is a no-op.
- `test_animation_layer_mask` — masked layer affects only listed
  joints; unmasked joints stay at base values.

**Files**: `blend.hpp`, extension to `detail/graph_eval.hpp` for
the new node kinds.

**Out of scope**: IK (A5).

## Batch A5 — IK constraints (FABRIK or CCD)

**Goal**: `solveIK` for 2-bone arm/leg chains plus a generic chain
solver. FABRIK recommended — it's iterative, branchless, and
converges well for chains of 2-6 joints.

**Test gate**:

- `test_animation_ik_2bone` — known-good shoulder/elbow/wrist chain;
  given a wrist target, the elbow lands within 1e-4 of the
  analytically-computed bent position.
- `test_animation_ik_chain` — 5-joint chain reaches a target ≤ chain
  length within 50 iterations to 1e-3 tolerance.
- `test_animation_ik_unreachable` — target beyond chain reach
  produces a stretched-toward-target solution (no NaN, no infinite
  loop).
- `test_animation_ik_weight` — `IKTarget::weight = 0.5` blends 50/50
  between solved and pre-IK pose.

**Files**: `ik.hpp`, `src/IK.cpp`.

**Risks**: numerical drift in long chains. The test gate's `1e-3`
tolerance is intentionally loose to accommodate FABRIK's iterative
nature — tighten only if a profile reveals it matters.

**Out of scope**: joint constraints (twist limits, hinge), full
analytical IK for specific rigs.

## Batch A6 — Motion warping

**Goal**: `applyWarp` for foot-placement / attack-alignment use
cases. A `WarpRequest` morphs the root motion (or named joint
motion) from `from` to `to` over `[startTime, endTime]`.

**Test gate**:

- `test_animation_warp_root` — root-motion clip warped to hit a
  target point; sampled pose at `endTime` has root at `to ± epsilon`.
- `test_animation_warp_window` — outside the warp window the pose
  is unmodified.
- `test_animation_warp_blend` — partial-weight warp produces a
  position lerped between authored and target.

**Files**: `warp.hpp`, `src/Warp.cpp`.

**Out of scope**: contact-IK during warping (would compose A5+A6;
worth a future batch but not blocking).

## Batch A7 — Batch / crowd evaluation

**Goal**: integrate with the engine's `forEachChunk<AnimationStateRef,
Transform>` so many agents can be evaluated in parallel. Hand each
worker a private `PoseBuffer` pool (engine-owned scratch arena).

**Test gate**:

- `test_animation_crowd_smoke` — register 256 NPCs sharing one
  skeleton + one clip; engine.step() evaluates them all; resulting
  poses are deterministic across 2 runs.
- `test_animation_crowd_lod` — agents marked LOD=1 sample at 30 Hz
  instead of 60 Hz; verify per-tick evaluation skips correctly.
- `bench/animation_crowd_bench.cpp` — 1k / 8k / 32k crowd of a
  shared skeleton; scalar vs. with-SIMD-pose-math comparison
  (depends on the engine's SIMD sibling for the pose math).

**Files**: extension to `eval.hpp` for the batch-evaluation entry
point; `detail/job_batch.hpp`; bench source.

**Risks**: cross-thread pose-buffer aliasing. Mitigation: caller
guarantees one buffer per chunk slice via the engine's scratch
arena pattern.

**Out of scope**: distributed crowd / LOD policies beyond simple
skip-every-N (engine games own the policy).

## Batch A8 — Diagnostics + retarget + serialization

**Goal**: round out the v1.0 surface. Pose validation diagnostics
(NaN check, axis-flip check), basic skeleton retargeting for
shared-clip-different-rig cases, and `serialization.hpp` for
asset save/load.

**Test gate**:

- `test_animation_diagnostics` — `validatePose` rejects NaN
  rotations and degenerate scales.
- `test_animation_retarget_smoke` — clip authored on rig A samples
  onto rig B with matching joint names; per-joint local rotations
  preserved.
- `test_animation_serialization_roundtrip` — write skeleton + clip
  set to bytes, read back, all metadata + curve data matches
  byte-for-byte.

**Files**: `diagnostics.hpp`, `retarget.hpp`, `serialization.hpp`,
`cloth.hpp` (hook signatures only — full cloth is a v1.x topic),
`src/Retarget.cpp`.

**Out of scope**: cloth solver (out of scope for the entire library
per design notes §1; we ship hook signatures only).

## v1.0 close-out criteria

- ✓ Every batch A1–A8 landed and tested.
- ✓ Engine integration smoke test passes (RPG demo D14 wires this
  library — see `GAME_EXTENSION.md`).
- ✓ Bench coverage: at least one `bench/animation_*.cpp` showing
  end-to-end per-agent cost at 1k / 8k crowd sizes.
- ✓ Docs: README, USER_GUIDE, MAINTAINER_GUIDE, plus this file
  with all batches marked ✅ landed.
- ✓ ctest 100% on `build/` and `build-werror/`.
- ✓ Version stamped at 1.0.0 in `include/threadmaxx_animation/version.hpp`.

## v1.x candidate batches (not blocking v1.0)

### v1.x — SIMD-accelerated pose blending

`detail/pose_math.hpp`'s `lerp_pose` and `blend_pose_weighted` are
the obvious vectorization targets. Per-joint is 7 floats (3 trans +
4 rot, scale optional). Reuse `threadmaxx::simd::quat_ops` for the
normalization step. Bench gate: 16k-joint blend must beat scalar
by ≥2× to justify dispatching.

### v1.x — Curve compression

Replace uniformly-sampled `std::vector<JointPose>` with per-channel
spline curves (Catmull-Rom or Hermite). Memory win for long clips;
sampling cost should be neutral. Out of scope until a real game
shows clip storage is a problem.

### v1.x — Constraint joints (twist/hinge limits) on IK

A6's FABRIK is unconstrained. Real arm IK wants twist limits at the
shoulder and elbow. Bolt on after a real game's animator complains.

### v1.x — Cloth solver

Out of scope per DESIGN_NOTES §1, but the hooks ship in A8. Real
implementation would be a separate sibling library
(`threadmaxx_cloth`) — same boundary discipline as
`threadmaxx_navmesh` vs. `threadmaxx_physics`.

### v1.x — GPU pose evaluation

Currently CPU-only. GPU-side pose buffers + dispatch-based
evaluation are a renderer-side concern (and depend on the renderer
adopting compute-pass primitives). Coordinate with renderer batch
sequencing.

## Out of scope for the whole library

Per DESIGN_NOTES §1 — none of this lands at any batch:

- Rendering backend / skinning execution
- Physics simulation
- ECS storage ownership
- Asset import pipeline (FBX/glTF loaders are game-side)
- Network replication
- Editor UI
- Engine component-model ownership
