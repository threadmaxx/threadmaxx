# `threadmaxx_ui` — Maintainer Guide

This document is the contract for keeping the library at v1.x binary-
compatible while it evolves. It lives next to the code so it moves
when the code moves.

## 1. Versioning

The current release is **v1.0.0**. Three artifacts MUST move together
when bumping:

1. `include/threadmaxx_ui/version.hpp` —
   `THREADMAXX_UI_VERSION_MAJOR/MINOR/PATCH` macros AND the
   `version_string()` literal.
2. Tag the commit.
3. Update the status line at the top of `FUTURE_WORK.md`.

### Bump rules (SemVer)

- **MAJOR** — breaking public API change (signature of a `*.hpp`
  entry point, layout of a public POD, removal of a function /
  class / enum value, change to the `DrawCmd` variant layout, change
  to the `Vertex` struct layout).
- **MINOR** — additive change (new widget, new draw-cmd kind appended
  at the END of the enum, new property-inspector overload, new
  backend, new HitTestFlags bit). Existing API stays source +
  binary compatible.
- **PATCH** — bug fix, perf, or doc improvement. No API change.

### Deprecation cycle

`[[deprecated]]` for one minor release, then removal in the next
major.

## 2. Public surface

Everything reachable through `#include
<threadmaxx_ui/threadmaxx_ui.hpp>` is the contract. The umbrella
header lists every public file currently in play:

```
backend.hpp     config.hpp      context.hpp      debug.hpp
dragdrop.hpp    draw.hpp        gizmo.hpp        input.hpp
inspect.hpp     layout.hpp      menu.hpp         panel.hpp
tree.hpp        types.hpp       version.hpp      widget.hpp
backends/NullBackend.hpp
backends/VertexBackend.hpp
```

Anything under `include/threadmaxx_ui/detail/` (currently
`id_stack.hpp`, `rect_math.hpp`, `clip_stack.hpp`) is implementation
detail and NOT part of the API contract — those can change between
minor releases as long as the public surface stays stable.

Everything in `src/threadmaxx_ui/` is private. `UIContext`'s body
lives in the header for inlineability, but its members are accessed
ONLY via the public methods.

## 3. Adding a public symbol

1. Doxygen `@brief` on the symbol. Load-bearing methods also get
   `@thread_safety` (single-threaded by contract) / `@pre`.
2. If it adds a new `DrawCmdKind`, append at the END of the enum —
   reordering is a MAJOR break (changes wire layout).
3. If it adds a new `HitTestFlags` bit, take the next free bit —
   never reuse old values.
4. If it adds a new widget, drop the test in `tests/ui/test_ui_widget_*.cpp`
   alongside the existing ones, and update the no-alloc gate's
   widget mix.

## 4. Adding a new backend

Pattern (mirrors `VertexBackend`):

1. `include/threadmaxx_ui/backends/<Name>Backend.hpp` — public
   header with a class deriving `IUIBackend`.
2. `src/threadmaxx_ui/backends/<Name>Backend.cpp` — implementation.
3. `src/threadmaxx_ui/CMakeLists.txt` — append to
   `THREADMAXX_UI_SOURCES` + `THREADMAXX_UI_PUBLIC_HEADERS`.
4. `tests/ui/test_ui_<name>_backend.cpp` — at minimum: emit a known
   draw stream and verify the backend's output shape.

## 5. Hot-path discipline

`UIContext::beginFrame()` → widget calls → `endFrame()` is the
steady-state hot path. Two contracts:

1. **Zero allocations after warmup** — pinned by
   `test_ui_no_allocations` (foundation), `test_ui_widget_no_allocations`
   (widget set), `test_ui_inspect_no_allocations` (inspector), and
   `test_ui_crowd_no_alloc` (v1.0 close-out gate). Vectors stay at
   their reserved capacity; no `std::function` captures; no
   `emplace_back` that could realloc.
2. **Performance gate** — `bench/ui_crowd_bench` at 512 widgets, 8
   panels, must stay under 1 ms / frame for the UI build phase in
   Release builds. The current baseline is 0.211 ms / frame;
   regressions to ≥ 700 µs should be investigated.

**Don't reintroduce per-frame heap allocations in `interact()` or
widget bodies.** If a new feature needs retained state, store it in
`UIContext::widgetState(id)` (the unordered_map auto-reuses nodes).
If a new feature needs a per-frame buffer, allocate on the
`drawList()` arena or on a host-supplied scratch — don't grow a
member vector on every frame.

**Don't replace `emitText`'s resize+memcpy** with `vector::insert` —
GCC's `-Wstringop-overflow` fires false positives on the iterator-
pair insert when it gets inlined into a TU that can't see the
reserved capacity. The current pattern is the workaround.

## 6. Determinism contract

Same input stream + same context + same widget call order → byte-
identical draw command stream AND identical hover / focus / active
state. Pinned by `test_ui_input_determinism`.

The library:
- Uses FNV-1a-64 with frozen seed for `WidgetID` hashing.
- Uses integer math throughout layout (no float accumulator).
- Does not depend on iteration order of any STL hash container.
  Widget state lookup is by exact key.

If you add a feature that depends on time, take it via
`UIInput::deltaTimeSeconds` — never from `std::chrono` directly.

## 7. Internal layout cheat sheet

```
include/threadmaxx_ui/
  threadmaxx_ui.hpp    umbrella include
  types.hpp            WidgetID, Vec2i, Rect, Color
  config.hpp           kIdStackDepth, kLayoutStackDepth, etc.
  draw.hpp             DrawCmd / DrawList
  backend.hpp          IUIBackend
  context.hpp          UIContext (per-frame state owner)
  input.hpp            UIInput POD, HitTestFlags, interact()
  layout.hpp           Size + resolveRow / resolveColumn + scoped pushers
  widget.hpp           label / button / checkbox / radio / slider /
                       dragScalar / inputText / selectable / tooltip /
                       theme::*
  tree.hpp             treeNodeBegin / treeNodeEnd / collapsingHeader
  menu.hpp             beginMenuBar / beginMenu / menuItem / popups
  inspect.hpp          inspect() overloads + inspectEnum<E>
  panel.hpp            PanelState, beginPanel / endPanel, host-rect clamp
  dragdrop.hpp         beginDragSource / dropTarget / cancelDrag
  gizmo.hpp            dragHandle2D
  debug.hpp            beginHud / row / kv / kvInt / kvFloat
  version.hpp          macros + version_string()
  detail/
    id_stack.hpp       IdStack + FNV-1a-64
    rect_math.hpp      inset / translate helpers
    clip_stack.hpp     ClipStack
  backends/
    NullBackend.hpp    test sink
    VertexBackend.hpp  flat vertex / index / draw stream emitter

src/threadmaxx_ui/
  UIContext.cpp       (only the bits not inline)
  Layout.cpp          size resolution + stack glue
  Input.cpp           interact / finalizeInputState (focus + popup +
                      drag sweep)
  Widgets.cpp         widget set
  Tree.cpp / Menu.cpp / Panel.cpp / DragDrop.cpp / Gizmo.cpp / Debug.cpp
  Inspect.cpp         out-of-line int/float/handle value formatters
  backends/
    NullBackend.cpp
    VertexBackend.cpp
```

## 8. Test taxonomy

42 tests in `tests/ui/`. When adding a feature, drop the test in the
matching bucket — CI grep prefix `^ui\.` keeps the suite namespaced.

| Bucket | Tests | What they pin |
|--------|------:|---------------|
| Foundations | 5 | Widget ID hashing, rect math, draw-list POD, lifecycle, no-alloc |
| Layout | 6 | Row/column/nested/padding/clip/overflow |
| Input | 5 | Hover/focus/mouse capture/keyboard capture/determinism |
| Widgets | 9 | Each FR-2 primitive + no-alloc |
| Trees + menus + popups | 5 | Expand/keyboard nav/menu bar/modal popup/menu nav |
| Inspector | 5 | Primitives/enum/vector/handle/no-alloc |
| Panels + drag/drop + gizmos + HUD | 5 | Move/resize/drag-drop/2D gizmo/HUD |
| Backend + v1.0 gate | 2 | Vertex backend / 500-widget crowd no-alloc |

## 9. Sanitizer + warning hygiene

The build must stay clean under:
- `-Wall -Wextra -Wpedantic -Wshadow -Wsign-conversion -Wconversion
  -Wold-style-cast -Werror` (the `THREADMAXX_WARNINGS_AS_ERRORS=ON`
  build tree).
- ASAN / UBSAN — engine-side sanitizer trees include the UI suite.
  TSAN is not run on the UI library since the public surface is
  single-threaded by contract.

## 10. Out-of-scope for v1.x

Per `DESIGN_NOTES.md` §§ NG-1..NG-10:

- General-purpose application framework
- Windowing / platform abstraction (game owns this)
- Renderer ownership (UI emits a draw stream; host uploads)
- Hidden singleton state
- Forced docking / theming / multi-window in v1
- Rich text or code editor in v1
- Asset import / filesystem browsing as a hard dep
- Reflection as a hard dep
- GPU-driven UI geometry generation
- UI system that depends on the editor being present (it must also
  be useful for in-game overlays + debug HUDs)

Things slated for v1.x but not v1.0:

- Docking + tabs
- Tables
- Rich text + Unicode shaping (v1.0 is ASCII / Latin-1)
- Theming presets + style stack
- Reflection-driven inspector (depends on `threadmaxx_reflect`)
- Animation / transition helpers
- Multi-window OS support
- Capture-to-image diagnostic
