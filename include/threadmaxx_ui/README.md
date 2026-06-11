# `threadmaxx_ui`

Immediate-mode UI primitives for tools and in-game chrome inside the
`threadmaxx` ecosystem. **Status**: v1.0.0 — production-ready.

## What

A small, deterministic, zero-alloc-hot-path UI layer. Powers editor
panels, property inspectors, trees, menus, drag handles, debug HUDs,
and simple in-game tool windows. Renderer-neutral: the library emits
a flat draw-command stream that any GPU host can consume via the
provided reference `VertexBackend`.

The library covers nine pillars:

- **Frame context** — `UIContext` owns the per-frame state (ID stack,
  draw list, hit-tests, hover / focus / active IDs, popup state,
  drag/drop, debug HUD cursor). Construct one per editor pane /
  in-game overlay; no globals.
- **Layout** — header-only `resolveRow` / `resolveColumn` size math
  (fixed + flex with last-flex-absorbs-rounding-leftover); scoped
  `pushLayout` / `popLayout` + `pushClip` / `popClip` stacks.
- **Input + interaction** — `UIInput` POD; `interact()` last-
  registered-wins hover; sticky mouse capture; Tab / Shift-Tab /
  arrow-key focus cycle; `wantsMouseCapture` / `wantsKeyboardCapture`.
- **Widgets** — label, button (with disabled style), checkbox, radio,
  slider, drag-scalar (Ctrl 0.5x / Shift 2x), input-text, separator,
  image placeholder, selectable, tooltip.
- **Trees + menus + popups** — `treeNodeBegin/End` with retained
  open/close state; collapsing headers; popup state machine (one
  open at a time); menu bar with latched hover-to-switch.
- **Property inspector** — `inspect(ctx, id, bounds, label, value)`
  overloads for bool / int32 / float / string / Vec3 / handle;
  `inspectEnum<E>` template. No reflection dependency.
- **Panels + drag/drop + gizmos + debug HUD** — movable / resizable
  panels with double-click collapse; typed-payload drag/drop
  (FNV-1a-64 hash); screen-space 2D drag handles; stateless HUD
  rows.
- **Backends** — `NullBackend` (test sink), `VertexBackend`
  (renderer-neutral tessellation to flat vertex/index/draw streams
  any GPU host can upload).

The library is engine-agnostic — it does NOT link against
`threadmaxx::threadmaxx`. Renderer integration is a thin host-side
adapter that consumes `VertexBackend`'s output.

## Quick start

```cmake
target_link_libraries(my_target PRIVATE threadmaxx::ui)
```

```cpp
#include <threadmaxx_ui/threadmaxx_ui.hpp>

using namespace threadmaxx::ui;

// 1. Pick a backend. NullBackend for tests; VertexBackend in real apps.
VertexBackend backend;
UIContext ctx;
ctx.setBackend(&backend);

// 2. Per game tick: feed input, build frame.
UIInput in;
in.mousePos = Vec2i{mouseX, mouseY};
in.mouseButtons = leftDown ? MouseButton::Left : 0;
in.deltaTimeSeconds = 1.0f / 60.0f;
ctx.setInput(in);

ctx.beginFrame();

// Menu bar
beginMenuBar(ctx, Rect{0, 0, 1920, 22});
if (beginMenu(ctx, WidgetID{0x1}, Rect{4, 2, 60, 20}, "File")) {
    if (menuItem(ctx, WidgetID{0x11}, Rect{4, 24, 120, 20}, "Open")) {
        openSelectedFile();
    }
    endMenu(ctx);
}
endMenuBar(ctx);

// Panel with property inspector
static PanelState props{ .bounds = Rect{20, 40, 360, 360} };
if (beginPanel(ctx, WidgetID{0x100}, "Properties", props)) {
    inspect(ctx, WidgetID{0x101}, Rect{props.bounds.x + 6, props.bounds.y + 24, 340, 20},
            "Enabled", &myBool);
    inspect(ctx, WidgetID{0x102}, Rect{props.bounds.x + 6, props.bounds.y + 46, 340, 20},
            "Speed", &myFloat);
    endPanel(ctx);
}

ctx.endFrame();

// 3. Upload backend.vertices() / .indices(), iterate backend.draws().
```

`USER_GUIDE.md` walks each pillar in detail.
`MAINTAINER_GUIDE.md` documents the versioning and ABI policy.

## Performance

`bench/ui_crowd_bench` is the frame-build throughput gate. On the v1.0
dev target (Linux x86_64, Release build):

| Widgets | Panels | Avg build cost |
|--------:|-------:|---------------:|
|     512 |      8 | **0.211 ms / frame** (~5x under the 1 ms gate) |

The hot path (`UIContext::beginFrame` → widget calls → `endFrame`) is
zero-allocation after warmup; this is contract, pinned by
`test_ui_crowd_no_alloc` (512 widgets, 100 mix calls, zero heap
traffic under a tracking allocator) and `test_ui_no_allocations` /
`test_ui_widget_no_allocations` / `test_ui_inspect_no_allocations`.

## Tests

42 tests in `tests/ui/`, all green on `build/` and `build-werror/`
(`-Wsign-conversion -Wconversion -Wold-style-cast -Werror`).
Categories:

- **Foundations** (5) — widget ID hashing, rect math, draw-list
  POD stream, lifecycle, no-alloc gate.
- **Layout** (6) — row / column / nested / padding+spacing / clip
  stack / overflow.
- **Input** (5) — hover / focus / mouse-capture / keyboard-capture /
  determinism.
- **Widgets** (9) — button / checkbox / radio / slider / drag-scalar /
  input-text / selectable / tooltip / no-alloc.
- **Trees + menus + popups** (5) — tree expand / tree keyboard nav /
  menu bar / popup modal / menu keyboard nav.
- **Inspector** (5) — primitives / enum / vector / handle / no-alloc.
- **Panels + drag/drop + gizmos + HUD** (5).
- **Backend + v1.0 gate** (2) — vertex backend / 500-widget no-alloc.

## Out of scope

- Docking + tabs (v1.x)
- Rich text / Unicode shaping (v1.x; v1.0 is ASCII / Latin-1)
- Tables (v1.x)
- Reflection-driven inspector (v1.x, needs `threadmaxx_reflect`)
- Animation / transition helpers (v1.x)
- Multi-window OS support (v1.x)
- 3D world gizmos (editor-side; UI ships 2D screen-space only)
- Windowing / platform abstraction (NG-2)
- Renderer ownership (NG-3)
