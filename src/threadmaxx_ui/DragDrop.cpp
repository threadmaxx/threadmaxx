/// @file DragDrop.cpp
/// @brief Drag-source + drop-target state machine.

#include "threadmaxx_ui/dragdrop.hpp"

#include "threadmaxx_ui/context.hpp"
#include "threadmaxx_ui/input.hpp"

namespace threadmaxx::ui {

void beginDragSource(UIContext& ctx, WidgetID id,
                     std::uint64_t payloadHash,
                     const void* payloadData) noexcept {
    // Already dragging this source — nothing to do.
    if (ctx.dragSourceId() == id) return;

    const UIInput* in = ctx.input();
    if (!in) return;

    // Start a drag when the host widget is "active" (mouse-down inside).
    if (ctx.activeId() != id) return;

    // Only start once the cursor has moved past a small threshold so a
    // simple click doesn't accidentally trigger a drag. ANY non-zero
    // delta this frame counts; the host can layer their own threshold by
    // gating the call site.
    if (in->mouseDelta.x == 0 && in->mouseDelta.y == 0) return;

    ctx.setDragSourceId(id);
    ctx.setDragPayloadHash(payloadHash);
    ctx.setDragPayloadData(payloadData);
}

void cancelDrag(UIContext& ctx) noexcept {
    ctx.setDragSourceId(WidgetID{});
    ctx.setDragPayloadHash(0);
    ctx.setDragPayloadData(nullptr);
}

DropEvent dropTarget(UIContext& ctx, WidgetID /*id*/, Rect bounds,
                     std::uint64_t expectedHash) noexcept {
    DropEvent ev{};
    if (!ctx.dragActive()) return ev;
    if (ctx.dragPayloadHash() != expectedHash) return ev;

    const UIInput* in = ctx.input();
    if (!in) return ev;
    if (!contains(bounds, in->mousePos.x, in->mousePos.y)) return ev;

    ev.active = true;
    ev.data = ctx.dragPayloadData();
    if ((in->mouseButtonsReleased & MouseButton::Left) != 0) {
        ev.dropped = true;
        // Consume — clear the drag state so the engine's end-of-frame
        // sweep doesn't see it as orphaned.
        ctx.setDragSourceId(WidgetID{});
        ctx.setDragPayloadHash(0);
        ctx.setDragPayloadData(nullptr);
    }
    return ev;
}

} // namespace threadmaxx::ui
