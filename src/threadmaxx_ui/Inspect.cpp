/// @file Inspect.cpp
/// @brief Out-of-line value-formatting helpers for `inspect.hpp`. Kept
/// out of header-inline scope so GCC's optimizer doesn't trigger spurious
/// -Wstringop-overflow warnings on the snprintf+vector::insert chain.

#include "threadmaxx_ui/inspect.hpp"

#include <cstdio>
#include <string_view>

#include "threadmaxx_ui/context.hpp"
#include "threadmaxx_ui/draw.hpp"
#include "threadmaxx_ui/widget.hpp"

namespace threadmaxx::ui::detail {

void renderIntValue(UIContext& ctx, Rect vr, std::int32_t value) noexcept {
    char buf[32];
    const int n = std::snprintf(buf, sizeof(buf), "%d", value);
    const std::size_t len = n > 0 ? static_cast<std::size_t>(n) : 0;
    ctx.drawList().emitText(Vec2i{vr.x + 4, vr.y + 2}, theme::kText,
                            std::string_view{buf, len});
}

void renderFloatValue(UIContext& ctx, Rect vr, float value) noexcept {
    char buf[32];
    const int n = std::snprintf(buf, sizeof(buf), "%.3f",
                                static_cast<double>(value));
    const std::size_t len = n > 0 ? static_cast<std::size_t>(n) : 0;
    ctx.drawList().emitText(Vec2i{vr.x + 4, vr.y + 2}, theme::kText,
                            std::string_view{buf, len});
}

void renderHandleValue(UIContext& ctx, Rect vr, std::uint64_t v) noexcept {
    char buf[32];
    const int n = std::snprintf(buf, sizeof(buf), "0x%llx",
                                static_cast<unsigned long long>(v));
    const std::size_t len = n > 0 ? static_cast<std::size_t>(n) : 0;
    ctx.drawList().emitText(Vec2i{vr.x + 4, vr.y + 2}, theme::kTextDisabled,
                            std::string_view{buf, len});
}

} // namespace threadmaxx::ui::detail
