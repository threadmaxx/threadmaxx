# `threadmaxx_animation` — batch plan

Sibling-library implementation plan. `DESIGN_NOTES.md` is the
authoritative spec; this doc breaks it down into shippable
test-driven batches.

Status: **A1 + A2 + A3 + A4 landed (2026-06-07)**. A5–A8 are 📋 planned.
Sequencing follows the §9 "implementation order" of the design
notes, regrouped into shippable units that each carry their own
tests.

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

## Batch A2 — Clip sampling + event tracks ✅ landed 2026-06-07

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

### A2 retrospective (2026-06-07)

- **Shipped**: `ClipDesc` gained `jointCount` + `keyframeTimes` +
  flat AoS `keyframes` (`kf[k * jointCount + j]`); new
  `detail/curve_eval.hpp` with `wrapTime` / `sampleClip` /
  `collectEvents`; new public `eval.hpp` with the `Animator`
  playhead; new `src/Animator.cpp`. Umbrella include now pulls in
  `eval.hpp`. Public headers + sources listed in
  `src/threadmaxx_animation/CMakeLists.txt`. Two new test executables
  (`test_animation_clip_sampling`, `test_animation_event_tracks`)
  wired into `tests/animation/CMakeLists.txt`. Full ctest 167/167
  green.
- **Event semantics**: events fire in the half-open window
  `(lastTime, newTime]` on forward motion. Looping wrap fires the
  tail window `(lastTime, duration]` plus the closed head window
  `[0, newTime]` so an event keyed at exactly `t=0` fires once per
  wrap (pinned by `test_animation_event_tracks` § 6). Backward
  motion (`advance(-dt)`, `setTime(earlier)`) fires nothing — the
  explicit-replay path the plan calls out is deliberately not
  exposed yet; the absence of a `replayEvents(from, to)` method is
  the contract for A2.
- **`Animator::setClip` resets**: switching clips zeros the
  playhead and drops pending events. Trading off a tiny ergonomic
  loss (no "preserve state when switching") against a much simpler
  invariant — A3's graph evaluation owns multi-clip blending, so
  the single-clip Animator doesn't need to model clip handoff.
- **Looping detection**: `advance` is the only path that sets the
  `wrapped` flag. `setTime` normalizes the same way but never
  flags a wrap → never fires events. This keeps "rewind via
  setTime" symmetric with "rewind via negative dt".
- **Zero-duration clip safety**: `wrapTime` short-circuits to 0,
  `sampleClip` copies the first keyframe. `advance` early-returns
  before any event scan. Pinned by
  `test_animation_clip_sampling` § 7.
- **No SoA refactor**: AoS won on plan-recommendation +
  profile-when-it-bites. The flat keyframe vector hands a
  `std::span<const JointPose>` straight into `detail::lerp_pose`,
  so the A1.x SIMD blend kernel path is already the natural
  upgrade — no API churn needed when it lands.
- **Soul check**: `Animator::samplePose` is `const noexcept` and
  writes into a caller-owned `PoseSpan`. `drainEvents` is the only
  state mutator outside of `advance` / `setTime` / `setClip`. The
  A7 crowd path can hand each worker its own private `Animator`
  slice and a private `PoseBuffer` with no engine-side
  synchronization beyond standard slice partitioning — the same
  pattern simd's kernels already use.

## Batch A3 — Graph + Animator + Clip/Output nodes ✅ landed 2026-06-07

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

### A3 retrospective (2026-06-07)

- **Shipped**: new public `graph.hpp` (`NodeType` enum with every
  future node kind declared, `GraphNodeId` with `kInvalidGraphNodeId`
  sentinel, `ClipNode` / `OutputNode` payloads, `AnimationGraph`
  class). New public `EvalContext` / `EvalResult` in `eval.hpp`,
  Animator extended with `setGraph` / `evaluate` / `setParameter` /
  `getParameter` / `nodeTime`. New `detail/graph_eval.hpp` with the
  `stepClipNode` helper. New `src/threadmaxx_animation/GraphEval.cpp`
  carrying both AnimationGraph method bodies and the Animator's
  graph-mode impl. Umbrella include + CMake updated. Three new test
  executables (`test_animation_graph_basic`,
  `test_animation_graph_param`, `test_animation_graph_dirty`); full
  ctest 170/170 green.
- **AnimationGraph is one graph, not a manager**: DESIGN_NOTES §5.3
  sketched a manager-style `AnimationGraph` that owns multiple
  graphs by `GraphId`. We diverge — `AnimationGraph` is a single
  graph. Rationale: A3's minimal Clip → Output topology is far
  cleaner without the indirection, and the engine integration in
  A7 will hand each agent its own graph pointer anyway (the
  manager is just a registry the game owns; the engine-side
  contract is "borrow a graph pointer"). The `GraphId` type is
  still reserved in `types.hpp` for the day a registry-style
  wrapper is wanted on top.
- **Per-instance state lives on the Animator, not the graph**:
  per-node playhead times and parameter overrides are
  `std::vector<float>`s indexed by node id, owned by the Animator.
  The bound `AnimationGraph` is logically `const` after
  `setGraph` — it's just the immutable template. A7's batch
  evaluation can spray N Animators across workers all pointing at
  one graph with no synchronization on the graph side.
- **Two-tier parameter resolution**: A Clip node has a default
  `playbackRate` baked into the graph (`AnimationGraph::setParameter`
  edits it). The Animator can override per-instance via
  `Animator::setParameter`. `getParameter` falls back to the graph
  default when no override is set. Override booleans live in
  `nodePlaybackRateOverridden_` so the default→override→default
  transitions work cleanly. Only `"playbackRate"` is recognized in
  A3 — unknown names are silently ignored, which lets future
  parameters land without breaking older callers.
- **EvalResult::dirty contract**: dirty is `true` iff (first
  evaluate after `setGraph`) OR (`ctx.dt != 0`) OR (any
  `setParameter` recognized a name change since the last
  evaluate). After a successful evaluate, both transient flags
  reset. Unrecognized parameter names don't flip the bit (pinned
  by `test_animation_graph_dirty` § 5).
- **NodeType enum carries every future variant up front**: the
  enum lists `Clip / Blend1D / Blend2D / Additive / Layer / IK /
  Warping / Output` even though only Clip and Output are
  implemented in A3. Switch statements on `NodeType` (currently
  just two arms in `evaluateInto`) are missing-case-warnings
  enforced via `-Wswitch` — A4/A5 batches will trigger compile
  errors if they forget to handle a new node kind. The "relay
  first input" fallthrough for unimplemented kinds is a
  belt-and-suspenders fallback to keep partially-built graphs
  from UB-ing if a forward-declared node accidentally lands in a
  graph today.
- **Recursive walk over Output → input**: A3's topology never
  exceeds depth 2 (Output → Clip), so the recursive
  `evaluateInto` walker is clearer than a topological-sort
  preprocess. `jointCount()` is also a bounded-depth walk
  (capped at 64 hops as a malformed-graph guard). A4 will revisit
  when blend nodes introduce multi-input dispatch — that's the
  natural point to introduce a real `topoOrder_` cache on the
  graph.
- **Event semantics carry over from A2**: each Clip node's event
  collection runs through the same `detail::collectEvents` the
  single-clip Animator uses, with the same wrap-aware semantics.
  Events from all clip nodes accumulate into `EvalResult::firedEvents`
  per-call. A4 blending will need a per-clip weight-gate (don't
  fire events for a clip with weight==0), but A3's single Clip
  source doesn't need it.
- **Mode switch resets symmetrically**: `setClip(...)` clears
  graph state and zeros all per-node playheads; `setGraph(...)`
  clears clip state and zeros the single-clip playhead. Mixing
  modes on one Animator is not supported by design (A2's
  `samplePose` and A3's `evaluate` write into the same pose
  buffer conceptually, but the playhead bookkeeping is fully
  disjoint). Switching back and forth is safe and produces clean
  zero-state, pinned by `test_animation_graph_basic` § 5.
- **PoseBuffer auto-resizes**: `evaluate` calls `outPose.resize`
  to match the graph's `jointCount()` if it doesn't already.
  Keeps the per-frame call site short — caller doesn't have to
  query joint count before each evaluate. The first allocation
  is the only allocation; subsequent calls hit the steady-state
  capacity.
- **Soul check**: graph-mode hot path is one bounded recursion
  (depth 2 today), one `stepClipNode` (`fmod` + linear keyframe
  scan + per-joint copy), and one event collection. Zero heap
  allocations after the first evaluate per Animator. The bound
  AnimationGraph is read-only during evaluation, so A7 crowd
  evaluation drops in with no further synchronization on the
  graph side — exactly the chunk-friendly pattern the design
  notes ask for.

## Batch A4 — Blend nodes + additive layers ✅ landed 2026-06-07

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

### A4 retrospective (2026-06-07)

- **Shipped**: new public `blend.hpp` with `Blend1DNode` /
  `Blend2DNode` / `AdditiveNode` / `LayerNode` POD payloads. `graph.hpp`
  gained `addBlend1D` / `addBlend2D` / `addAdditive` / `addLayer` /
  `setLayerMask` factory methods plus `blend1dNode` / `blend2dNode` /
  `additiveNode` / `layerNode` typed accessors. `detail/pose_math.hpp`
  gained `additive_pose` (TRS-aware additive composition) and
  `mask_blend_pose` (per-joint mask-gated lerp). `detail/graph_eval.hpp`
  now owns the `NodeRuntime` struct + per-parameter override-bit
  constants. `GraphEval.cpp` extended `evaluateInto` with Blend1D /
  Blend2D / Additive / Layer arms backed by a LIFO `ScratchPool` over
  Animator-owned reusable PoseBuffers. Umbrella include + CMake +
  4 new test executables; full ctest **174/174 green** (170 + 4).
- **`NodeRuntime` is detail-namespace, not nested-private**: A3 had
  three parallel vectors (`nodeTimes_`, `nodePlaybackRate_`,
  `nodePlaybackRateOverridden_`); A4 grew that to 8+ parameters
  (`param` / `x` / `y` / `weight` plus override flags). Refactored to
  a single `std::vector<detail::NodeRuntime>` keyed by node id. The
  struct lives in `detail/graph_eval.hpp` so the GraphEval helpers
  can poke its fields directly without a `friend` declaration or
  nested-private-type exposure on `Animator`. Public API stayed
  byte-identical — `Animator::setParameter` / `getParameter` /
  `nodeTime` carry the same signatures; only their bodies migrated to
  read/write `NodeRuntime` fields.
- **Two-tier parameter resolution scales naturally**: the same A3
  pattern (graph default → per-Animator override, fall back through
  the override-bit check) extended uncomplicatedly to four new
  parameter names. The `withDefault` lambda in `getParameter`
  centralizes the fallthrough so adding a fifth parameter is one
  enum bit + one `if` arm. Recognized names by node kind:
  Clip → `"playbackRate"`; Blend1D → `"param"`; Blend2D → `"x"`/`"y"`;
  Additive/Layer → `"weight"`. Cross-kind names (e.g.
  `setParameter(clipNode, "weight", v)`) are silently ignored, same
  as A3's unknown-name policy.
- **Blend2D uses inverse-distance weighting (IDW)**: each input's
  weight is `1 / (d² + ε)` where `d` is the Euclidean distance from
  the query point to the input's 2D position; the snap-eps path
  collapses to a single sample when the query is exactly on top of an
  authored point. Picked over barycentric (which needs an authored
  triangulation) and bilinear (which assumes a regular grid) because
  it accepts any point set the game ships. At the centroid of a
  regular point set all distances are equal so weights are uniform
  (test gate ✓). Bilinear's exactness at axis-aligned corners is
  unavailable here — the centroid test is uniform-within-tolerance
  rather than bit-exact. If a benched workload later cares about
  exact bilinear, the right move is to bake the choice as a Blend2D
  config flag, not to replace IDW.
- **N-input blends accumulate weighted into `out`**: for Blend2D's
  multi-input case the recursive walker would normally need N scratch
  buffers (one per input). Instead the loop seeds `out` with the
  first non-zero-weight input, then lerps subsequent inputs into
  `out` with running normalized weight `w_i / (W_so_far + w_i)`.
  After all inputs are folded in, `out` is the weight-sum-normalized
  blend. One scratch buffer regardless of input count → bounded
  scratch depth at evaluator runtime.
- **`mask_blend_pose` handles short masks gracefully**: per-joint
  flag `(i < mask.size()) && (mask[i] != 0)` so masks shorter than
  the pose simply treat the un-described joints as unmasked. Lets
  the Layer node ship before the skeleton is fully wired (mask
  defaults to empty → Layer is a no-op, base relays through), and
  matches the "default behavior is base-preserving" contract that
  authoring tools assume.
- **Additive scale composition is `base.S * lerp(1, delta.S, w)`**:
  multiplicative — `base.S=(2,2,2)` and `delta.S=(1.5,1.5,1.5)` at
  weight 1 yields `(3,3,3)` (pinned by test §5). The delta is
  expected to be authored as a difference from a neutral pose;
  treating a fully-baked clip as the delta produces
  double-rotation/double-scale artifacts. This matches the standard
  "additive bake" tooling convention.
- **Additive rotation: `base.R * nlerp(identity, delta.R, w)`**:
  sign-aligns `delta.R` against identity (negates if `delta.R.w < 0`)
  so the shorter-arc rotation is taken when the delta authored
  through the back hemisphere. Same nlerp instead of slerp tradeoff
  as A1's `detail::lerp` — sufficient for delta-rotation magnitudes
  the additive layer typically carries.
- **`ScratchPool` is Animator-owned, LIFO, grows lazily**: the
  recursive walker borrows a `std::vector<std::vector<JointPose>>&`
  on the Animator (`scratchPosePool_`), pushes a buffer on every
  `acquire(jointCount)`, releases LIFO. Reuse across ticks: zero
  steady-state allocation once the pool has grown to the maximum
  blend nesting depth × jointCount. Allocator pressure is bounded by
  graph depth, not graph node count — a graph with 1000 nodes but
  flat topology never pays for more than two scratch buffers.
- **`evaluateInto` arm coverage is `-Wswitch`-safe**: every NodeType
  is handled explicitly in `AnimationGraph::setParameter` /
  `getParameter`; future Node kinds (`IK`, `Warping`) trigger compile
  warnings when added. The `evaluateInto` fallthrough still relays
  the first input for `IK` / `Warping` so A5/A6 stubs land cleanly
  without breaking partial graphs.
- **PoseBuffer auto-resize ⇒ caller sees one allocation**: the same
  A3 contract holds — `evaluate` calls `outPose.resize(jc)` if
  needed. Steady state is alloc-free in both the caller's PoseBuffer
  and the Animator's scratch pool.
- **Soul check**: graph-mode hot path is one bounded recursion
  (depth = blend nesting), one `stepClipNode` per Clip leaf, IDW
  weight computation only for Blend2D nodes (small vector, no
  allocations after pool warm-up), and one `mask_blend_pose` per
  Layer node. Zero heap allocations after the first evaluate per
  Animator. A7's crowd evaluation still drops in unchanged: per-agent
  Animator + per-agent PoseBuffer + per-agent ScratchPool, all
  worker-private, zero engine-side synchronization on the graph.

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
