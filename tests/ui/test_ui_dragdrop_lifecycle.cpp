/// @file test_ui_dragdrop_lifecycle.cpp

#include "Check.hpp"
#include "threadmaxx_ui/context.hpp"
#include "threadmaxx_ui/dragdrop.hpp"
#include "threadmaxx_ui/input.hpp"

int main() {
    using namespace threadmaxx::ui;

    UIContext ctx;
    const WidgetID srcId{0x9101};
    const WidgetID dstId{0x9102};
    const Rect srcBounds{0,  0, 100, 100};
    const Rect dstBounds{200, 0, 100, 100};

    const std::uint64_t kTextureHash = makeDragPayloadHash("Texture");
    const std::uint64_t kMeshHash    = makeDragPayloadHash("Mesh");
    const int payload = 42;

    // Frame 1: press inside source.
    UIInput press;
    press.mousePos = Vec2i{10, 10};
    press.mouseButtons = MouseButton::Left;
    press.mouseButtonsPressed = MouseButton::Left;
    ctx.setInput(press);
    ctx.beginFrame();
    interact(ctx, srcId, srcBounds);
    beginDragSource(ctx, srcId, kTextureHash, &payload);
    (void)dropTarget(ctx, dstId, dstBounds, kTextureHash);
    ctx.endFrame();
    CHECK(!ctx.dragActive());  // no delta yet -> no drag

    // Frame 2: drag — source activates.
    UIInput drag;
    drag.mousePos = Vec2i{50, 10};
    drag.mouseDelta = Vec2i{40, 0};
    drag.mouseButtons = MouseButton::Left;
    ctx.setInput(drag);
    ctx.beginFrame();
    interact(ctx, srcId, srcBounds);
    beginDragSource(ctx, srcId, kTextureHash, &payload);
    {
        auto ev = dropTarget(ctx, dstId, dstBounds, kTextureHash);
        CHECK(!ev.active);  // not over target yet
    }
    ctx.endFrame();
    CHECK(ctx.dragActive());

    // Frame 3: over target — should be active but not dropped.
    UIInput over;
    over.mousePos = Vec2i{250, 50};
    over.mouseButtons = MouseButton::Left;
    ctx.setInput(over);
    ctx.beginFrame();
    interact(ctx, srcId, srcBounds);
    beginDragSource(ctx, srcId, kTextureHash, &payload);
    {
        auto ev = dropTarget(ctx, dstId, dstBounds, kTextureHash);
        CHECK(ev.active);
        CHECK(!ev.dropped);
        CHECK(ev.data == &payload);
    }
    ctx.endFrame();

    // Mismatched hash: not active.
    ctx.setInput(over);
    ctx.beginFrame();
    interact(ctx, srcId, srcBounds);
    beginDragSource(ctx, srcId, kTextureHash, &payload);
    {
        auto ev = dropTarget(ctx, dstId, dstBounds, kMeshHash);
        CHECK(!ev.active);
        CHECK(!ev.dropped);
    }
    ctx.endFrame();

    // Frame 4: release over target -> dropped + clears state.
    UIInput rel;
    rel.mousePos = Vec2i{250, 50};
    rel.mouseButtonsReleased = MouseButton::Left;
    ctx.setInput(rel);
    ctx.beginFrame();
    interact(ctx, srcId, srcBounds);
    {
        auto ev = dropTarget(ctx, dstId, dstBounds, kTextureHash);
        CHECK(ev.dropped);
        CHECK(*static_cast<const int*>(ev.data) == 42);
    }
    ctx.endFrame();
    CHECK(!ctx.dragActive());

    // cancelDrag clears in-progress drag.
    ctx.setInput(press);
    ctx.beginFrame();
    interact(ctx, srcId, srcBounds);
    ctx.endFrame();
    ctx.setInput(drag);
    ctx.beginFrame();
    interact(ctx, srcId, srcBounds);
    beginDragSource(ctx, srcId, kTextureHash, &payload);
    CHECK(ctx.dragActive());
    cancelDrag(ctx);
    CHECK(!ctx.dragActive());
    ctx.endFrame();

    EXIT_WITH_RESULT();
}
