#pragma once

#include "threadmaxx_animation/blend.hpp"
#include "threadmaxx_animation/clip.hpp"
#include "threadmaxx_animation/types.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

/// AnimationGraph — node-based pose-evaluation DAG. A3 ships the
/// minimal node set (Clip + Output); A4 lights up Blend1D/Blend2D /
/// Additive / Layer; A5 adds IK; A6 adds Warping. The NodeType enum
/// keeps every variant declared from the start so handlers can pin
/// switch coverage at compile time.
///
/// A graph is a single object — `AnimationGraph` owns nodes,
/// connections, and per-node defaults. The `Animator` (eval.hpp)
/// binds a graph and walks it on each `evaluate` call; per-instance
/// playhead state (clip times, parameter overrides) lives on the
/// Animator, not the graph. This keeps the graph itself immutable
/// after construction and makes multi-agent crowd evaluation (A7) a
/// pure read on the graph side.
namespace threadmaxx::animation {

enum class NodeType : std::uint8_t {
    Clip,
    Blend1D,
    Blend2D,
    Additive,
    Layer,
    IK,
    Warping,
    Output,
};

/// Opaque node handle. Bit pattern is the node's index into the
/// graph's internal node array; `kInvalidGraphNodeId` sentinel uses
/// `0xFFFFFFFFu`.
struct GraphNodeId {
    std::uint32_t value{0xFFFFFFFFu};

    constexpr bool operator==(const GraphNodeId&) const noexcept = default;
    constexpr bool valid() const noexcept { return value != 0xFFFFFFFFu; }
};

inline constexpr GraphNodeId kInvalidGraphNodeId{0xFFFFFFFFu};

/// Clip-source node. Holds a borrowed pointer into the clip registry;
/// the registry owns lifetime, the graph stores the pointer. Default
/// playback rate is overridable per-Animator-instance via
/// `Animator::setParameter(nodeId, "playbackRate", v)`.
struct ClipNode {
    const ClipDesc* clip = nullptr;
    float playbackRate = 1.0f;
};

/// Terminal sink. A graph has exactly one Output node (the one passed
/// to `setOutput`); evaluation reads its single connected input and
/// copies that pose into the caller's `PoseBuffer`.
struct OutputNode {};

class AnimationGraph {
public:
    AnimationGraph() = default;
    explicit AnimationGraph(SkeletonRef skeleton) : skeleton_(skeleton) {}

    SkeletonRef skeleton() const noexcept { return skeleton_; }
    void setSkeleton(SkeletonRef skeleton) noexcept { skeleton_ = skeleton; }

    /// Add a Clip source node. `clip` must outlive the graph
    /// (typically owned by the AnimationRegistry).
    GraphNodeId addClip(const ClipDesc* clip, float playbackRate = 1.0f);

    /// Add the terminal Output sink. Call `setOutput` to designate it
    /// as the graph's evaluation root.
    GraphNodeId addOutput();

    /// Add a 1D blend node. `thresholds` must be sorted ascending and
    /// must have the same length as the eventual input list (added via
    /// `connect`). The graph default `"param"` is 0.
    GraphNodeId addBlend1D(std::span<const float> thresholds);

    /// Add a 2D blend node. `positionsX[i]` / `positionsY[i]` is input
    /// `i`'s blend-space coordinate. Both spans must have the same
    /// length, matching the eventual input list. Graph defaults
    /// `"x"` / `"y"` are 0.
    GraphNodeId addBlend2D(std::span<const float> positionsX,
                           std::span<const float> positionsY);

    /// Add an Additive composition node. `inputs[0]` is base,
    /// `inputs[1]` is the delta layer. Graph default `"weight"` is 1.
    GraphNodeId addAdditive(float weight = 1.0f);

    /// Add a masked-layer node. `inputs[0]` is base, `inputs[1]` is
    /// overlay. Use `setLayerMask` to install the per-joint mask after
    /// the joint count is known. Graph default `"weight"` is 1.
    GraphNodeId addLayer(float weight = 1.0f);

    /// Install the per-joint mask on a Layer node. `mask.size()` should
    /// equal the skeleton's joint count; out-of-range joints (mask size
    /// shorter than joint count) are treated as unmasked.
    void setLayerMask(GraphNodeId node, std::span<const std::uint8_t> mask);

    /// Connect `from`'s output to `to`'s input list. Multi-input nodes
    /// (Blend1D/Blend2D/Additive/Layer) consume their inputs in
    /// connection order — for Blend1D, that order must align with the
    /// thresholds passed to `addBlend1D`.
    void connect(GraphNodeId from, GraphNodeId to);

    /// Pin the output node from which evaluation reads the final pose.
    void setOutput(GraphNodeId output) noexcept { output_ = output; }
    GraphNodeId output() const noexcept { return output_; }

    /// Per-node parameter on the graph default itself (the parameter
    /// the Animator falls back to when no instance override exists).
    /// Recognized names by node kind:
    ///   - Clip:     `"playbackRate"`
    ///   - Blend1D:  `"param"`
    ///   - Blend2D:  `"x"`, `"y"`
    ///   - Additive: `"weight"`
    ///   - Layer:    `"weight"`
    /// Unknown name/node combinations are silently ignored.
    void setParameter(GraphNodeId node, std::string_view name, float value);
    std::optional<float> getParameter(GraphNodeId node, std::string_view name) const;

    std::size_t nodeCount() const noexcept { return nodes_.size(); }
    NodeType nodeType(GraphNodeId id) const noexcept;

    /// Clip-node-specific accessor (returns nullptr if `id` doesn't
    /// reference a Clip node). The pointer is owned by the graph and
    /// stable for the graph's lifetime.
    const ClipNode* clipNode(GraphNodeId id) const noexcept;

    /// Blend / Additive / Layer accessors. Each returns nullptr if
    /// `id` doesn't reference the expected node kind. Pointers are
    /// graph-owned and stable for the graph's lifetime.
    const Blend1DNode* blend1dNode(GraphNodeId id) const noexcept;
    const Blend2DNode* blend2dNode(GraphNodeId id) const noexcept;
    const AdditiveNode* additiveNode(GraphNodeId id) const noexcept;
    const LayerNode* layerNode(GraphNodeId id) const noexcept;

    /// Upstream connections to `id` (the set of nodes whose outputs
    /// flow into this node's inputs). A3's Output uses inputs[0].
    std::span<const GraphNodeId> inputs(GraphNodeId id) const noexcept;

    /// Joint count of the bound output clip (walks Output→Clip to find
    /// it). 0 if the graph has no connected Clip node yet — callers
    /// must size their PoseBuffer themselves in that case.
    std::uint32_t jointCount() const noexcept;

private:
    struct Node {
        NodeType type{};
        ClipNode clip{};         // valid iff type == NodeType::Clip
        Blend1DNode blend1d{};   // valid iff type == NodeType::Blend1D
        Blend2DNode blend2d{};   // valid iff type == NodeType::Blend2D
        AdditiveNode additive{}; // valid iff type == NodeType::Additive
        LayerNode layer{};       // valid iff type == NodeType::Layer
        std::vector<GraphNodeId> inputs;
    };

    bool isValid(GraphNodeId id) const noexcept {
        return id.valid() && id.value < nodes_.size();
    }

    SkeletonRef skeleton_{};
    std::vector<Node> nodes_;
    GraphNodeId output_ = kInvalidGraphNodeId;
};

} // namespace threadmaxx::animation
