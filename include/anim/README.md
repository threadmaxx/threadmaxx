# `anim` — animation, IK, and pose evaluation sibling library

## 1. Purpose

`anim` provides runtime animation evaluation for games built on `threadmaxx`.

It is for:

* skeleton loading and binding,
* animation clips and events,
* state machines and blend trees,
* layered animation,
* additive poses,
* inverse kinematics,
* motion warping,
* cloth hooks or attachment hooks,
* pose caching for skinned rendering.

It is **not** for:

* rendering,
* physics simulation,
* ECS storage ownership,
* asset import pipelines,
* network replication,
* editor UI,
* owning the engine’s component model.

That matches the roadmap boundary: the engine should expose the animation-related component slots, but the animation math belongs in a sibling library. 

## 2. Design principles

1. **Above the engine.** No core engine changes beyond the component hooks already planned.
2. **Pose-first architecture.** Everything ends in a pose buffer the renderer can consume.
3. **Skeleton-agnostic evaluation.** Clips, graphs, IK, and modifiers all operate on the same pose representation.
4. **Chunk-friendly.** Batch evaluation should work over contiguous agent/entity spans.
5. **Zero allocation in the hot path.** Pose evaluation uses caller-owned buffers.
6. **Deterministic by default.** Same inputs, same pose output.
7. **Incremental evaluation.** Only dirty graphs and dirty poses recompute.
8. **Layered blending.** Base locomotion, upper-body overlays, facial layers, etc.
9. **No renderer coupling.** Skinning output is just data.
10. **Small public surface.** Prefer a few composable primitives over a giant framework.

## 3. Package layout

```text id="a4m7ty"
include/threadmaxx/anim/
  anim.hpp               // umbrella include
  types.hpp              // SkeletonId, ClipId, PoseId, GraphId, SlotId
  skeleton.hpp           // skeleton definitions and bindings
  clip.hpp               // animation clips and event tracks
  pose.hpp               // pose buffers, joints, blend weights
  graph.hpp              // state machines and blend trees
  blend.hpp              // layered/additive blending helpers
  ik.hpp                 // IK constraints and solvers
  warp.hpp               // motion warping helpers
  cloth.hpp              // attachment / cloth influence hooks
  retarget.hpp           // skeleton retargeting helpers
  eval.hpp               // graph evaluation API
  serialization.hpp      // save/load for animation assets
  diagnostics.hpp        // debug poses, timing, validation
  detail/
    hash.hpp
    curve_eval.hpp
    pose_math.hpp
    graph_eval.hpp
    job_batch.hpp
```

If you want runtime assets and offline baking separated, split import tools into a `tools/` or `src/` target.

---

## 4. Core data model

### 4.1 Skeletons

Skeletons define joint hierarchy and bind poses.

```cpp id="q2v8nm"
namespace threadmaxx::anim {

using SkeletonId = std::uint64_t;
using ClipId = std::uint64_t;
using GraphId = std::uint64_t;
using PoseId = std::uint64_t;

struct JointId {
    std::uint32_t value{};
};

struct SkeletonRef {
    SkeletonId id{};
    std::uint32_t generation{};
};

struct Joint {
    std::string name;
    int parent = -1;
    Mat4 bindLocal{};
};

struct SkeletonDesc {
    std::string name;
    std::vector<Joint> joints;
    std::vector<Mat4> bindGlobal;
};

} // namespace threadmaxx::anim
```

### 4.2 Clips

Clips are sampled animation curves over time.

```cpp id="l1b3cd"
namespace threadmaxx::anim {

struct ClipSample {
    PoseId pose{};
    float time{};
};

struct EventTrackEvent {
    float time{};
    std::string name;
};

struct ClipDesc {
    std::string name;
    float duration{};
    bool looping{};
    std::vector<EventTrackEvent> events;
};

} // namespace threadmaxx::anim
```

### 4.3 Poses

A pose is the runtime output: per-joint local transforms plus metadata.

```cpp id="m9t6re"
namespace threadmaxx::anim {

struct JointPose {
    Vec3 translation{};
    Quat rotation{};
    Vec3 scale{1.0f, 1.0f, 1.0f};
};

struct Pose {
    std::vector<JointPose> joints;
    float weight = 1.0f;
    bool valid = false;
};

struct PoseSpan {
    std::span<JointPose> joints;
};

} // namespace threadmaxx::anim
```

This is the object the renderer can skin from, and it lines up with the roadmap’s `AnimationPoseRef` render-side hook. 

---

## 5. Public API

### 5.1 Registry and asset access

```cpp id="v5x1s7"
namespace threadmaxx::anim {

class AnimationRegistry {
public:
    SkeletonRef addSkeleton(SkeletonDesc desc);
    ClipId addClip(ClipDesc desc);

    bool isValid(SkeletonRef skeleton) const noexcept;
    bool isValid(ClipId clip) const noexcept;

    const SkeletonDesc* getSkeleton(SkeletonRef skeleton) const noexcept;
    const ClipDesc* getClip(ClipId clip) const noexcept;
};

} // namespace threadmaxx::anim
```

### 5.2 Pose buffers

Pose buffers should be caller-owned and reusable.

```cpp id="r8d3fw"
namespace threadmaxx::anim {

class PoseBuffer {
public:
    PoseBuffer() = default;
    explicit PoseBuffer(std::size_t jointCount);

    void resize(std::size_t jointCount);
    std::size_t size() const noexcept;

    PoseSpan localPose();
    std::span<const JointPose> localPose() const noexcept;
};

} // namespace threadmaxx::anim
```

### 5.3 Graph evaluation

A graph drives the final pose.

```cpp id="k6h2jp"
namespace threadmaxx::anim {

enum class NodeType : std::uint8_t {
    Clip,
    Blend2D,
    Blend1D,
    Additive,
    Layer,
    IK,
    Warping,
    Output
};

struct GraphNodeId {
    std::uint32_t value{};
};

struct GraphDesc {
    std::string name;
    SkeletonRef skeleton{};
};

class AnimationGraph {
public:
    GraphId create(GraphDesc desc);
    void destroy(GraphId id);

    template<class Node>
    GraphNodeId addNode(GraphId graph, Node node);

    void connect(GraphId graph, GraphNodeId from, GraphNodeId to);
    void setOutput(GraphId graph, GraphNodeId node);
};

} // namespace threadmaxx::anim
```

### 5.4 Evaluation API

This is the core runtime call.

```cpp id="n1y5vh"
namespace threadmaxx::anim {

struct EvalContext {
    float dt{};
    float time{};
    float globalWeight = 1.0f;
};

struct EvalResult {
    PoseId outputPose{};
    std::vector<EventTrackEvent> firedEvents;
    bool dirty = false;
};

class Animator {
public:
    EvalResult evaluate(GraphId graph,
                        EvalContext ctx,
                        PoseBuffer& outPose);

    void setParameter(GraphId graph, std::string_view name, float value);
    void setParameter(GraphId graph, std::string_view name, bool value);
    void setParameter(GraphId graph, std::string_view name, Vec3 value);
};

} // namespace threadmaxx::anim
```

### 5.5 IK and constraints

Keep IK as a small constraint layer, not a giant solver framework.

```cpp id="u3q4fn"
namespace threadmaxx::anim {

struct IKTarget {
    JointId joint{};
    Vec3 position{};
    Quat rotation{};
    float weight = 1.0f;
};

struct IKChain {
    std::vector<JointId> joints;
    IKTarget target;
    float maxStretch = 1.0f;
};

void solveIK(const SkeletonDesc& skeleton,
             PoseSpan pose,
             std::span<const IKChain> chains);

} // namespace threadmaxx::anim
```

### 5.6 Motion warping

Useful for foot placement, attack alignment, and reach correction.

```cpp id="d7m2kc"
namespace threadmaxx::anim {

struct WarpRequest {
    Vec3 from{};
    Vec3 to{};
    float startTime{};
    float endTime{};
};

void applyWarp(PoseSpan pose,
               const SkeletonDesc& skeleton,
               const WarpRequest& request);

} // namespace threadmaxx::anim
```

---

## 6. Integration with `threadmaxx`

The roadmap already says the engine can host animation as user systems once a `Skeleton` / `AnimationState` shape exists, but the engine should not own the math. That is the right boundary to preserve. 

### 6.1 Component-driven use

A game system can look like this:

```cpp id="z5x9ca"
class AnimationSystem final : public threadmaxx::ISystem {
public:
    void update(threadmaxx::SystemContext& ctx) override {
        ctx.forEachChunk<
            threadmaxx::AnimationStateRef,
            threadmaxx::Transform
        >([&](auto& chunk) {
            auto states = chunk.span<threadmaxx::AnimationStateRef>();
            auto transforms = chunk.span<threadmaxx::Transform>();

            // Evaluate graphs, write back root motion / pose refs.
        });
    }
};
```

### 6.2 Render bridge

The render contract already expects `AnimationPoseRef` and skinned draw items in the render frame. `anim` should produce exactly that kind of pose output, then hand it to the render-side code. 

### 6.3 Job-friendly batch evaluation

Evaluation should support batch work over many agents:

* sample clips in parallel,
* evaluate graphs in parallel,
* run IK per agent or per rig batch,
* build final pose buffers for the renderer.

That fits the engine’s worker model and chunked storage model.

---

## 7. Runtime modes

### 7.1 Direct clip playback

The simplest mode: one clip, one skeleton, one output pose.

### 7.2 Graph-driven animation

State machines select clip nodes and blend trees.

### 7.3 Additive layering

Upper-body overlays, recoil, facial layers, injury layers.

### 7.4 Procedural correction

IK and warping adjust the final pose without changing the authored source.

### 7.5 Crowd animation

Many NPCs share the same skeleton and clips, but get staggered time offsets and low-cost LOD evaluation.

---

## 8. What the library should not do

* no rendering backend,
* no physics solver,
* no collision authority,
* no full editor UI,
* no asset import pipeline ownership,
* no ECS storage ownership,
* no network replication,
* no hardcoded skeleton format,
* no dependence on one specific renderer’s skinned-pose layout.

That is exactly consistent with the roadmap’s separation of concerns. 

---

## 9. Implementation order

1. skeleton and clip registry,
2. pose buffer and pose math,
3. clip sampling,
4. simple graph evaluation,
5. blend nodes and additive layers,
6. IK constraints,
7. motion warping,
8. event tracks,
9. batch evaluation,
10. diagnostics and validation.

---

## 10. Tests to add

* clip sampling round-trip tests,
* skeleton hierarchy validation,
* blend tree correctness tests,
* additive layer composition tests,
* IK convergence tests,
* motion warping regression tests,
* event-track firing tests,
* pose determinism tests,
* batch-evaluation consistency tests,
* render bridge smoke tests with skinned draw items.
