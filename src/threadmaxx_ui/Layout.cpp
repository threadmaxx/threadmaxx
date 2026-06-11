/// @file Layout.cpp
/// @brief Pure-function size resolution + the layout / clip stack glue on
/// `UIContext`.

#include "threadmaxx_ui/layout.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>

#include "threadmaxx_ui/context.hpp"
#include "threadmaxx_ui/draw.hpp"

namespace threadmaxx::ui {

namespace {

struct ResolveTotals {
    std::int32_t fixedTotal = 0;
    float flexTotal = 0.0f;
    std::int32_t lastFlexIdx = -1;
};

ResolveTotals computeTotals(std::span<const Size> sizes) noexcept {
    ResolveTotals t{};
    for (std::size_t i = 0; i < sizes.size(); ++i) {
        const Size& s = sizes[i];
        if (s.mode == SizeMode::Fixed) {
            t.fixedTotal += s.pixels;
        } else {
            t.flexTotal += s.flexWeight;
            t.lastFlexIdx = static_cast<std::int32_t>(i);
        }
    }
    return t;
}

} // namespace

void resolveRow(Rect parent, std::span<const Size> sizes, std::span<Rect> out,
                Padding pad, std::int32_t spacing) noexcept {
    const std::size_t n = sizes.size() < out.size() ? sizes.size() : out.size();
    if (n == 0) return;
    const Rect content = applyPadding(parent, pad);

    const ResolveTotals totals = computeTotals(sizes.subspan(0, n));
    const std::int32_t spacingBudget = n > 1
        ? spacing * static_cast<std::int32_t>(n - 1)
        : 0;
    std::int32_t flexBudget = content.w - totals.fixedTotal - spacingBudget;
    if (flexBudget < 0) flexBudget = 0;

    std::int32_t cursor = content.x;
    std::int32_t flexAssigned = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const Size& s = sizes[i];
        std::int32_t w = 0;
        if (s.mode == SizeMode::Fixed) {
            w = s.pixels;
        } else if (totals.flexTotal > 0.0f) {
            if (static_cast<std::int32_t>(i) == totals.lastFlexIdx) {
                w = flexBudget - flexAssigned;
                if (w < 0) w = 0;
            } else {
                const float frac = s.flexWeight / totals.flexTotal;
                w = static_cast<std::int32_t>(static_cast<float>(flexBudget) * frac);
                if (w < 0) w = 0;
                flexAssigned += w;
            }
        }
        Rect r{};
        r.x = cursor;
        r.y = content.y;
        r.w = w;
        r.h = content.h;
        out[i] = r;
        cursor += w + spacing;
    }
}

void resolveColumn(Rect parent, std::span<const Size> sizes, std::span<Rect> out,
                   Padding pad, std::int32_t spacing) noexcept {
    const std::size_t n = sizes.size() < out.size() ? sizes.size() : out.size();
    if (n == 0) return;
    const Rect content = applyPadding(parent, pad);

    const ResolveTotals totals = computeTotals(sizes.subspan(0, n));
    const std::int32_t spacingBudget = n > 1
        ? spacing * static_cast<std::int32_t>(n - 1)
        : 0;
    std::int32_t flexBudget = content.h - totals.fixedTotal - spacingBudget;
    if (flexBudget < 0) flexBudget = 0;

    std::int32_t cursor = content.y;
    std::int32_t flexAssigned = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const Size& s = sizes[i];
        std::int32_t h = 0;
        if (s.mode == SizeMode::Fixed) {
            h = s.pixels;
        } else if (totals.flexTotal > 0.0f) {
            if (static_cast<std::int32_t>(i) == totals.lastFlexIdx) {
                h = flexBudget - flexAssigned;
                if (h < 0) h = 0;
            } else {
                const float frac = s.flexWeight / totals.flexTotal;
                h = static_cast<std::int32_t>(static_cast<float>(flexBudget) * frac);
                if (h < 0) h = 0;
                flexAssigned += h;
            }
        }
        Rect r{};
        r.x = content.x;
        r.y = cursor;
        r.w = content.w;
        r.h = h;
        out[i] = r;
        cursor += h + spacing;
    }
}

std::int32_t pushLayout(UIContext& ctx, Rect bounds, Orientation orient,
                        Padding pad, std::int32_t spacing) noexcept {
    LayoutFrame frame{};
    frame.content = applyPadding(bounds, pad);
    frame.padding = pad;
    frame.spacing = spacing;
    frame.orient = orient;
    const std::size_t before = ctx.layoutDepth();
    ctx.layoutPushRaw(frame);
    if (ctx.layoutDepth() == before) {
        // overflow — push was rejected
        return kLayoutOverflowed;
    }
    return static_cast<std::int32_t>(ctx.layoutDepth() - 1);
}

void popLayout(UIContext& ctx) noexcept {
    ctx.layoutPopRaw();
}

const LayoutFrame& currentLayout(const UIContext& ctx) noexcept {
    return ctx.layoutTop();
}

Rect pushClip(UIContext& ctx, Rect bounds) noexcept {
    const Rect effective = ctx.clipStack().push(bounds);
    ctx.drawList().emitClipPush(effective);
    return effective;
}

void popClip(UIContext& ctx) noexcept {
    ctx.clipStack().pop();
    ctx.drawList().emitClipPop();
}

// -- UIContext members (the raw push/pop/top accessors) -----------------------

void UIContext::layoutPushRaw(const LayoutFrame& frame) noexcept {
    if (layoutDepth_ < layoutStack_.size()) {
        layoutStack_[layoutDepth_++] = frame;
    } else {
        ++layoutOverflows_;
    }
}

void UIContext::layoutPopRaw() noexcept {
    if (layoutDepth_ > 0) --layoutDepth_;
}

const LayoutFrame& UIContext::layoutTop() const noexcept {
    assert(layoutDepth_ > 0 && "layoutTop() called with empty layout stack");
    return layoutStack_[layoutDepth_ - 1];
}

} // namespace threadmaxx::ui
