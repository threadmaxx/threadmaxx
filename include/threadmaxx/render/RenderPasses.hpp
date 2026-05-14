#pragma once

#include <cstdint>

namespace threadmaxx {

/// Pre-defined render-pass bins. The hierarchical @ref RenderFrame holds
/// a per-pass span of @ref DrawItem so renderers can iterate one bin at
/// a time without per-item filtering.
///
/// The pass set is intentionally short and renderer-neutral: every
/// reasonable forward / forward+ pipeline can route work to these four
/// bins, and the renderer can choose to merge / skip / re-order as
/// needed (e.g. a no-shadow renderer simply ignores @ref ShadowCasters).
///
/// Renderers that want more pass slots can do so by introducing their
/// own classification on top of @ref DrawItem::flags or
/// @ref DrawItem::sortKey; the engine does not gate on the enum
/// extensibility.
enum class RenderPass : std::uint8_t {
    Opaque        = 0,
    Transparent   = 1,
    ShadowCasters = 2,
    Overlay       = 3,
};

/// Number of defined passes. Used to size the per-pass arrays in
/// @ref RenderFrame and @ref RenderFrameBuilder.
inline constexpr std::size_t kRenderPassCount = 4;

/// Convert a pass to its dense bin index.
constexpr std::size_t passIndex(RenderPass p) noexcept {
    return static_cast<std::size_t>(p);
}

} // namespace threadmaxx
