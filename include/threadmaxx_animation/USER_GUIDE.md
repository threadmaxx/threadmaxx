# `threadmaxx_animation` — User Guide

Engine-agnostic skeletal animation: pose evaluation, graph-based
composition, crowd batching, IK, and motion warping for the
`threadmaxx` engine's POD model.

## When to use this library

Reach for `threadmaxx_animation` when you have:

- A skeletal mesh with a small (≤ ~256) joint hierarchy.
- A handful to thousands of agents you want to animate per tick.
- Authored clip data (sampled keyframes — FBX / glTF importers
  produce this naturally; bring your own importer).
- A renderer that consumes per-joint local transforms.

It is NOT a procedural animation system (no spring physics, no
muscle simulation) and it does not own entity state — your engine
drives `Animator::advance` / `evaluate` once per tick and pipes
the resulting pose into its renderer.

## Quick start

```cpp
#include <threadmaxx_animation/threadmaxx_animation.hpp>

using namespace threadmaxx::animation;

// 1. Register assets once at startup.
AnimationRegistry reg;
SkeletonRef skel = reg.addSkeleton(makeHumanoidSkeleton());
ClipRef      clip = reg.addClip(makeWalkClip());

// 2. Create an Animator per agent.
Animator anim;
anim.setSkeleton(skel);
anim.setClip(clip);  // single-clip mode

// 3. Per-tick: advance + sample.
PoseBuffer buf;
buf.resize(reg.getSkeleton(skel)->joints.size());

void tick(float dt) {
    anim.advance(dt);
    anim.samplePose(buf.localPose());
    // Hand buf.localPose() to the renderer.
}
```

The same Animator instance handles graph-mode evaluation by
calling `setGraph(...)` instead of `setClip(...)`.

## Build setup

Add the dependency:

```cmake
target_link_libraries(my_target PRIVATE threadmaxx::animation)
```

The CMake target carries the include directory, `cxx_std_20`, and
a transitive link to `threadmaxx::threadmaxx` (for the math PODs).
The library produces one static archive (`libthreadmaxx_animation.a`
on POSIX).

### Build option

The library is opt-in via:

```
cmake -B build -DTHREADMAXX_BUILD_ANIMATION=ON  # default ON
```

Setting `OFF` drops the `threadmaxx::animation` target.

## Public surface inventory

All types live in namespace `threadmaxx::animation`. Each header
is a clean unit — include just what you need (or the umbrella
`threadmaxx_animation.hpp`).

### Pose model (`pose.hpp`)

| Type / function                  | Purpose                                          |
|----------------------------------|--------------------------------------------------|
| `JointPose`                      | Per-joint TRS (40 B POD: `Vec3` + `Quat` + `Vec3`) |
| `Pose`                           | Owning pose buffer with weight + validity         |
| `PoseSpan`                       | Non-owning span — the kernel boundary contract    |
| `PoseBuffer`                     | Reusable allocation for the hot eval path         |

### Skeleton + clip (`skeleton.hpp`, `clip.hpp`)

| Type                             | Purpose                                          |
|----------------------------------|--------------------------------------------------|
| `Joint`                          | One node: name, parent index, bind-local pose    |
| `SkeletonDesc`                   | Owning skeleton container (joints + bindGlobal)  |
| `ClipDesc`                       | AoS-flat keyframes + sorted times + events       |
| `EventTrackEvent`                | `{time, name}` pair fired by clip sampling       |

### Registry (`registry.hpp`)

| Method                                | Purpose                                          |
|---------------------------------------|--------------------------------------------------|
| `addSkeleton(desc) -> SkeletonRef`    | Register; ref stays valid until matching remove  |
| `addClip(desc) -> ClipRef`            | Same model for clips                             |
| `getSkeleton(ref)` / `getClip(ref)`   | Borrow immutable pointer (nullptr on stale ref)  |
| `removeSkeleton(ref)` / `removeClip`  | Bumps generation; outstanding refs become stale  |

Single-threaded by contract — call `add` / `remove` from the sim
thread; `get*` is safe to call from many worker reads in the same
wave.

### Animation graph (`graph.hpp`)

| Type / method                              | Purpose                                          |
|--------------------------------------------|--------------------------------------------------|
| `NodeType` enum                            | Clip / Blend1D / Blend2D / Additive / Layer / IK / Warp / Output |
| `GraphNodeId`                              | Opaque node handle                               |
| `AnimationGraph::addClip(clipPtr)`         | Authors a Clip-source node                       |
| `AnimationGraph::addBlend1D(payload)`      | 1D parameter-driven blend                        |
| `AnimationGraph::addBlend2D(payload)`      | 2D inverse-distance-weighted blend               |
| `AnimationGraph::addAdditive(payload)`     | Base + weighted delta                            |
| `AnimationGraph::addLayer(payload)`        | Per-joint mask layer                             |
| `AnimationGraph::addOutput(input)`         | Terminal sink (exactly one per graph)            |
| `setDefaultParameter(nodeId, name, val)`   | Graph-wide parameter default                     |

### Animator + evaluation (`eval.hpp`)

| Method                                       | Purpose                                          |
|----------------------------------------------|--------------------------------------------------|
| `setSkeleton(ref)`                           | Mandatory before advance / evaluate              |
| `setClip(ref)` (single-clip mode)            | Bind one clip; use `advance` + `samplePose`      |
| `setGraph(&graph)` (graph mode)              | Bind a graph; use `evaluate(ctx, outPose)`       |
| `advance(dt)` / `samplePose(out)`            | Single-clip mode hot path                        |
| `evaluate(EvalContext, PoseSpan out)`        | Graph mode hot path                              |
| `setParameter(nodeId, name, value)`          | Per-instance override of a graph default         |
| `evaluateCrowd(span<AgentInput>, ...)`       | Crowd batch — N agents in one call               |

### Blends (`blend.hpp`)

`Blend1DNode`, `Blend2DNode`, `AdditiveNode`, `LayerNode` payloads.
Construct, add via `AnimationGraph::add...`, connect inputs by
calling `connect(srcId, dstId, inputIndex)`. The graph resolves
weights from the bound `Animator`'s parameter overrides.

### IK (`ik.hpp`)

| Function                                   | Purpose                                          |
|--------------------------------------------|--------------------------------------------------|
| `solveTwoBone(positions[3], target) -> IKResult`  | Analytical 2-bone helper (reference)      |
| `solveIK(span<Vec3> chain, IKTarget) -> IKResult` | FABRIK on a 1..N-bone position chain      |
| `IKTarget { position, rotation*, weight, tolerance, maxIterations }` | Solve parameters (`rotation` reserved) |

The solver operates on world-space joint positions; per-joint
rotation reconstruction is the caller's problem in v1.0.

### Motion warping (`warp.hpp`)

| Function                                   | Purpose                                          |
|--------------------------------------------|--------------------------------------------------|
| `applyWarp(pose, jointIndex, params)`      | Single-joint translation morph over `[t0, t1]`   |
| `WarpParams { time, startTime, endTime, from, to, weight }` | Window + endpoint + blend           |

### Diagnostics (`diagnostics.hpp`)

| Function                                   | Purpose                                          |
|--------------------------------------------|--------------------------------------------------|
| `validatePose(span<JointPose>) -> PoseValidationReport` | NaN / denormal / degenerate scale walk |
| `validatePose(PoseSpan)`                   | PoseSpan overload (same code path)               |
| `validateJoint(JointPose) -> PoseIssue`    | Per-joint entry point                            |
| `PoseIssue` bit-flag enum                  | `NanTranslation` / `NanRotation` / `DenormalRotation` / `DegenerateScale` |
| `any(issue)` / `has(issue, bit)`           | Bit-set helpers                                  |

### Retargeting (`retarget.hpp`)

| Function                                   | Purpose                                          |
|--------------------------------------------|--------------------------------------------------|
| `buildRetargetMap(src, dst) -> RetargetMap`     | Walk joint names, build index pairs         |
| `retargetPose(srcSpan, dstSpan, map, channels)` | Copy mapped joints into dst pose            |
| `RetargetChannels { copyRotation=true, copyTranslation=false, copyScale=false }` | Channel selection |

### Serialization (`serialization.hpp`)

| Function                                   | Purpose                                          |
|--------------------------------------------|--------------------------------------------------|
| `writeAnimationAssetBundle(bundle) -> vector<uint8_t>` | Serialize a skeletons + clips bundle  |
| `readAnimationAssetBundle(bytes) -> optional<Bundle>`  | Parse; nullopt on bad magic/version/truncation |
| `kAnimationAssetMagic` / `kAnimationAssetVersion` | Wire-format header constants                |

Wire format mirrors the engine's `WorldSnapshot`: host-endian POD
blob with `[magic 'AMTX' u32][version u32]` header. Cross-arch
transfer is out of scope for v1.0.

### Cloth hooks (`cloth.hpp`)

PODs only — the solver lives in a future `threadmaxx_cloth`
sibling library. The hooks let game code declare attachment points
and a future cloth backend's update callback in one place.

| Type                                       | Purpose                                          |
|--------------------------------------------|--------------------------------------------------|
| `ClothAttachmentPoint`                     | `{particleIndex, joint, localOffset}`            |
| `ClothAttachmentSet`                       | Named set bound to a skeleton                    |
| `ClothSolverHooks`                         | Function pointer + userData                      |
| `updateCloth(pose, attachments, hooks)`    | No-op trampoline when `hooks.update == nullptr`  |

## Two evaluation modes

### Single-clip mode

For agents whose animation is one time-keyed clip (idle loops,
projectile flight, simple props):

```cpp
Animator anim;
anim.setSkeleton(skel);
anim.setClip(walkClip);

anim.advance(dt);
anim.samplePose(buf.localPose());
```

`advance` advances the playhead; `samplePose` writes the sampled
pose into the caller's buffer. Events fired in the advanced
window are accumulated in the Animator's event buffer; drain with
`drainEvents`.

### Graph mode

For agents that blend / layer / IK / warp:

```cpp
AnimationGraph graph;
auto walk = graph.addClip(walkClipPtr);
auto run  = graph.addClip(runClipPtr);
auto b1d  = graph.addBlend1D(Blend1DNode{
    .inputs = {walk, run},
    .thresholds = {0.0f, 1.0f},
});
auto out  = graph.addOutput(b1d);
graph.setDefaultParameter(b1d, "param", 0.5f);

Animator anim;
anim.setSkeleton(skel);
anim.setGraph(&graph);

EvalContext ctx{dt, simTime, 1.0f};
EvalResult res = anim.evaluate(ctx, buf.localPose());
```

Per-Animator overrides via `anim.setParameter(b1d, "param", 0.8f)`
let crowd agents share one graph while playing it differently.

## Crowd evaluation

For thousands of agents, batch through `Animator::evaluateCrowd`
or its free-function form in `eval.hpp`:

```cpp
std::vector<AgentInput> inputs(agentCount);
std::vector<PoseSpan>   outputs(agentCount);
// Fill inputs[i].graph / inputs[i].parameters / outputs[i] = bufs[i].localPose()

evaluateCrowd(inputs, outputs, ctx);
```

Per-agent LOD lives on `AgentInput::lod` — `Full` runs the graph,
`Reduced` samples a cheaper fallback (graph-author's choice),
`Skipped` writes nothing. The crowd path is allocation-free in
steady state once `PoseBuffer`s are sized.

See `bench/animation_crowd_bench.cpp` for the per-agent perf
contract (~12 µs / agent at 1k crowd on the reference host).

## Integration with the engine's `forEachChunk`

Wire a system that drives one Animator per entity by walking the
chunk containing your `AnimatorComponent`:

```cpp
class AnimationSystem : public threadmaxx::ISystem {
public:
    ComponentSet reads()  const noexcept override {
        return ComponentSet{Component::AnimationStateRef};
    }
    ComponentSet writes() const noexcept override {
        return ComponentSet{Component::AnimationStateRef};
    }

    void update(threadmaxx::SystemContext& ctx) override {
        const float dt = ctx.dt();
        threadmaxx::forEachChunk<AnimationStateRef>(ctx,
            [dt](std::span<const EntityHandle> es,
                 std::span<const AnimationStateRef> refs,
                 threadmaxx::CommandBuffer& cb) {
                for (std::size_t i = 0; i < es.size(); ++i) {
                    auto* agent = lookupAgent(refs[i]);
                    agent->animator.advance(dt);
                    agent->animator.samplePose(agent->buffer.localPose());
                }
            });
    }
};
```

Agent state (Animator, PoseBuffer) lives in a game-side container
indexed by `AnimationStateRef::id` — the engine intentionally
does not own animator state, mirroring the design of physics /
nav refs.

## Conventions

### Allocation policy

- **Per-tick**: the hot path (advance, sample, evaluate,
  evaluateCrowd) does NOT allocate once `PoseBuffer`s are sized
  and the graph is constructed.
- **Setup**: `AnimationRegistry::add*`, `AnimationGraph::add*`
  allocate; do them at level-load.
- **Validation**: `validatePose` / `validateJoint` are
  allocation-free.

### NaN / denormal / degenerate inputs

- `slerp` / `nlerp` of identity-vs-identity returns identity.
- A pose with a NaN translation propagates through the graph; the
  renderer sees the NaN. Use `validatePose` to catch this in
  debug builds.
- A zero-axis scale collapses geometry; `validatePose` flags it
  as `DegenerateScale`.
- The library never sanitizes inputs — that's the caller's
  contract.

### Threading

- `AnimationRegistry` is single-threaded.
- `AnimationGraph` is immutable after construction — every
  Animator pointing at it reads safely from any worker.
- `Animator` is single-instance: one worker may own one Animator
  for one chunk. Sharing an Animator across workers is undefined.
- `evaluateCrowd` is internally parallelizable (the contract
  pins per-agent independence); the v1.0 implementation runs the
  agents serially within the caller's thread, but per-agent
  ordering is not observable.

### Empty inputs

- An empty `PoseSpan` is a valid no-op for every kernel.
- An Animator with no skeleton bound returns a zero-joint pose
  silently (`evaluate` writes 0 joints).

## Performance expectations

Measured on a recent x86_64 desktop with `-O3`, single-thread,
medians from `bench/animation_crowd_bench`:

| Scenario                              | Per-agent cost |
|---------------------------------------|---------------:|
| 1k agents, single-clip mode           | ~6 µs          |
| 1k agents, graph mode (Clip → Out)    | ~9 µs          |
| 1k agents, graph (Blend1D + IK + Warp)| ~14 µs         |
| 8k agents, graph (Clip → Out)         | ~7 µs / agent  |

The 8k scaling is sub-linear because per-agent allocator pressure
amortizes against the crowd-path's pre-sized scratch buffers.

### When to expect a perf cliff

- **Joint counts > 256** — most blend kernels are O(joints), so
  cost scales linearly. SoA refactor is a v1.x candidate.
- **Graph depth > 8** — every node introduces an intermediate
  pose copy. Flatten where possible.
- **Per-tick parameter churn** — `setParameter` writes a map;
  hot-loop updates pay a hash cost. For changes every frame,
  cache the node payload pointer and mutate inline.

## Restrictions / non-goals

Per `DESIGN_NOTES.md` §1, the library does NOT:

- Render meshes or execute skinning (renderer-side concern).
- Own ECS components or entity state (game-side concern).
- Import FBX / glTF / animation source formats (game-side
  importer; the library consumes already-decoded `SkeletonDesc` /
  `ClipDesc`).
- Simulate physics or cloth.
- Replicate state over a network.
- Provide editor UI.

If you need any of the above, build it as a separate sibling
library or in your game-side code.

## Picking `slerp` vs `nlerp` in pose blends

Internal blend kernels use `nlerp` for per-joint quaternion
blending by default (throughput trumps mathematical purity for
adjacent keyframes; both pick shortest-path automatically).
Custom blend nodes can opt into `slerp` via the `quat_ops` kernel
in `threadmaxx_simd` if a use case demands constant angular
velocity.

## Library version

```cpp
#include <threadmaxx_animation/version.hpp>

// Compile-time:
static_assert(THREADMAXX_ANIMATION_VERSION_MAJOR == 1);
#if THREADMAXX_ANIMATION_VERSION >= 10100  // require ≥ 1.1.0
   // ...
#endif

// Runtime:
std::printf("threadmaxx_animation v%s\n",
            threadmaxx::animation::version_string());
```

Version bumps follow [semver](https://semver.org/). See
`CHANGELOG.md` for the release history and
`MAINTAINER_GUIDE.md` for the full lifecycle policy.

## See also

- `README.md` — top-level overview.
- `DESIGN_NOTES.md` — the original spec.
- `FUTURE_WORK.md` — batch-by-batch landed work + v1.x candidates.
- `CHANGELOG.md` — per-release notes.
- `MAINTAINER_GUIDE.md` — how the evaluation model, registry,
  graph kernels, and crowd path are organized internally; how to
  add new node types and serialization fields.
- `tests/animation/*.cpp` — example usage of every public API.
- `bench/animation_crowd_bench.cpp` — crowd-evaluation perf
  harness.
