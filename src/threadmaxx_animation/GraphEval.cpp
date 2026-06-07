#include "threadmaxx_animation/eval.hpp"
#include "threadmaxx_animation/graph.hpp"

#include "threadmaxx_animation/detail/graph_eval.hpp"

#include <span>
#include <utility>

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

void AnimationGraph::connect(GraphNodeId from, GraphNodeId to) {
    if (!isValid(from) || !isValid(to)) return;
    nodes_[to.value].inputs.push_back(from);
}

void AnimationGraph::setParameter(GraphNodeId node, std::string_view name, float value) {
    if (!isValid(node)) return;
    Node& n = nodes_[node.value];
    if (n.type == NodeType::Clip && name == "playbackRate") {
        n.clip.playbackRate = value;
    }
}

std::optional<float> AnimationGraph::getParameter(GraphNodeId node,
                                                  std::string_view name) const {
    if (!isValid(node)) return std::nullopt;
    const Node& n = nodes_[node.value];
    if (n.type == NodeType::Clip && name == "playbackRate") {
        return n.clip.playbackRate;
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

std::span<const GraphNodeId> AnimationGraph::inputs(GraphNodeId id) const noexcept {
    if (!isValid(id)) return {};
    return std::span<const GraphNodeId>(nodes_[id.value].inputs);
}

std::uint32_t AnimationGraph::jointCount() const noexcept {
    if (!output_.valid()) return 0;
    // Walk Output → its first input → ... until we find a Clip node
    // with a non-null ClipDesc. A3's Clip → Output topology resolves
    // in one hop; later batches with nested blends would walk deeper.
    GraphNodeId cur = output_;
    for (int hops = 0; hops < 64; ++hops) {  // bounded to catch malformed cycles
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
    nodeTimes_.clear();
    nodePlaybackRate_.clear();
    nodePlaybackRateOverridden_.clear();
    if (graph_ != nullptr) {
        const std::size_t n = graph_->nodeCount();
        nodeTimes_.assign(n, 0.0f);
        nodePlaybackRate_.assign(n, 1.0f);
        nodePlaybackRateOverridden_.assign(n, false);
    }
    firstEvalAfterSetGraph_ = (graph_ != nullptr);
    paramsChangedSinceLastEval_ = false;
}

void Animator::setParameter(GraphNodeId node, std::string_view name, float value) {
    if (graph_ == nullptr) return;
    if (!node.valid() || node.value >= nodePlaybackRate_.size()) return;
    if (name == "playbackRate") {
        nodePlaybackRate_[node.value] = value;
        nodePlaybackRateOverridden_[node.value] = true;
        paramsChangedSinceLastEval_ = true;
    }
}

float Animator::getParameter(GraphNodeId node, std::string_view name) const {
    if (graph_ == nullptr) return 0.0f;
    if (!node.valid() || node.value >= nodePlaybackRate_.size()) return 0.0f;
    if (name == "playbackRate") {
        if (nodePlaybackRateOverridden_[node.value]) {
            return nodePlaybackRate_[node.value];
        }
        if (auto v = graph_->getParameter(node, name)) return *v;
    }
    return 0.0f;
}

float Animator::nodeTime(GraphNodeId node) const noexcept {
    if (graph_ == nullptr) return 0.0f;
    if (!node.valid() || node.value >= nodeTimes_.size()) return 0.0f;
    return nodeTimes_[node.value];
}

namespace {

// Recursively evaluate `nodeId`'s output and write it into `out`.
// A3 only supports Clip and Output; other node types fall through to
// a "copy the first input" pass so a forward-declared graph still
// produces something rather than UB.
void evaluateInto(const AnimationGraph& g,
                  Animator& a,
                  GraphNodeId nodeId,
                  float dt,
                  std::span<JointPose> out,
                  std::vector<EventTrackEvent>& firedEvents,
                  std::vector<float>& nodeTimesRef,
                  std::span<const float> nodePlaybackRate,
                  std::span<const std::uint8_t> nodePlaybackRateOverridden) {
    if (!nodeId.valid()) return;
    const NodeType t = g.nodeType(nodeId);
    if (t == NodeType::Clip) {
        const ClipNode* cn = g.clipNode(nodeId);
        if (cn == nullptr || cn->clip == nullptr) return;
        const float defaultRate = cn->playbackRate;
        const bool overridden = nodePlaybackRateOverridden[nodeId.value] != 0;
        const float rate = overridden ? nodePlaybackRate[nodeId.value] : defaultRate;
        const float oldTime = nodeTimesRef[nodeId.value];
        const float newTime = detail::stepClipNode(*cn, oldTime, dt, rate, out);
        nodeTimesRef[nodeId.value] = newTime;

        // Event collection mirrors A2's Animator semantics: forward
        // motion fires (oldTime, newTime]; looping wrap fires the
        // tail of the prev loop plus [0, newTime] of the new loop.
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
        evaluateInto(g, a, ins.front(), dt, out, firedEvents,
                     nodeTimesRef, nodePlaybackRate, nodePlaybackRateOverridden);
        return;
    }

    // Forward-declared node types (Blend1D/Blend2D/Additive/Layer/IK/
    // Warping). A4+ will replace these arms with real implementations;
    // for A3 we just relay the first input's pose so a partially-built
    // graph still produces output.
    auto ins = g.inputs(nodeId);
    if (ins.empty()) return;
    evaluateInto(g, a, ins.front(), dt, out, firedEvents,
                 nodeTimesRef, nodePlaybackRate, nodePlaybackRateOverridden);
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

    // Span over the bool vector — std::vector<bool> isn't span-compatible,
    // so we build a transient uint8_t shadow on the stack.
    std::vector<std::uint8_t> overriddenShadow(nodePlaybackRateOverridden_.size());
    for (std::size_t i = 0; i < overriddenShadow.size(); ++i) {
        overriddenShadow[i] = nodePlaybackRateOverridden_[i] ? std::uint8_t{1}
                                                              : std::uint8_t{0};
    }

    evaluateInto(*graph_,
                 *this,
                 graph_->output(),
                 ctx.dt,
                 outPose.localPose().joints,
                 r.firedEvents,
                 nodeTimes_,
                 std::span<const float>(nodePlaybackRate_),
                 std::span<const std::uint8_t>(overriddenShadow));

    r.dirty = inputsChanged;
    firstEvalAfterSetGraph_ = false;
    paramsChangedSinceLastEval_ = false;
    return r;
}

} // namespace threadmaxx::animation
