#pragma once

/// @file tree.hpp
/// @brief Tree nodes + collapsing headers. Retained "open / closed" state
/// lives in `WidgetState::iv` per ID. Pattern:
///
///   if (treeNodeBegin(ctx, id, bounds, "Folder")) {
///       // children here
///       treeNodeEnd(ctx);
///   }
///
/// Skipping children when the node is closed naturally drops them from the
/// focus walk so Up/Down arrow navigation traverses only visible nodes.

#include <cstdint>
#include <string_view>

#include "threadmaxx_ui/input.hpp"
#include "threadmaxx_ui/types.hpp"

namespace threadmaxx::ui {

class UIContext;

/// Begin a tree node. Returns true if the node is currently open AND the
/// caller must therefore emit children and call `treeNodeEnd`.
bool treeNodeBegin(UIContext& ctx, WidgetID id, Rect bounds,
                   std::string_view label) noexcept;

/// Pair with a matching `treeNodeBegin` that returned true. Pop the
/// indentation marker (no draw-list state today; reserved for future
/// styling).
void treeNodeEnd(UIContext& ctx) noexcept;

/// Same as `treeNodeBegin` but styled as a section header (no indent /
/// chevron). Same open/close semantics.
bool collapsingHeader(UIContext& ctx, WidgetID id, Rect bounds,
                      std::string_view label) noexcept;

/// Programmatic open/close. Survives across frames.
void setTreeOpen(UIContext& ctx, WidgetID id, bool open) noexcept;
[[nodiscard]] bool isTreeOpen(const UIContext& ctx, WidgetID id) noexcept;

} // namespace threadmaxx::ui
