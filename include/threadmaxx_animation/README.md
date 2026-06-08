# `threadmaxx_animation`

Engine-agnostic skeletal animation library for the `threadmaxx`
engine. **Status**: v1.0.0 — production-ready.

## What

A pose-evaluation pipeline that turns time + parameters into a
flat `std::span<JointPose>` the renderer can consume. Designed to
plug into the engine's `forEachChunk`-style iteration without
owning entities or component storage.

The library covers eight pillars:

- **Pose data model** — `JointPose` / `PoseBuffer` / `PoseSpan`
  PODs that the renderer skinning path consumes directly.
- **Clip sampling + event tracks** — fixed-rate keyframe sampling
  with looping and authored events.
- **Animation graph** — node-based DAG (Clip → Blend → Output)
  evaluated per agent by an `Animator`.
- **Blends** — 1D / 2D / additive + per-joint layer masks.
- **IK** — FABRIK chain solver plus a 2-bone analytical helper.
- **Motion warping** — single-joint translation morph over a time
  window (foot placement, attack alignment).
- **Crowd evaluation** — batch `evaluateCrowd` + per-agent LOD that
  scales to thousands of agents in a single worker job.
- **Round-trip surface** — name-matched skeleton retargeting,
  pose-validation diagnostics, asset-bundle serialization, and
  cloth attachment hooks (PODs only; solver lives in a future
  `threadmaxx_cloth` sibling).

The library is engine-agnostic at link time — it pulls in
`threadmaxx::threadmaxx` only for the math PODs (`Vec3`, `Quat`).
It does NOT own entities, components, or simulation time; the
host engine drives `Animator::advance` / `evaluate` once per tick
and pipes the result into its renderer.

## Quick start

```cmake
target_link_libraries(my_target PRIVATE threadmaxx::animation)
```

```cpp
#include <threadmaxx_animation/threadmaxx_animation.hpp>

using namespace threadmaxx::animation;

AnimationRegistry reg;
auto skel = reg.addSkeleton(makeHumanoidSkeleton());
auto clip = reg.addClip(makeWalkClip());

Animator anim;
anim.setSkeleton(skel);
anim.setClip(clip);

PoseBuffer buf;
buf.resize(reg.getSkeleton(skel)->joints.size());

// Every tick:
anim.advance(dt);
anim.samplePose(buf.localPose());
// Hand buf.localPose() to the renderer.
```

## Documentation

| Document | Audience | Purpose |
|----------|----------|---------|
| [`README.md`](README.md) | Everyone | Top-level overview (this file) |
| [`USER_GUIDE.md`](USER_GUIDE.md) | Consumers | Public surface inventory, integration patterns, perf expectations |
| [`MAINTAINER_GUIDE.md`](MAINTAINER_GUIDE.md) | Library devs | Architecture, evaluation model, how to add nodes / kernels / formats |
| [`DESIGN_NOTES.md`](DESIGN_NOTES.md) | Anyone | Original spec (frozen reference) |
| [`FUTURE_WORK.md`](FUTURE_WORK.md) | Library devs | Batch-by-batch landed work + v1.x candidates |
| [`CHANGELOG.md`](CHANGELOG.md) | Everyone | Per-release notes |

## Scope

- ✓ Pose math (TRS interpolation, slerp/nlerp blending).
- ✓ Clip sampling, event firing, parameter-driven blends.
- ✓ Graph-based composition (Clip → Blend → Layer → IK → Warp →
  Output).
- ✓ Crowd evaluation with per-agent LOD.
- ✓ Skeleton retargeting (name-matched), pose validation,
  asset bundle serialization.
- ✓ Engine-agnostic API; the engine pulls in this library, not
  the other way around.

Out of scope (per `DESIGN_NOTES.md` §1): rendering / skinning
execution, ECS storage ownership, asset import (FBX / glTF
loaders are game-side), physics, network replication, editor UI,
cloth solver (future `threadmaxx_cloth` sibling).

## Status: production-ready

- 26 dedicated test executables registered with CTest (100%
  passing on `build/` and `build-werror/` trees).
- Crowd evaluation benchmark (`bench/animation_crowd_bench.cpp`)
  with measured per-agent costs at 1k / 8k crowd sizes.
- All eight A-batches (A1–A8) landed and reviewed — see
  `FUTURE_WORK.md` for the per-batch retrospectives.
- Versioning policy documented (semver, lifecycle in
  `MAINTAINER_GUIDE.md`).

See [`FUTURE_WORK.md`](FUTURE_WORK.md) for v1.x candidate work
(SIMD-accelerated blending, curve compression, IK constraint
joints, cloth solver, GPU pose evaluation — none of which blocks
v1.0 production use).

## License

Same as the parent `threadmaxx` project.
