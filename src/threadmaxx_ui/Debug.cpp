/// @file Debug.cpp
/// @brief HUD row helpers.

#include "threadmaxx_ui/debug.hpp"

#include <cstdio>
#include <string_view>

#include "threadmaxx_ui/context.hpp"
#include "threadmaxx_ui/draw.hpp"

namespace threadmaxx::ui::debug {

namespace {
constexpr std::int32_t kRowHeight = 16;
std::int32_t g_columnX = 8;  // global default; per-context override below
} // namespace

void setColumnX(UIContext& /*ctx*/, std::int32_t x) noexcept {
    g_columnX = x;
}

void beginHud(UIContext& ctx, std::int32_t startX, std::int32_t startY) noexcept {
    g_columnX = startX;
    ctx.resetDebugCursor(startY);
}

void row(UIContext& ctx, std::string_view text, Color color) noexcept {
    ctx.drawList().emitText(Vec2i{g_columnX, ctx.debugCursorY()}, color, text);
    ctx.advanceDebugCursor(kRowHeight);
}

void kv(UIContext& ctx, std::string_view key, std::string_view value) noexcept {
    char buf[128];
    const int n = std::snprintf(buf, sizeof(buf), "%.*s: %.*s",
                                static_cast<int>(key.size()), key.data(),
                                static_cast<int>(value.size()), value.data());
    const std::size_t len = n > 0 ? static_cast<std::size_t>(n) : 0;
    row(ctx, std::string_view{buf, len});
}

void kvInt(UIContext& ctx, std::string_view key, std::int64_t value) noexcept {
    char buf[128];
    const int n = std::snprintf(buf, sizeof(buf), "%.*s: %lld",
                                static_cast<int>(key.size()), key.data(),
                                static_cast<long long>(value));
    const std::size_t len = n > 0 ? static_cast<std::size_t>(n) : 0;
    row(ctx, std::string_view{buf, len});
}

void kvFloat(UIContext& ctx, std::string_view key, float value) noexcept {
    char buf[128];
    const int n = std::snprintf(buf, sizeof(buf), "%.*s: %.3f",
                                static_cast<int>(key.size()), key.data(),
                                static_cast<double>(value));
    const std::size_t len = n > 0 ? static_cast<std::size_t>(n) : 0;
    row(ctx, std::string_view{buf, len});
}

} // namespace threadmaxx::ui::debug
