# Changelog

All notable changes to `threadmaxx_animation` are recorded here.
The format follows [Keep a Changelog](https://keepachangelog.com/);
the project adheres to [Semantic Versioning](https://semver.org/).
See [`MAINTAINER_GUIDE.md`](MAINTAINER_GUIDE.md) for the bump rules.

## [1.0.0] — 2026-06-08 — Production-ready close-out

### Added

- **`version.hpp`** — `THREADMAXX_ANIMATION_VERSION_MAJOR/MINOR/PATCH`
  macros, packed `THREADMAXX_ANIMATION_VERSION` integer, and
  `version_string()` for runtime logging.
- **`README.md`** — top-level library overview with quick start +
  doc cross-references.
- **`USER_GUIDE.md`** — user-facing documentation (public surface
  inventory, both evaluation modes, crowd-eval integration, perf
  expectations).
- **`MAINTAINER_GUIDE.md`** — internal documentation (evaluation
  model, registry threading contract, hot-path allocation
  discipline, how to add graph node types, serialization format,
  common pitfalls).
- **`CHANGELOG.md`** — this file.

### Closed-out roadmap

`FUTURE_WORK.md` v1.0 closure section documents what's shipped vs.
deferred to v1.x candidate batches (SIMD-accelerated blending,
curve compression, IK constraint joints, cloth solver, GPU pose
evaluation).

---

## [0.8.0] — 2026-06-07 — Batch A8 — Diagnostics + retarget + serialization

### Added

- **`diagnostics.hpp`** — `validatePose` walks a `std::span<JointPose>`
  and emits a `PoseValidationReport` with a `PoseIssue` bit-flag
  set (`NanTranslation`, `NanRotation`, `DenormalRotation` where
  `|q|² < 1e-6`, `DegenerateScale` where any axis ≤ 0). Header-only,
  `noexcept`, allocation-free.
- **`retarget.hpp`** + **`Retarget.cpp`** — `buildRetargetMap`
  walks joint names and produces `(srcIndex, dstIndex)` pairs;
  `retargetPose` copies rotation by default (translation + scale
  opt-in via `RetargetChannels`). Stale out-of-range indices are
  silently skipped.
- **`serialization.hpp`** — `writeAnimationAssetBundle` /
  `readAnimationAssetBundle` with `[magic 'AMTX' u32][version u32 = 1]`
  header. Host-endian POD blob mirroring the engine's
  `WorldSnapshot` convention.
- **`cloth.hpp`** — hook PODs (`ClothAttachmentPoint`,
  `ClothAttachmentSet`, `ClothSolverHooks`) + no-op `updateCloth`
  trampoline. Real solver lives in a future `threadmaxx_cloth`
  sibling.
- Three test executables: `test_animation_diagnostics`,
  `test_animation_retarget_smoke`,
  `test_animation_serialization_roundtrip`.

### Deferred to v1.x

- Axis remap for retargeting (different rest orientations).
- Byte-swapping serialization reader (cross-arch transfer).

---

## [0.7.0] — 2026-06-07 — Batch A7 — Batch / crowd evaluation

### Added

- **`evaluateCrowd(span<AgentInput>, span<PoseSpan>, EvalContext)`**
  — single-call N-agent evaluation. Per-agent `parameterOverrides`
  + `lod` field (`Full` / `Reduced` / `Skipped`). Pool reuse
  pinned by `test_animation_crowd_pool.cpp`.
- **`bench/animation_crowd_bench.cpp`** — per-agent cost at 1k /
  8k crowd sizes (CSV output).
- Three test executables: `test_animation_crowd_smoke`,
  `test_animation_crowd_lod`, `test_animation_crowd_pool`.

### Performance

- 1k agents, graph mode (Clip → Out): ~9 µs / agent.
- 8k agents, graph mode: ~7 µs / agent (sub-linear scaling due to
  amortized pool reuse).

---

## [0.6.0] — 2026-06-07 — Batch A6 — Motion warping

### Added

- **`warp.hpp`** — `applyWarp(pose, jointIndex, WarpParams)`
  single-joint translation morph over `[startTime, endTime]`.
  Additive (composes with the sampled clip pose).
  `NodeType::Warp` graph node + `WarpNode` payload.
- Three test executables: `test_animation_warp_root`,
  `test_animation_warp_window`, `test_animation_warp_blend`.

### Deferred

- Multi-joint warps (graph "Warping" node with named joint
  selection) — v1.x.
- Rotation warping — v1.x.
- Contact-IK during warping (would compose A5 + A6) — v1.x.

---

## [0.5.0] — 2026-06-07 — Batch A5 — IK constraints

### Added

- **`ik.hpp`** — `solveIK(span<Vec3>, IKTarget) -> IKResult`
  FABRIK on a 1..N-bone position chain.
  `solveTwoBone(positions[3], IKTarget) -> IKResult` analytical
  helper (reference for the FABRIK output).
  `IKTarget::tolerance` defaults to 1e-3; `maxIterations`
  defaults to 50. `IKResult::converged` true when end-effector
  lands within tolerance.
- `NodeType::IK` graph node slot reserved (handler wires in v1.x).
- Four test executables: `test_animation_ik_2bone`,
  `test_animation_ik_chain`, `test_animation_ik_unreachable`,
  `test_animation_ik_weight`.

### Deferred

- Twist / hinge joint constraints — v1.x.
- Pole vectors beyond the 2-bone helper — v1.x.
- Target rotation enforcement (`IKTarget::rotation` reserved but
  unused) — v1.x.

---

## [0.4.0] — 2026-06-07 — Batch A4 — Blend nodes + additive layers

### Added

- **`blend.hpp`** — `Blend1DNode` (parameter-driven, sorted
  thresholds), `Blend2DNode` (inverse-distance-weighted on
  arbitrary 2D positions), `AdditiveNode` (base + weight × delta,
  TRS-aware), `LayerNode` (per-joint mask).
- `AnimationGraph::addBlend1D` / `addBlend2D` / `addAdditive` /
  `addLayer` builder methods. Parameter resolution via two-tier
  path (graph default, per-Animator override).
- Four test executables: `test_animation_blend1d`,
  `test_animation_blend2d`, `test_animation_additive`,
  `test_animation_layer_mask`.

---

## [0.3.0] — 2026-06-07 — Batch A3 — Graph + Animator + Clip/Output nodes

### Added

- **`graph.hpp`** — `AnimationGraph` (node-based DAG), `NodeType`
  enum (Clip / Blend1D / Blend2D / Additive / Layer / IK / Warp /
  Output, every variant declared from the start so switch
  coverage pins at compile time), `GraphNodeId` opaque handle.
- **`eval.hpp`** — `Animator::setGraph(&graph)` + `evaluate(ctx, out)`
  graph-mode hot path. Per-Animator parameter overrides via
  `setParameter(nodeId, name, value)`. Cycle detection at
  `connect` time.
- **`EvalContext`** + **`EvalResult`** PODs. `EvalResult::dirty`
  signals whether downstream consumers can skip processing.
- Three test executables: `test_animation_graph_basic`,
  `test_animation_graph_param`, `test_animation_graph_dirty`.

---

## [0.2.0] — 2026-06-07 — Batch A2 — Clip sampling + event tracks

### Added

- **`clip.hpp`** — `ClipDesc` with AoS-flat keyframes, sorted
  `keyframeTimes`, optional `events` track. `EventTrackEvent`
  POD (time, name).
- **`AnimationRegistry::addClip(desc) -> ClipRef`** +
  `getClip(ref)` + `removeClip(ref)`.
- **`Animator::setClip(ref)`** + `advance(dt)` + `samplePose(out)`
  single-clip-mode hot path. Looping handled at sample time.
  Events accumulate in the Animator's drain buffer.
- Two test executables: `test_animation_clip_sampling`,
  `test_animation_event_tracks`.

---

## [0.1.0] — 2026-06-07 — Batch A1 — Foundations (registry + pose math)

### Added

- **`types.hpp`** — `JointId`, `SkeletonRef`, `ClipRef`
  generation-tagged refs.
- **`pose.hpp`** — `JointPose` (40 B POD: Vec3 + Quat + Vec3),
  `Pose` (owning), `PoseSpan` (non-owning kernel-boundary
  contract), `PoseBuffer` (reusable hot-path allocation).
- **`skeleton.hpp`** — `Joint`, `SkeletonDesc`.
- **`registry.hpp`** — `AnimationRegistry::addSkeleton/getSkeleton/removeSkeleton`.
- **`detail/pose_math.hpp`** — `lerp_pose` (TRS, slerp + nlerp),
  `blend_pose_weighted` (N-way weighted composition).
- **`threadmaxx_animation.hpp`** umbrella header.
- Top-level CMake target `threadmaxx::animation` (STATIC archive).
- Four test executables: `test_animation_skeleton`,
  `test_animation_pose_buffer`, `test_animation_pose_math`,
  `test_animation_clip` (the latter pinned the registry +
  generation-tagged ref contract on a placeholder ClipDesc that
  A2 fully populated).

---

## Pre-v1: Design phase

The library was scoped in `DESIGN_NOTES.md` before any code
landed. `FUTURE_WORK.md` broke that scope into the eight A-batches
above; each batch landed as a single coherent change with its own
test gate. See `FUTURE_WORK.md` for per-batch retrospectives.
