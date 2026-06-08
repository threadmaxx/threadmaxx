# `threadmaxx_animation` — Maintainer Guide

Internal documentation for engineers extending or debugging the
animation library. For external usage, see `USER_GUIDE.md`.

## Architecture overview

```
                ┌──────────────────────────────────────┐
                │  Consumer code                       │
                │  #include <threadmaxx_animation/...> │
                └────────────────┬─────────────────────┘
                                 ▼
        ┌──────────────────────────────────────────────────┐
        │  Public surface (one header per concern)         │
        │                                                  │
        │   pose.hpp        skeleton.hpp     clip.hpp      │
        │   registry.hpp    graph.hpp        eval.hpp      │
        │   blend.hpp       ik.hpp           warp.hpp      │
        │   diagnostics.hpp retarget.hpp     serialization.hpp │
        │   cloth.hpp       types.hpp        version.hpp   │
        └────────────────┬─────────────────────────────────┘
                         ▼
            ┌────────────────────────────────────┐
            │  detail/                            │
            │    pose_math.hpp  — lerp_pose,     │
            │                     blend_pose_*   │
            │    graph_eval.hpp — node walker    │
            │    fabrik.hpp     — FABRIK kernel  │
            └────────────────────────────────────┘
                         ▼
            ┌────────────────────────────────────┐
            │  threadmaxx::threadmaxx (math PODs) │
            │  Vec3, Quat — only dep at link     │
            └────────────────────────────────────┘
```

The library is a `STATIC` archive (`libthreadmaxx_animation.a`).
The CMake target `threadmaxx::animation` carries the include
directory, `cxx_std_20`, and a transitive link to
`threadmaxx::threadmaxx` (math PODs only — no engine subsystems).

## The evaluation model

### Two modes, mutually exclusive at any moment

1. **Single-clip mode** — `setClip(ref)` binds one ClipDesc. The
   Animator owns one playhead. Hot path is `advance(dt)` + `samplePose(out)`.

2. **Graph mode** — `setGraph(&graph)` binds an immutable graph.
   The Animator owns per-node playhead state. Hot path is
   `evaluate(EvalContext, PoseSpan out)`, which walks the graph in
   topological (post-order) order from Output back to the source
   nodes.

Switching modes resets internal playhead state. The two APIs do
NOT compose — mixing them on a single Animator is undefined.

### Graph evaluation order

Nodes are evaluated in registration order with a leaf-first walk:
the walker recursively evaluates each input node, caching its
output pose in an Animator-owned scratch buffer, then composes
the current node's output. Cycle detection is a registration-time
check (`AnimationGraph::connect` rejects edges that would close a
cycle). The Output node is the terminal sink — exactly one per
graph.

### Per-node playhead state

For nodes with time-dependent behavior (Clip, Blend1D, Warp),
per-Animator state lives in the Animator's playhead vector,
indexed by GraphNodeId. The graph itself stores authored defaults
only; per-instance overrides via `Animator::setParameter` populate
a small map that the eval walker consults before falling back to
the graph default.

This split keeps the graph reusable across thousands of agents in
crowd evaluation — the graph is read-only, every per-agent
mutation lives on the Animator.

## Registry threading contract

`AnimationRegistry` is single-threaded by contract. The expected
engine integration is:

- **preStep (sim thread, serial)**: register / unregister assets.
- **update (worker, parallel)**: borrow `getSkeleton(ref)` /
  `getClip(ref)` pointers; multiple workers reading is safe
  because no mutation happens during a wave.

Generation-tagged refs catch use-after-remove: every `add` slot
records a monotonically-increasing generation; `remove` bumps it.
A stale ref's generation no longer matches; `get*` returns
nullptr.

Why no internal mutex? The single-threaded sim-thread mutate +
parallel worker read pattern is the engine's standing discipline
for shared resources (see also the engine's
`ResourceRegistry` — `include/threadmaxx/Resource.hpp`). A mutex
here would only matter if registration happened mid-wave, which
the contract forbids.

## Hot-path allocation discipline

The crowd-eval bench (`bench/animation_crowd_bench.cpp`) measures
per-agent steady-state cost. The discipline that keeps it
allocation-free in steady state:

- **`PoseBuffer`** is caller-owned and sized once at agent
  creation. The library never resizes it implicitly.
- **`Animator::evaluate` / `samplePose`** writes into a
  caller-supplied `PoseSpan`. No internal allocation.
- **`evaluateCrowd`** uses a `thread_local` scratch buffer for
  intermediate node poses; first invocation may allocate but
  subsequent calls reuse.
- **Graph construction (`addClip` / `addBlend1D` / `connect`)**
  is setup-time only. Tests assert by counting allocations on
  the hot path under a custom allocator.

The "no allocation in hot path" guarantee is verified by
`tests/animation/test_animation_crowd_pool.cpp`.

## Adding a new graph node type

This is the most common extension. Roughly 6 steps. Use `Blend1D`
as the canonical example.

### 1. Add the enum tag in `graph.hpp`

```cpp
enum class NodeType : std::uint8_t {
    Clip = 0,
    Blend1D,
    Blend2D,
    // ...
    MyNewNode,  // add here
    Output,
};
```

Keep the existing values stable; new types append.

### 2. Define the payload POD

Either in `blend.hpp` (if blend-shaped) or a new header
(`mynode.hpp`):

```cpp
struct MyNewNode {
    std::vector<GraphNodeId> inputs;
    float                    authoredDefault;
    // ...
};
```

The payload owns no per-agent state — only authored data.

### 3. Add the graph builder method

```cpp
class AnimationGraph {
public:
    GraphNodeId addMyNewNode(MyNewNode payload);
};
```

The builder method allocates a new node slot, stores the payload,
returns the GraphNodeId.

### 4. Extend `detail/graph_eval.hpp`

Add a `case NodeType::MyNewNode:` arm in the node-dispatch
`switch`:

```cpp
case NodeType::MyNewNode: {
    const auto& payload = graph.getMyNewNode(node.id);
    // Evaluate inputs, compose into outPose.
    break;
}
```

The compiler will warn about a missing arm via
`-Wswitch` once the enum tag exists.

### 5. Add an Animator parameter slot if needed

If the node has a per-instance parameter (like Blend1D's `param`),
extend `Animator::setParameter` to recognize the parameter name
for that node's NodeType.

### 6. Test (`tests/animation/test_animation_my_new_node.cpp`)

Verify:
- Authored-only evaluation produces the expected pose.
- Per-instance override changes the output.
- Connecting to an invalid input GraphNodeId fails at
  `connect` time (not at evaluate).
- Two animators on the same graph with different parameters
  produce different poses.
- Crowd-batch path produces identical results to single-agent
  evaluate.

Register the test in `tests/animation/CMakeLists.txt`.

## Adding a new built-in pose component

If a future v1.x adds something like per-joint blendshape weights,
the model is to extend `JointPose`. This is a BREAKING change
(layout-changing) — bump MAJOR, document the migration in
`CHANGELOG.md`. The 40-byte `JointPose` POD is the v1.0 layout
contract.

Less invasive options to consider first:

- Add a side-band buffer (e.g., `std::span<BlendshapeWeights>`
  parallel to the joint span) that some nodes consume.
- Add a new node type that produces / consumes the new data
  without touching `JointPose`.

If neither fits, the v2.0 break is the right call.

## Crowd evaluation

`evaluateCrowd(span<AgentInput>, span<PoseSpan>, EvalContext)` is
the batch entry point. Its contract:

- Each agent evaluates independently — no cross-agent state.
- The shared `graph` is read-only across all agents.
- Per-agent overrides come from `AgentInput::parameterOverrides`.
- `AgentInput::lod` is consulted before evaluation; `Skipped`
  agents are no-ops, `Reduced` agents may take a cheaper path
  authored into the graph (a Layer node with a `lod` parameter is
  the standard pattern).

The v1.0 implementation runs the agents serially within the
caller's thread. Per-agent ordering is NOT observable — the
contract is "every agent ends up with its own correct pose
written to its `PoseSpan`," not "evaluate them in any particular
order." This leaves the door open for a future
JobSystem-dispatched parallel implementation without breaking
existing callers.

## Serialization format

`writeAnimationAssetBundle` produces a flat byte stream:

```
[magic 'AMTX' u32]
[version u32]
[skeletonCount u64]
  (per skeleton: u64 nameLen, name bytes, u64 jointCount,
   per joint: name (length-prefixed), parent i32, bindLocal POD,
   u64 bindGlobalCount, bindGlobal poses)*
[clipCount u64]
  (per clip: name, f32 duration, u8 looping,
   u64 eventCount, (time, name)* events,
   u64 jointCount, u64 keyframeTimeCount, f32 times*,
   u64 keyframeCount, JointPose keyframes*)*
```

All numeric fields are host-endian POD. The wire format is for
on-disk cache + bake-step output; cross-arch transfer is a v1.x
topic (byte-swapping reader gated on `version >= 2`).

Bumping `kAnimationAssetVersion` is mandatory on any field
addition. The reader rejects bad magic OR bad version OR
truncation and returns `std::nullopt`. Tests
(`test_animation_serialization_roundtrip.cpp`) pin all three
failure modes.

## Testing strategy

Three layers, all in `tests/animation/`:

1. **Unit tests** — one executable per public concept
   (pose math, clip sampling, graph wiring, IK convergence, warp
   window, diagnostics, retarget, serialization).
2. **Composition tests** — exercise the graph end-to-end
   (`test_animation_graph_basic`, `test_animation_layer_mask`,
   `test_animation_additive`).
3. **Crowd integration tests** — verify per-agent independence
   and pool reuse (`test_animation_crowd_smoke`,
   `test_animation_crowd_lod`, `test_animation_crowd_pool`).

All tests use the project-wide `Check.hpp` harness — one
executable per test, non-zero exit means failure.

### Werror tree

The library compiles clean under
`-DTHREADMAXX_WARNINGS_AS_ERRORS=ON` (which adds
`-Wsign-conversion -Wconversion -Wold-style-cast -Wshadow`). Use
the `build-werror/` tree as the discipline gate when touching
public surface — it surfaces silent narrowing conversions that
clang's default warnings miss.

```
cmake -B build-werror -DTHREADMAXX_WARNINGS_AS_ERRORS=ON
cmake --build build-werror -j
(cd build-werror && ctest -R '^animation\.' --output-on-failure)
```

## Repository layout (engineer-facing)

```
include/threadmaxx_animation/
├── README.md                 # Top-level overview
├── CHANGELOG.md              # Per-release notes
├── DESIGN_NOTES.md           # Original spec (don't edit unless re-scoping)
├── FUTURE_WORK.md            # Batch-by-batch landed work + v1.x candidates
├── USER_GUIDE.md             # User-facing docs
├── MAINTAINER_GUIDE.md       # This file
├── threadmaxx_animation.hpp  # Umbrella include
├── version.hpp               # Library version macros + version_string()
├── types.hpp                 # JointId / SkeletonRef / ClipRef / ...
├── pose.hpp                  # JointPose / Pose / PoseSpan / PoseBuffer
├── skeleton.hpp              # Joint / SkeletonDesc
├── clip.hpp                  # ClipSample / EventTrackEvent / ClipDesc
├── registry.hpp              # AnimationRegistry
├── blend.hpp                 # Blend1DNode / Blend2DNode / AdditiveNode / LayerNode
├── graph.hpp                 # AnimationGraph + NodeType
├── eval.hpp                  # Animator + EvalContext / EvalResult + evaluateCrowd
├── ik.hpp                    # solveIK / solveTwoBone / IKTarget
├── warp.hpp                  # applyWarp / WarpParams
├── diagnostics.hpp           # validatePose / PoseValidationReport / PoseIssue
├── retarget.hpp              # buildRetargetMap / retargetPose
├── serialization.hpp         # readAnimationAssetBundle / writeAnimationAssetBundle
├── cloth.hpp                 # ClothAttachment* / ClothSolverHooks (no solver)
└── detail/
    ├── pose_math.hpp         # lerp_pose / blend_pose_weighted
    ├── graph_eval.hpp        # Graph node-dispatch walker
    └── fabrik.hpp            # FABRIK kernel

src/threadmaxx_animation/
├── CMakeLists.txt
├── Registry.cpp
├── Animator.cpp
├── Graph.cpp
├── IK.cpp
├── Warp.cpp
└── Retarget.cpp

tests/animation/
├── CMakeLists.txt
├── test_animation_skeleton.cpp        # SkeletonDesc registration
├── test_animation_clip.cpp            # ClipDesc registration
├── test_animation_pose_buffer.cpp     # PoseBuffer reuse
├── test_animation_pose_math.cpp       # lerp / slerp / nlerp
├── test_animation_clip_sampling.cpp   # Keyframe interpolation
├── test_animation_event_tracks.cpp    # Event fire ordering
├── test_animation_graph_basic.cpp     # Clip → Output minimum
├── test_animation_graph_param.cpp     # setParameter override
├── test_animation_graph_dirty.cpp     # Dirty-flag short-circuit
├── test_animation_blend1d.cpp         # 1D parameter blend
├── test_animation_blend2d.cpp         # 2D IDW blend
├── test_animation_additive.cpp        # Base + delta composition
├── test_animation_layer_mask.cpp      # Per-joint mask layer
├── test_animation_ik_2bone.cpp        # Analytical 2-bone reference
├── test_animation_ik_chain.cpp        # FABRIK N-bone
├── test_animation_ik_unreachable.cpp  # Convergence on out-of-reach
├── test_animation_ik_weight.cpp       # IKTarget weight blending
├── test_animation_warp_root.cpp       # Root-joint translation warp
├── test_animation_warp_window.cpp     # Time-window envelope
├── test_animation_warp_blend.cpp      # Warp + base blend
├── test_animation_crowd_smoke.cpp     # evaluateCrowd basic
├── test_animation_crowd_lod.cpp       # AgentInput::lod filtering
├── test_animation_crowd_pool.cpp      # Pool reuse / no allocation
├── test_animation_diagnostics.cpp     # PoseIssue bit-set classifier
├── test_animation_retarget_smoke.cpp  # buildRetargetMap + retargetPose
└── test_animation_serialization_roundtrip.cpp  # Read/write/round-trip

bench/
└── animation_crowd_bench.cpp          # Per-agent cost at 1k / 8k
```

## Library version (`version.hpp`)

The library exposes a semver version via macros and a constexpr
function:

```cpp
#define THREADMAXX_ANIMATION_VERSION_MAJOR 1
#define THREADMAXX_ANIMATION_VERSION_MINOR 0
#define THREADMAXX_ANIMATION_VERSION_PATCH 0
#define THREADMAXX_ANIMATION_VERSION (MAJOR*10000 + MINOR*100 + PATCH)

constexpr const char* version_string() noexcept;  // → "1.0.0"
```

When bumping, update **both** the macros AND the string literal
returned by `version_string()`. Also append a section to
`CHANGELOG.md`.

## Versioning / ABI

The library produces a static archive; downstream callers
recompile against the headers, so source-ABI is what matters.

- **Public POD layouts** (`JointPose`, `SkeletonDesc`, `ClipDesc`,
  `EvalContext`, `EvalResult`, `IKTarget`, `WarpParams`,
  `PoseValidationReport`, `RetargetChannels`,
  `AnimationAssetBundle`, `ClothAttachmentPoint`,
  `ClothAttachmentSet`) are stable. Layout changes are breaking
  (bump MAJOR).
- **Public method signatures** are stable. Adding overloads is
  fine; changing existing signatures is breaking.
- **Enum values** (`NodeType`, `PoseIssue`) — existing values are
  stable; appending new values at the end is additive (MINOR).
- **`detail::*`** is internal. Consumers should not include
  `detail/` headers directly; tests and bench are allowed to.
- **Wire format constants** (`kAnimationAssetMagic`,
  `kAnimationAssetVersion`) — magic is stable, version bumps on
  any wire-format field addition.

When evolving:

- Add new graph node types via the workflow above. Append the
  enum tag; never reorder.
- Add new public functions via overloads or new headers. Removing
  is breaking — deprecate first, remove in the next major.
- Wire format changes bump `kAnimationAssetVersion` and the
  reader's accept-list.

## Common pitfalls

### "My graph evaluates the same pose every tick despite parameter changes"

The Animator caches the last evaluation in a dirty-flag short-
circuit. `setParameter` must flip the dirty flag — check
`test_animation_graph_dirty.cpp` for the contract pin. Custom
parameter writes that bypass `setParameter` (writing into a node's
payload directly) will NOT invalidate the cache.

### "Crowd evaluation produces correct results but allocates per agent"

The `thread_local` intermediate scratch buffer is per-thread, not
per-agent. If you're seeing per-agent allocations, check that:

- `PoseBuffer`s are pre-sized at agent construction.
- `AgentInput::parameterOverrides` is reusing its allocation
  (clear, don't reconstruct).
- The graph itself isn't being rebuilt mid-frame.

### "Serialization round-trip fails on a clip with events"

`EventTrackEvent::name` is a `std::string` — the writer
length-prefixes it. If you've forgotten to update the reader for
a new field (or added a field to `EventTrackEvent` without
bumping `kAnimationAssetVersion`), the read offset will drift
and the truncation check kicks in. The recipe in the "Adding a
new built-in pose component" section applies analogously here.

### "IK converges but the rotation reconstruction looks wrong"

v1.0 doesn't reconstruct rotations from FABRIK output — that's
the caller's problem. The IK solver moves chain points; the
caller is responsible for computing per-joint orientations that
align the bones with the new chain. A graph IK node that owns
both is a v1.x candidate.

### "Retargeting between two skeletons with different rest orientations produces twisted limbs"

v1.0's retargeting assumes identical rest orientations per joint
name. Axis remap is a v1.x topic (see `FUTURE_WORK.md` v1.x
candidates). Workaround for v1.0: pre-process the source clip to
match the destination skeleton's rest orientations.

### "Pose validation flags `DenormalRotation` on a manually-zeroed quaternion"

That's the intended behavior — `Quat{0, 0, 0, 0}` is degenerate
(can't normalize). Use `Quat{0, 0, 0, 1}` (identity) for "no
rotation". The threshold is `|q|² < 1e-6`.

### "Adding a new built-in component to `threadmaxx`'s core breaks animation tests"

Unlikely — the library depends on `Vec3` / `Quat` only. If a
core change touches those PODs, every sibling library will be
affected; that's an engine-wide breaking change.

## See also

- `DESIGN_NOTES.md` — original spec.
- `FUTURE_WORK.md` — batch-by-batch landed work + open follow-ups.
- `USER_GUIDE.md` — user-facing API reference.
- `/CLAUDE.md` (repo root) — meta-instructions for AI-assisted
  development of `threadmaxx` and its sibling libraries.
