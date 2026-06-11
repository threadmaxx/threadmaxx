# `threadmaxx_ui` — User Guide

The library hands you `UIContext`. Everything else flows through it.
This guide walks the pillars in the order you'll typically wire them
up.

## 1. Context + backend

```cpp
#include <threadmaxx_ui/threadmaxx_ui.hpp>
using namespace threadmaxx::ui;

UIContext ctx;
VertexBackend backend;
backend.reserve(8192, 1024);
ctx.setBackend(&backend);
ctx.reserveHitTests(256);
ctx.reserveWidgetStates(256);
```

The backend is borrowed — the host owns the lifetime. `NullBackend`
exists for tests / hosts that don't draw yet.

## 2. Per-frame loop

```cpp
UIInput in;
in.mousePos = Vec2i{mx, my};
in.mouseDelta = Vec2i{mx - prevMx, my - prevMy};
in.mouseButtons       = currentMouseButtons;
in.mouseButtonsPressed  = newlyPressed;
in.mouseButtonsReleased = newlyReleased;
in.modifiers          = modBits;
in.navKeysPressed     = navKeysThisFrame;
in.deltaTimeSeconds   = 1.0f / 60.0f;
ctx.setInput(in);

ctx.beginFrame();
// ... widget calls ...
ctx.endFrame();
```

`endFrame` advances Tab focus, drops stale active state, and submits
the draw list to the backend.

## 3. Layout

Stateless resolution (any time):

```cpp
const std::array<Size, 3> sizes = {
    Size::fixed(80), Size::flex(1.0f), Size::fixed(100)};
std::array<Rect, 3> out;
resolveRow(parentRect, sizes, out, Padding::uniform(8), /*spacing*/ 4);
```

Scoped stacks (across nested widgets):

```cpp
pushLayout(ctx, parentRect, Orientation::Column,
           Padding::uniform(8), 4);
// emit children
popLayout(ctx);
```

Clipping:

```cpp
pushClip(ctx, panelBody);
// children that overflow get scissored
popClip(ctx);
```

## 4. Widgets

Every widget takes a `WidgetID`, a `Rect`, and any value bindings:

```cpp
if (button(ctx, WidgetID{0x1}, Rect{10, 10, 80, 24}, "Open"))
    doOpen();

checkbox(ctx, WidgetID{0x2}, Rect{10, 40, 200, 20}, "Enabled", &enabled);
slider(ctx, WidgetID{0x3}, Rect{10, 70, 200, 20}, &volume, 0.0f, 1.0f);

if (inputText(ctx, WidgetID{0x4}, Rect{10, 100, 200, 22},
              nameBuf, sizeof(nameBuf)))
    commitName();
```

`tooltip(ctx, host, hostBounds, "Help text", 0.5f)` — call AFTER the
host widget's `interact()` / widget call. Accumulates dwell time
from `UIInput::deltaTimeSeconds`.

## 5. Trees

```cpp
if (treeNodeBegin(ctx, WidgetID{0x10}, rRoot, "World")) {
    if (treeNodeBegin(ctx, WidgetID{0x11}, rPlayer, "Player")) {
        // children
        treeNodeEnd(ctx);
    }
    treeNodeEnd(ctx);
}
```

Collapsed nodes naturally drop their children from the focus cycle
(Up / Down arrows skip them) because the caller short-circuits the
body. `setTreeOpen(ctx, id, true/false)` opens or closes
programmatically.

## 6. Menus + popups

```cpp
beginMenuBar(ctx, Rect{0, 0, w, 22});
if (beginMenu(ctx, fileMenu, fileBtn, "File")) {
    if (menuItem(ctx, openItem, openR, "Open")) doOpen();
    endMenu(ctx);
}
endMenuBar(ctx);

// Right-click anywhere -> context popup
if (rightClickInBlank) openPopup(ctx, ctxPopup);
if (beginPopup(ctx, ctxPopup, anchorRect)) {
    if (menuItem(ctx, deleteItem, deleteR, "Delete")) doDelete();
    endPopup(ctx);
}
```

Popups eat input outside their body. Escape and click-outside close.
Menu bar latches — once one submenu is open, hovering siblings
auto-switches without another click.

## 7. Property inspector

```cpp
inspect(ctx, WidgetID{0x101}, row, "Enabled", &myBool);
inspect(ctx, WidgetID{0x102}, row, "Count",   &myInt);
inspect(ctx, WidgetID{0x103}, row, "Speed",   &myFloat);
inspect(ctx, WidgetID{0x104}, row, "Pos",     &x, &y, &z);
inspect(ctx, WidgetID{0x105}, row, "Name",    nameBuf, sizeof(nameBuf));
inspect(ctx, WidgetID{0x106}, row, "Owner",   entityHandle);  // read-only

inspectEnum(ctx, WidgetID{0x107}, row, "Mode", &mode,
            std::span<const std::pair<Mode, std::string_view>>{kModeOptions, 3});
```

Each row renders "label | control" with a 40 / 60 split by default.

## 8. Panels

```cpp
static PanelState ps{ .bounds = Rect{100, 100, 320, 240} };
if (beginPanel(ctx, WidgetID{0x200}, "Properties", ps)) {
    // body widgets
    endPanel(ctx);
}
```

Drag the title bar to move. Drag the bottom-right corner to resize
(`ps.minSize` enforces a floor). Double-click the title bar to
collapse. `setHostRect(ctx, ...)` defines the window in which
panels clamp.

## 9. Drag and drop

```cpp
constexpr std::uint64_t kTextureHash = makeDragPayloadHash("Texture");

// Source — call AFTER the host widget's interact().
const auto r = interact(ctx, srcId, srcBounds);
if (r.active) beginDragSource(ctx, srcId, kTextureHash, &myTexture);

// Target
if (auto ev = dropTarget(ctx, dstId, dstBounds, kTextureHash); ev.dropped) {
    assignTexture(*static_cast<const TextureId*>(ev.data));
}
```

Mismatched payload hashes are silently rejected. `cancelDrag(ctx)` or
Escape cancels.

## 10. Debug HUD

```cpp
debug::beginHud(ctx, /*x*/ 8, /*y*/ 8);
debug::kvFloat(ctx, "fps", fps);
debug::kvInt(ctx, "frame", frame);
debug::row(ctx, "build: Release");
```

Stateless — rebuilt every frame.

## 11. Hooking up the backend

`VertexBackend` produces a flat vertex / index / draw stream:

```cpp
// After ctx.endFrame() returns, on the GPU thread:
uploadVertices(backend.vertices().data(), backend.vertices().size());
uploadIndices(backend.indices().data(), backend.indices().size());

for (const auto& d : backend.draws()) {
    setScissor(d.scissor);
    switch (d.kind) {
        case VertexDrawKind::SolidColor: bindNoTexture(); break;
        case VertexDrawKind::FontAtlas:  bindFontAtlas(); break;
        case VertexDrawKind::Image:      bindImage(d.imageHandle); break;
    }
    drawIndexed(d.firstIndex, d.indexCount);
}
```

`Vertex` layout: `pos (vec2) + uv (vec2) + color (rgba8 packed)`, 20
bytes. Standard 2D UI shader.

## 12. Determinism + zero-alloc

The library is deterministic given the same input stream + same
context. Pinned by `test_ui_input_determinism`.

Hot path is zero-alloc after warmup. Three pinned gates:
- `test_ui_no_allocations` (64 rect emits, 16 text emits / frame)
- `test_ui_widget_no_allocations` (50 widgets / frame)
- `test_ui_crowd_no_alloc` (512 widgets across 8 panels / frame).

If you blow the gate: check that you reserve enough capacity at
startup (`reserveHitTests`, `reserveWidgetStates`, `backend.reserve`).

## 13. Common gotchas

- **Tooltip never fires** — `UIInput::deltaTimeSeconds` is 0. Set it
  per frame; defaults to 0.
- **Tab cycles wrong order** — focusables are walked in registration
  (= emit) order; reorder your widget calls.
- **Popup eats clicks intended for backgrounds** — that's by design;
  the popup is modal. Close it first or use
  `HitTestFlags::BypassPopupShadow` for menu-bar–style widgets.
- **Panel jumps off screen** — set `setHostRect(ctx, ...)` to your
  window rect; default is 1920×1080 virtual.
- **Vector inspector edits one component at a time** — that's the
  intended UX. The three sub-IDs are derived from your supplied
  ID via XOR.
