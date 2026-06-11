#pragma once

/// @file dragdrop.hpp
/// @brief Drag source + drop target. Payloads are runtime-typed by a
/// `std::uint64_t typeHash` (the caller supplies the hash, typically
/// derived at compile time from the payload's type name).
///
/// Pattern (source):
///
///   const auto r = interact(ctx, srcId, srcBounds);
///   if (r.active) beginDragSource(ctx, srcId, kTextureTypeHash, &textureId);
///
/// Pattern (target):
///
///   if (auto ev = dropTarget(ctx, dstId, dstBounds, kTextureTypeHash);
///       ev.dropped) {
///       host->assignTexture(*static_cast<const TextureId*>(ev.data));
///   }

#include <cstdint>
#include <string_view>

#include "threadmaxx_ui/detail/id_stack.hpp"
#include "threadmaxx_ui/input.hpp"
#include "threadmaxx_ui/types.hpp"

namespace threadmaxx::ui {

class UIContext;

/// FNV-1a-64 hash of a type's name. Use as a compile-time `payloadHash` so
/// source and target agree without explicit registration.
[[nodiscard]] inline constexpr std::uint64_t makeDragPayloadHash(std::string_view typeName) noexcept {
    return detail::fnv1a64(typeName);
}

/// Register a drag source. Call AFTER the host widget's `interact()` —
/// when the host is active AND the left button is held, the library tags
/// the in-progress drag with the supplied payload. Cancellation: Escape
/// or release-without-drop both clear it.
void beginDragSource(UIContext& ctx, WidgetID id,
                     std::uint64_t payloadHash,
                     const void* payloadData) noexcept;

/// Cancel any in-progress drag (Escape handler or host code).
void cancelDrag(UIContext& ctx) noexcept;

/// One frame's drop-target outcome.
struct DropEvent {
    /// A drag with a matching payload hash is in progress AND the cursor
    /// is inside the target's bounds.
    bool active = false;
    /// Mouse released inside this frame — the drop completed. Consume
    /// `data` here.
    bool dropped = false;
    /// Borrowed payload pointer (same as the source's `payloadData`). Valid
    /// while `active` or `dropped` is true.
    const void* data = nullptr;
};

/// Probe the drop target. Returns a `DropEvent` describing whether the
/// current drag (if any) would accept here, and whether a drop completed
/// this frame. On a successful drop, the drag state is cleared.
[[nodiscard]] DropEvent dropTarget(UIContext& ctx, WidgetID id, Rect bounds,
                                   std::uint64_t expectedHash) noexcept;

} // namespace threadmaxx::ui
