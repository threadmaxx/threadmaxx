#pragma once

#include <cstdint>
#include <vector>

/// Blend / Layer / Additive node payloads + helpers for the A4 graph
/// surface. These types are reachable through `AnimationGraph::addBlend1D`
/// / `addBlend2D` / `addAdditive` / `addLayer`; they're declared in their
/// own header so a consumer that only needs the v1.0 evaluation API can
/// skip the blend-specific surface.
///
/// Per-node runtime parameters (`"param"`, `"x"`, `"y"`, `"weight"`) are
/// resolved through the same two-tier path as Clip's `"playbackRate"` —
/// graph default, overridable per-Animator via `Animator::setParameter`.
namespace threadmaxx::animation {

/// 1D blend node payload. `thresholds[i]` is the parameter value at which
/// input `i` is at full weight; thresholds must be sorted ascending. The
/// input count and threshold count must match. Param values outside the
/// authored range clamp to the nearest endpoint.
struct Blend1DNode {
    std::vector<float> thresholds;
    float param = 0.0f;  // graph default; per-Animator override via setParameter
};

/// 2D blend node payload. `positionsX[i]` / `positionsY[i]` is input `i`'s
/// blend-space coordinate. Resolution is inverse-distance-weighted: a
/// query at the centroid of a regular point set produces uniform weights;
/// a query exactly at a sample point collapses to that sample. Suitable
/// for locomotion-style direction/speed blends without needing a
/// triangulated authored grid.
struct Blend2DNode {
    std::vector<float> positionsX;
    std::vector<float> positionsY;
    float x = 0.0f;  // graph default x; per-Animator override via setParameter
    float y = 0.0f;  // graph default y
};

/// Additive composition node. `inputs[0]` is the base, `inputs[1]` is
/// the delta. Output = base + weight * delta (TRS-aware: translations
/// add, rotations compose via slerp(identity, delta.R, weight), scales
/// multiply componentwise by lerp(1, delta.S, weight)).
struct AdditiveNode {
    float weight = 1.0f;  // graph default; per-Animator override via setParameter
};

/// Masked-layer composition. `inputs[0]` is the base, `inputs[1]` is the
/// overlay. Joints listed in `mask` (mask[j] != 0) blend toward overlay
/// with `weight`; unmasked joints stay at base. Use for upper-body /
/// face overlays on top of full-body locomotion.
struct LayerNode {
    std::vector<std::uint8_t> mask;  // per-joint flag, 0 = unmasked, 1 = masked
    float weight = 1.0f;
};

}  // namespace threadmaxx::animation
