#pragma once

/// @file debug.hpp
/// @brief HUD / overlay helpers. Always-on rows that stack vertically;
/// no retained state — the HUD is rebuilt from scratch every frame.

#include <cstdint>
#include <string_view>

#include "threadmaxx_ui/types.hpp"

namespace threadmaxx::ui {

class UIContext;

namespace debug {

/// Reset the row cursor. Call once per frame before the first `row()` to
/// position the HUD at a known starting Y.
void beginHud(UIContext& ctx, std::int32_t startX = 8,
              std::int32_t startY = 8) noexcept;

/// Emit a single HUD row at the current cursor and advance.
void row(UIContext& ctx, std::string_view text,
         Color color = rgba(0xE0E0E0FFu)) noexcept;

/// Emit a key/value pair (printed as "key: value").
void kv(UIContext& ctx, std::string_view key, std::string_view value) noexcept;
void kvInt(UIContext& ctx, std::string_view key, std::int64_t value) noexcept;
void kvFloat(UIContext& ctx, std::string_view key, float value) noexcept;

/// Width of the HUD column — sets the X anchor. Defaults to 200.
void setColumnX(UIContext& ctx, std::int32_t x) noexcept;

} // namespace debug

} // namespace threadmaxx::ui
