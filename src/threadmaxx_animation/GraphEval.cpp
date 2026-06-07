#include "threadmaxx_animation/eval.hpp"
#include "threadmaxx_animation/graph.hpp"

#include "threadmaxx_animation/detail/graph_eval.hpp"
#include "threadmaxx_animation/detail/pose_math.hpp"

#include <algorithm>
#include <cstddef>
#include <span>
#include <utility>
#include <vector>

namespace threadmaxx::animation {

// === AnimationGraph ===

GraphNodeId AnimationGraph::addClip(const ClipDesc* clip, float playbackRate) {
    Node n;
    n.type = NodeType::Clip;
    n.clip.clip = clip;
    n.clip.playbackRate = playbackRate;
    nodes_.push_back(std::move(n));
    return GraphNodeId{static_cast<std::uint32_t>(nodes_.size() - 1)};
}

GraphNodeId AnimationGraph::addOutput() {
    Node n;
    n.type = NodeType::Output;
    nodes_.push_back(std::move(n));
    return GraphNodeId{static_cast<std::uint32_t>(nodes_.size() - 1)};
}

GraphNodeId AnimationGraph::addBlend1D(std::span<const float> thresholds) {
    Node n;
    n.type = NodeType::Blend1D;
    n.blend1d.thresholds.assign(thresholds.begin(), thresholds.end());
    nodes_.push_back(std::move(n));
    return GraphNodeId{static_cast<std::uint32_t>(nodes_.size() - 1)};
}

GraphNodeId AnimationGraph::addBlend2D(std::span<const float> positionsX,
                                       std::span<const float> positionsY) {
    Node n;
    n.type = NodeType::Blend2D;
    n.blend2d.positionsX.assign(positionsX.begin(), positionsX.end());
    n.blend2d.positionsY.assign(positionsY.begin(), positionsY.end());
    nodes_.push_back(std::move(n));
    return GraphNodeId{static_cast<std::uint32_t>(nodes_.size() - 1)};
}

GraphNodeId AnimationGraph::addAdditive(float weight) {
    Node n;
    n.type = NodeType::Additive;
    n.additive.weight = weight;
    nodes_.push_back(std::move(n));
    return GraphNodeId{static_cast<std::uint32_t>(nodes_.size() - 1)};
}

GraphNodeId AnimationGraph::addLayer(float weight) {
    Node n;
    n.type = NodeType::Layer;
    n.layer.weight = weight;
    nodes_.push_back(std::move(n));
    return GraphNodeId{static_cast<std::uint32_t>(nodes_.size() - 1)};
}

void AnimationGraph::setLayerMask(GraphNodeId node, std::span<const std::uint8_t> mask) {
    if (!isValid(node)) return;
    Node& n = nodes_[node.value];
    if (n.type != NodeType::Layer) return;
    n.layer.mask.assign(mask.begin(), mask.end());
}

void AnimationGraph::connect(GraphNodeId from, GraphNodeId to) {
    if (!isValid(from) || !isValid(to)) return;
    nodes_[to.value].inputs.push_back(from);
}

void AnimationGraph::setParameter(GraphNodeId node, std::string_view name, float value) {
    if (!isValid(node)) return;
    Node& n = nodes_[node.value];
    switch (n.type) {
        case NodeType::Clip:
            if (name == "playbackRate") n.clip.playbackRate = value;
            return;
        case NodeType::Blend1D:
            if (name == "param") n.blend1d.param = value;
            return;
        case NodeType::Blend2D:
            if (name == "x") n.blend2d.x = value;
            else if (name == "y") n.blend2d.y = value;
            return;
        case NodeType::Additive:
            if (name == "weight") n.additive.weight = value;
            return;
        case NodeType::Layer:
            if (name == "weight") n.layer.weight = value;
            return;
        case NodeType::Output:
        case NodeType::IK:
        case NodeType::Warping:
            return;
    }
}

std::optional<float> AnimationGraph::getParameter(GraphNodeId node,
                                                  std::string_view name) const {
    if (!isValid(node)) return std::nullopt;
    const Node& n = nodes_[node.value];
    switch (n.type) {
        case NodeType::Clip:
            if (name == "playbackRate") return n.clip.playbackRate;
            break;
        case NodeType::Blend1D:
            if (name == "param") return n.blend1d.param;
            break;
        case NodeType::Blend2D:
            if (name == "x") return n.blend2d.x;
            if (name == "y") return n.blend2d.y;
            break;
        case NodeType::Additive:
            if (name == "weight") return n.additive.weight;
            break;
        case NodeType::Layer:
            if (name == "weight") return n.layer.weight;
            break;
        case NodeType::Output:
        case NodeType::IK:
        case NodeType::Warping:
            break;
    }
    return std::nullopt;
}

NodeType AnimationGraph::nodeType(GraphNodeId id) const noexcept {
    if (!isValid(id)) return NodeType::Output;  // arbitrary; caller checks valid()
    return nodes_[id.value].type;
}

const ClipNode* AnimationGraph::clipNode(GraphNodeId id) const noexcept {
    if (!isValid(id)) return nullptr;
    const Node& n = nodes_[id.value];
    if (n.type != NodeType::Clip) return nullptr;
    return &n.clip;
}

const Blend1DNode* AnimationGraph::blend1dNode(GraphNodeId id) const noexcept {
    if (!isValid(id)) return nullptr;
    const Node& n = nodes_[id.value];
    if (n.type != NodeType::Blend1D) return nullptr;
    return &n.blend1d;
}

const Blend2DNode* AnimationGraph::blend2dNode(GraphNodeId id) const noexcept {
    if (!isValid(id)) return nullptr;
    const Node& n = nodes_[id.value];
    if (n.type != NodeType::Blend2D) return nullptr;
    return &n.blend2d;
}

const AdditiveNode* AnimationGraph::additiveNode(GraphNodeId id) const noexcept {
    if (!isValid(id)) return nullptr;
    const Node& n = nodes_[id.value];
    if (n.type != NodeType::Additive) return nullptr;
    return &n.additive;
}

const LayerNode* AnimationGraph::layerNode(GraphNodeId id) const noexcept {
    if (!isValid(id)) return nullptr;
    const Node& n = nodes_[id.value];
    if (n.type != NodeType::Layer) return nullptr;
    return &n.layer;
}

std::span<const GraphNodeId> AnimationGraph::inputs(GraphNodeId id) const noexcept {
    if (!isValid(id)) return {};
    return std::span<const GraphNodeId>(nodes_[id.value].inputs);
}

std::uint32_t AnimationGraph::jointCount() const noexcept {
    if (!output_.valid()) return 0;
    // DFS toward the first reachable Clip node — every blend topology
    // in A4 still funnels into one or more Clips at the leaves, and
    // every Clip in a well-formed graph has the same jointCount.
    // Bounded depth catches cyclic / malformed graphs.
    GraphNodeId cur = output_;
    for (int hops = 0; hops < 64; ++hops) {
        if (!isValid(cur)) return 0;
        const Node& n = nodes_[cur.value];
        if (n.type == NodeType::Clip) {
            return (n.clip.clip != nullptr) ? n.clip.clip->jointCount : 0;
        }
        if (n.inputs.empty()) return 0;
        cur = n.inputs.front();
    }
    return 0;
}

// === Animator: graph mode ===

void Animator::setGraph(const AnimationGraph* graph) noexcept {
    // Switching modes (or graphs) zeros all per-node state and drops
    // the single-clip binding.
    clip_ = nullptr;
    time_ = 0.0f;
    pendingEvents_.clear();

    graph_ = graph;
    nodeRuntime_.clear();
    if (graph_ != nullptr) {
        nodeRuntime_.assign(graph_->nodeCount(), detail::NodeRuntime{});
    }
    firstEvalAfterSetGraph_ = (graph_ != nullptr);
    paramsChangedSinceLastEval_ = false;
}

void Animator::setParameter(GraphNodeId node, std::string_view name, float value) {
    if (graph_ == nullptr) return;
    if (!node.valid() || node.value >= nodeRuntime_.size()) return;
    detail::NodeRuntime& r = nodeRuntime_[node.value];
    const NodeType t = graph_->nodeType(node);
    bool changed = false;
    if (name == "playbackRate" && t == NodeType::Clip) {
        r.playbackRate = value;
        r.overrideMask |= detail::kRateOverride;
        changed = true;
    } else if (name == "param" && t == NodeType::Blend1D) {
        r.param = value;
        r.overrideMask |= detail::kParamOverride;
        changed = true;
    } else if (name == "x" && t == NodeType::Blend2D) {
        r.blendX = value;
        r.overrideMask |= detail::kXOverride;
        changed = true;
    } else if (name == "y" && t == NodeType::Blend2D) {
        r.blendY = value;
        r.overrideMask |= detail::kYOverride;
        changed = true;
    } else if (name == "weight" &&
               (t == NodeType::Additive || t == NodeType::Layer)) {
        r.weight = value;
        r.overrideMask |= detail::kWeightOverride;
        changed = true;
    }
    if (changed) paramsChangedSinceLastEval_ = true;
}

float Animator::getParameter(GraphNodeId node, std::string_view name) const {
    if (graph_ == nullptr) return 0.0f;
    if (!node.valid() || node.value >= nodeRuntime_.size()) return 0.0f;
    const detail::NodeRuntime& r = nodeRuntime_[node.value];
    const NodeType t = graph_->nodeType(node);
    auto withDefault = [&](std::uint8_t bit, float overrideValue) -> float {
        if (r.overrideMask & bit) return overrideValue;
        if (auto v = graph_->getParameter(node, name)) return *v;
        return 0.0f;
    };
    if (name == "playbackRate" && t == NodeType::Clip) {
        return withDefault(detail::kRateOverride, r.playbackRate);
    }
    if (name == "param" && t == NodeType::Blend1D) {
        return withDefault(detail::kParamOverride, r.param);
    }
    if (name == "x" && t == NodeType::Blend2D) {
        return withDefault(detail::kXOverride, r.blendX);
    }
    if (name == "y" && t == NodeType::Blend2D) {
        return withDefault(detail::kYOverride, r.blendY);
    }
    if (name == "weight" && (t == NodeType::Additive || t == NodeType::Layer)) {
        return withDefault(detail::kWeightOverride, r.weight);
    }
    return 0.0f;
}

float Animator::nodeTime(GraphNodeId node) const noexcept {
    if (graph_ == nullptr) return 0.0f;
    if (!node.valid() || node.value >= nodeRuntime_.size()) return 0.0f;
    return nodeRuntime_[node.value].time;
}

namespace {

// Two-tier resolution mirror of Animator::getParameter, hot-loop
// version: takes the per-node runtime + the graph node directly so the
// recursive walker doesn't pay the public-API string-compare cost per
// node visit.
float resolvePlaybackRate(const detail::NodeRuntime& rt, const ClipNode& cn) noexcept {
    return (rt.overrideMask & detail::kRateOverride) ? rt.playbackRate
                                                       : cn.playbackRate;
}
float resolveParam(const detail::NodeRuntime& rt, const Blend1DNode& bn) noexcept {
    return (rt.overrideMask & detail::kParamOverride) ? rt.param : bn.param;
}
float resolveBlendX(const detail::NodeRuntime& rt, const Blend2DNode& bn) noexcept {
    return (rt.overrideMask & detail::kXOverride) ? rt.blendX : bn.x;
}
float resolveBlendY(const detail::NodeRuntime& rt, const Blend2DNode& bn) noexcept {
    return (rt.overrideMask & detail::kYOverride) ? rt.blendY : bn.y;
}
float resolveAdditiveWeight(const detail::NodeRuntime& rt,
                            const AdditiveNode& an) noexcept {
    return (rt.overrideMask & detail::kWeightOverride) ? rt.weight : an.weight;
}
float resolveLayerWeight(const detail::NodeRuntime& rt,
                         const LayerNode& ln) noexcept {
    return (rt.overrideMask & detail::kWeightOverride) ? rt.weight : ln.weight;
}

// LIFO scratch pool for blend evaluation. `top` is the index of the
// next unused buffer. Each acquire grows the pool if needed; release
// just decrements top.
struct ScratchPool {
    std::vector<std::vector<JointPose>>& buffers;
    std::size_t top = 0;

    std::span<JointPose> acquire(std::size_t jointCount) {
        if (top >= buffers.size()) buffers.emplace_back();
        auto& v = buffers[top++];
        if (v.size() != jointCount) v.resize(jointCount);
        return std::span<JointPose>(v);
    }
    void release() noexcept { if (top > 0) --top; }
};

// Resolve Blend1D weights for a parameter value across `n` thresholds.
// thresholds[0..n-1] sorted ascending. Outside the range the nearest
// endpoint takes full weight; inside the range the bracketing pair
// (k, k+1) splits weight by (param - thresholds[k]) / span.
struct Blend1DWeights {
    std::size_t idxA = 0;
    std::size_t idxB = 0;
    float alpha = 0.0f;  // weight on idxB; (1-alpha) on idxA
};
Blend1DWeights computeBlend1D(std::span<const float> thresholds, float param) noexcept {
    Blend1DWeights r;
    const std::size_t n = thresholds.size();
    if (n == 0) return r;
    if (n == 1) { r.idxA = r.idxB = 0; r.alpha = 0.0f; return r; }
    if (param <= thresholds.front()) { r.idxA = r.idxB = 0; return r; }
    if (param >= thresholds.back())  { r.idxA = r.idxB = n - 1; return r; }
    for (std::size_t i = 0; i + 1 < n; ++i) {
        if (thresholds[i] <= param && param <= thresholds[i + 1]) {
            r.idxA = i;
            r.idxB = i + 1;
            const float span = thresholds[i + 1] - thresholds[i];
            r.alpha = (span > 0.0f) ? (param - thresholds[i]) / span : 0.0f;
            return r;
        }
    }
    return r;
}

// IDW (inverse-distance squared) weight computation for Blend2D. At a
// regular point set's centroid all distances are equal so weights are
// uniform; at a sample point the small epsilon prevents div-by-zero
// and the near-sample collapses to ~full weight. Output written into
// `weights[0..n-1]`, normalized to sum 1.
void computeBlend2D(std::span<const float> xs,
                    std::span<const float> ys,
                    float qx, float qy,
                    std::vector<float>& weights) {
    const std::size_t n = std::min(xs.size(), ys.size());
    weights.assign(n, 0.0f);
    if (n == 0) return;

    constexpr float kEps = 1e-6f;
    constexpr float kSnapEps = 1e-12f;

    // If query is essentially on top of a sample point, snap to it.
    for (std::size_t i = 0; i < n; ++i) {
        const float dx = qx - xs[i];
        const float dy = qy - ys[i];
        const float d2 = dx * dx + dy * dy;
        if (d2 < kSnapEps) {
            weights[i] = 1.0f;
            return;
        }
    }

    float sum = 0.0f;
    for (std::size_t i = 0; i < n; ++i) {
        const float dx = qx - xs[i];
        const float dy = qy - ys[i];
        const float d2 = dx * dx + dy * dy + kEps;
        const float w = 1.0f / d2;
        weights[i] = w;
        sum += w;
    }
    if (sum > 0.0f) {
        const float inv = 1.0f / sum;
        for (std::size_t i = 0; i < n; ++i) weights[i] *= inv;
    }
}

// Lerp `dst = lerp(dst, src, w)` componentwise, for accumulating a
// weighted blend over many inputs. Used by Blend2D to fold N inputs
// into one pose without N intermediate scratch buffers.
void accumulateWeighted(std::span<JointPose> dst,
                        std::span<const JointPose> src,
                        float w) noexcept {
    using detail::lerp;
    for (std::size_t j = 0; j < dst.size(); ++j) {
        dst[j] = lerp(dst[j], src[j], w);
    }
}

void copyPose(std::span<const JointPose> src, std::span<JointPose> dst) noexcept {
    for (std::size_t j = 0; j < dst.size(); ++j) dst[j] = src[j];
}

// Recursively evaluate `nodeId`'s output and write it into `out`.
// Handles Clip / Output / Blend1D / Blend2D / Additive / Layer. Unknown
// node types (IK, Warping — reserved for A5/A6) fall through to a
// "relay first input" pass so a forward-declared graph still produces
// something rather than UB.
void evaluateInto(const AnimationGraph& g,
                  GraphNodeId nodeId,
                  float dt,
                  std::span<JointPose> out,
                  std::vector<EventTrackEvent>& firedEvents,
                  std::span<detail::NodeRuntime> runtime,
                  ScratchPool& scratch) {
    if (!nodeId.valid()) return;
    const NodeType t = g.nodeType(nodeId);

    if (t == NodeType::Clip) {
        const ClipNode* cn = g.clipNode(nodeId);
        if (cn == nullptr || cn->clip == nullptr) return;
        detail::NodeRuntime& rt = runtime[nodeId.value];
        const float rate = resolvePlaybackRate(rt, *cn);
        const float oldTime = rt.time;
        const float newTime = detail::stepClipNode(*cn, oldTime, dt, rate, out);
        rt.time = newTime;

        const ClipDesc& clip = *cn->clip;
        if (clip.duration > 0.0f && !clip.events.empty() && dt != 0.0f) {
            const float duration = clip.duration;
            bool wrapped = false;
            if (clip.looping) {
                const float raw = oldTime + dt * rate;
                if (raw >= duration || raw < 0.0f) wrapped = true;
            }
            detail::collectEvents(clip, oldTime, newTime, wrapped, firedEvents);
        }
        return;
    }

    if (t == NodeType::Output) {
        auto ins = g.inputs(nodeId);
        if (ins.empty()) return;
        evaluateInto(g, ins.front(), dt, out, firedEvents, runtime, scratch);
        return;
    }

    if (t == NodeType::Blend1D) {
        const Blend1DNode* bn = g.blend1dNode(nodeId);
        const auto ins = g.inputs(nodeId);
        if (bn == nullptr || ins.empty()) return;
        const detail::NodeRuntime& rt = runtime[nodeId.value];
        const float param = resolveParam(rt, *bn);
        const auto w = computeBlend1D(bn->thresholds, param);
        if (w.idxA >= ins.size()) return;
        if (w.idxA == w.idxB || w.idxB >= ins.size()) {
            // Endpoint or single-input case — just relay input idxA.
            evaluateInto(g, ins[w.idxA], dt, out, firedEvents, runtime, scratch);
            return;
        }
        auto scratchA = scratch.acquire(out.size());
        auto scratchB = scratch.acquire(out.size());
        evaluateInto(g, ins[w.idxA], dt, scratchA, firedEvents, runtime, scratch);
        evaluateInto(g, ins[w.idxB], dt, scratchB, firedEvents, runtime, scratch);
        detail::lerp_pose(scratchA, scratchB, w.alpha, out);
        scratch.release();
        scratch.release();
        return;
    }

    if (t == NodeType::Blend2D) {
        const Blend2DNode* bn = g.blend2dNode(nodeId);
        const auto ins = g.inputs(nodeId);
        if (bn == nullptr || ins.empty()) return;
        const detail::NodeRuntime& rt = runtime[nodeId.value];
        const float qx = resolveBlendX(rt, *bn);
        const float qy = resolveBlendY(rt, *bn);
        std::vector<float> weights;
        computeBlend2D(bn->positionsX, bn->positionsY, qx, qy, weights);
        const std::size_t n = std::min(weights.size(), ins.size());
        if (n == 0) return;

        // Two-pose accumulation: seed `out` with the first weighted
        // input, then lerp subsequent inputs into it with normalized
        // running weight `w_i / (W_so_far + w_i)`. After the loop, `out`
        // is the weight-sum-normalized blend.
        auto scratchIn = scratch.acquire(out.size());
        float accumulated = 0.0f;
        for (std::size_t i = 0; i < n; ++i) {
            const float wi = weights[i];
            if (wi <= 0.0f) continue;
            evaluateInto(g, ins[i], dt, scratchIn, firedEvents, runtime, scratch);
            if (accumulated <= 0.0f) {
                copyPose(scratchIn, out);
                accumulated = wi;
            } else {
                const float alpha = wi / (accumulated + wi);
                accumulateWeighted(out, scratchIn, alpha);
                accumulated += wi;
            }
        }
        scratch.release();
        return;
    }

    if (t == NodeType::Additive) {
        const AdditiveNode* an = g.additiveNode(nodeId);
        const auto ins = g.inputs(nodeId);
        if (an == nullptr || ins.size() < 2) {
            // Missing delta — just relay the base (or first input).
            if (!ins.empty()) {
                evaluateInto(g, ins.front(), dt, out, firedEvents, runtime, scratch);
            }
            return;
        }
        const detail::NodeRuntime& rt = runtime[nodeId.value];
        const float w = resolveAdditiveWeight(rt, *an);
        auto deltaBuf = scratch.acquire(out.size());
        evaluateInto(g, ins[0], dt, out, firedEvents, runtime, scratch);
        evaluateInto(g, ins[1], dt, deltaBuf, firedEvents, runtime, scratch);
        // `out` is already the base; additive_pose reads base + delta
        // and writes back to `out`. Read base into a scratch so the
        // base/out aliasing doesn't matter for the additive math.
        auto baseBuf = scratch.acquire(out.size());
        copyPose(out, baseBuf);
        detail::additive_pose(baseBuf, deltaBuf, w, out);
        scratch.release();
        scratch.release();
        return;
    }

    if (t == NodeType::Layer) {
        const LayerNode* ln = g.layerNode(nodeId);
        const auto ins = g.inputs(nodeId);
        if (ln == nullptr || ins.size() < 2) {
            if (!ins.empty()) {
                evaluateInto(g, ins.front(), dt, out, firedEvents, runtime, scratch);
            }
            return;
        }
        const detail::NodeRuntime& rt = runtime[nodeId.value];
        const float w = resolveLayerWeight(rt, *ln);
        auto overlayBuf = scratch.acquire(out.size());
        evaluateInto(g, ins[0], dt, out, firedEvents, runtime, scratch);
        evaluateInto(g, ins[1], dt, overlayBuf, firedEvents, runtime, scratch);
        auto baseBuf = scratch.acquire(out.size());
        copyPose(out, baseBuf);
        detail::mask_blend_pose(baseBuf, overlayBuf,
                                std::span<const std::uint8_t>(ln->mask), w, out);
        scratch.release();
        scratch.release();
        return;
    }

    // IK / Warping — A5/A6 territory. Relay first input so a
    // forward-declared graph still produces something.
    auto ins = g.inputs(nodeId);
    if (ins.empty()) return;
    evaluateInto(g, ins.front(), dt, out, firedEvents, runtime, scratch);
}

} // namespace

EvalResult Animator::evaluate(EvalContext ctx, PoseBuffer& outPose) {
    EvalResult r;
    if (graph_ == nullptr) return r;

    const std::uint32_t jc = graph_->jointCount();
    if (jc == 0) return r;
    if (outPose.size() != jc) outPose.resize(jc);

    const bool inputsChanged = firstEvalAfterSetGraph_
                            || paramsChangedSinceLastEval_
                            || (ctx.dt != 0.0f);

    ScratchPool pool{scratchPosePool_, 0};
    evaluateInto(*graph_,
                 graph_->output(),
                 ctx.dt,
                 outPose.localPose().joints,
                 r.firedEvents,
                 std::span<detail::NodeRuntime>(nodeRuntime_),
                 pool);

    r.dirty = inputsChanged;
    firstEvalAfterSetGraph_ = false;
    paramsChangedSinceLastEval_ = false;
    return r;
}

} // namespace threadmaxx::animation
