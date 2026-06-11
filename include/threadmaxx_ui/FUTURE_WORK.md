# `threadmaxx_ui` — batch plan

Sibling-library implementation plan. `DESIGN_NOTES.md` is the
authoritative spec; this doc breaks it down into shippable
test-driven batches and pins the load-bearing decisions the spec
left implicit.

Status: **planning** — DESIGN_NOTES.md landed 2026-06-11.
First batch (UI1) starts immediately.

Sequencing follows §8 ("Suggested implementation order") of the
design notes, regrouped into shippable units that each carry their
own tests.

## Conventions

Each batch is independently shippable:

- **Goal** — what the batch accomplishes in one sentence.
- **Test gate** — assertions that prove the batch landed.
- **Files** — what's added / modified.
- **Risks** — what could break.
- **Out of scope** — explicitly deferred to a later batch.

Batches start red, go green, then refactor. The library produces a
static library `threadmaxx::ui` plus public headers under
`include/threadmaxx_ui/`. Renderer backends live behind a thin
draw-list adapter; the reference adapter targets the engine's
existing Vulkan renderer.

The hot path (frame build) must never allocate after warmup. Tests
prove this with a global `operator new` tracking allocator, the
same pattern the audio library uses.

## DESIGN_NOTES audit (decisions pinned here)

The AI-generated spec is good on shape but leaves several
load-bearing decisions implicit. This section pins them so the
batches don't have to re-litigate during implementation.

1. **Draw-list format is the backend contract.** Backends consume
   a flat POD command stream (`DrawCmd` variant: `Rect`, `Line`,
   `Text`, `Image`, `ClipPush`, `ClipPop`) — no per-widget state
   reconstruction. Renderer adapters are tiny; UI semantics live
   above the cut.
2. **Coordinate system.** Pixels, top-left origin, integer rects in
   the command stream (`int32_t` x/y/w/h), floating-point logical
   coords inside the layout engine. DPI scale is a single `float`
   on the `UIContext` set by the host; widgets multiply at emit
   time.
3. **Font atlas ownership.** The library ships its own STB-truetype
   atlas (`detail/text_cache.hpp`) — same library tou2d uses, so
   we don't duplicate. Atlas lives on the `UIContext`; multiple
   contexts share via `shared_ptr` if the host opts in. Custom
   font sources land via a `FontProvider` interface.
4. **Input model.** UI defines `UIInput` POD (mouse pos, button
   bits, scroll, key + char queue, modifier flags) and the host
   feeds it per frame. The future `threadmaxx_input` lib lowers
   into `UIInput`; the UI library does NOT depend on it.
5. **Property inspector is type-overload-based, not reflection.**
   `inspect(ctx, "Label", T&)` is a free-function template
   specialized per type (`bool`, `int`, `float`, `string`,
   `Vec3`, etc.). NG-7 holds: reflection stays optional, and the
   inspector works without it.
6. **Retained widget state lives on the `UIContext`.** Keyed by
   `WidgetID`. Focus, tree-open, text cursor, drag state — all
   stored in a single hash map on the context; cleared with
   `resetState()`. No thread-locals, no singletons (NG-4).
7. **Determinism contract.** Same input stream + same context seed
   + same font asset bytes → byte-identical draw command stream.
   Pinned by a determinism test in UI3 (capture cmd stream from
   two parallel contexts driven by the same input log).
8. **Zero-alloc gate.** After `beginFrame()` warmup (3 frames),
   repeated `beginFrame/endFrame` cycles produce zero heap traffic
   under the tracking allocator. Pinned in UI1.
9. **Multi-context (FR-20).** Every storage path lives on the
   `UIContext` instance — host instantiates one per editor pane /
   in-game overlay. No global state.
10. **Gizmo scope (FR-14).** UI ships 2D *handles* — screen-space
    rectangles you drag with the mouse, suitable for 2D level
    editing and resize knobs. **3D world gizmos** (translate /
    rotate / scale axes that hit-test against a camera ray) are
    EDITOR-side and out of scope here. The UI library exposes the
    screen-space drag primitive; the editor wraps it with its own
    3D math.
11. **Docking deferred.** Panels are movable / resizable in v1.0
    (NG-5 says docking is optional). Tabs + dock-zone targeting
    land in v1.x.
12. **Drag/drop payload.** Runtime-typed: payload is a `void*` +
    a `std::uint64_t typeHash` (typeid hash baked at compile
    time). Drop target queries the hash before casting. No
    `std::any`.
13. **Text shaping.** ASCII / Latin-1 only in v1.0 with simple
    kerning from STB. Full Unicode / ICU / HarfBuzz is v1.x.
14. **Allocation policy in the layout engine.** Layout stacks
    pre-reserve fixed capacities at context construction; nested
    depth past the cap triggers an assert in debug, a silent
    truncate-and-warn in release. Default depth caps: 32 for the
    ID stack, 16 for the layout stack, 16 for the clip stack.

## Library structure (target end-state)

```
include/threadmaxx_ui/
  threadmaxx_ui.hpp       # umbrella include
  config.hpp              # compile-time defaults + capacity caps
  types.hpp               # WidgetID, Rect, Color, FontHandle, etc.
  input.hpp               # UIInput POD + capture flags
  context.hpp             # UIContext (per-frame state owner)
  layout.hpp              # row/column/child helpers + constraints
  widget.hpp              # button/label/checkbox/slider/text input
  text.hpp                # measure / wrap / truncate helpers
  tree.hpp                # tree node, collapsing header
  menu.hpp                # popup, menu bar, menu item
  panel.hpp               # movable / resizable panel windows
  dragdrop.hpp            # drag source / drop target
  inspect.hpp             # property rows (per-type overloads)
  gizmo.hpp               # 2D drag handles
  debug.hpp               # HUD / overlay helpers
  theme.hpp               # colors / spacing / metrics
  draw.hpp                # DrawCmd, DrawList
  backend.hpp             # IUIBackend renderer adapter
  fonts.hpp               # FontProvider + FontHandle + atlas API
  version.hpp             # version macros + version_string()
  detail/
    id_stack.hpp
    rect_math.hpp
    clip_stack.hpp
    text_cache.hpp        # STB-truetype atlas
    nav_state.hpp         # focus + keyboard nav state
    input_router.hpp      # hover / focus / capture resolution
src/threadmaxx_ui/
  UIContext.cpp
  Layout.cpp
  Widgets.cpp
  Text.cpp
  Tree.cpp
  Menu.cpp
  Panel.cpp
  DragDrop.cpp
  Gizmo.cpp
  Debug.cpp
  TextCache.cpp           # STB atlas
  backends/
    NullBackend.cpp       # default, drops the draw list (UI1)
    VulkanBackend.cpp     # reference adapter (UI8)
tests/ui/
  test_ui_*.cpp
bench/
  ui_*.cpp
```

## Batch UI1 — Foundations (context + ID stack + draw list)

**Goal**: bring up the `UIContext`, the `WidgetID` stack, the flat
`DrawList` POD stream, and the `NullBackend` test sink. No widgets
yet — just the scaffolding every later batch builds on.

**Test gate**:

- `test_ui_widget_id` — `WidgetID` is stable across frames for the
  same path; ID stack push/pop balances; hashing matches the
  expected FNV-1a-64 value on a fixed input.
- `test_ui_draw_list` — emit 4 rects + 2 lines + 1 clip-push/pop
  pair → backend sees the same count and the same POD bytes;
  `clear()` returns the list to empty without freeing capacity.
- `test_ui_context_lifecycle` — `beginFrame` / `endFrame` round
  trip; mid-frame state asserts on double-begin; backend receives
  exactly one snapshot per `endFrame`.
- `test_ui_no_allocations` — 3 warmup frames, then 100 `begin/end`
  cycles produce zero heap traffic under the tracking allocator.
- `test_ui_rect_math` — `Rect` intersect / contains / union edge
  cases (empty rect, fully contained, partial overlap).

**Files added**:
- `include/threadmaxx_ui/threadmaxx_ui.hpp` — umbrella
- `include/threadmaxx_ui/config.hpp` — capacity caps + defaults
- `include/threadmaxx_ui/types.hpp` — `WidgetID`, `Rect`, `Color`,
  `Vec2`
- `include/threadmaxx_ui/draw.hpp` — `DrawCmd` variant + `DrawList`
- `include/threadmaxx_ui/backend.hpp` — `IUIBackend` interface
- `include/threadmaxx_ui/context.hpp` — `UIContext`
- `include/threadmaxx_ui/detail/id_stack.hpp`
- `include/threadmaxx_ui/detail/rect_math.hpp`
- `src/threadmaxx_ui/UIContext.cpp`
- `src/threadmaxx_ui/backends/NullBackend.cpp`
- `src/threadmaxx_ui/CMakeLists.txt` — static lib `threadmaxx::ui`
- `tests/ui/CMakeLists.txt` + the 5 test files above
- Root `CMakeLists.txt` — gate behind `THREADMAXX_BUILD_UI=ON`

**Risks**:
- `WidgetID` hashing is a hash collision — pin the seed and the
  algorithm (FNV-1a-64) in `types.hpp`.
- `DrawCmd` variant size — keep it under 64 B so cache lines fit
  multiple commands.

**Out of scope**: widgets (UI3), layout (UI2), text (UI4),
input (UI3), real backends (UI8).

## Batch UI2 — Layout primitives

**Goal**: row / column / child / spacing / padding helpers, with a
predictable size-resolution pass. Layout commands are converted to
`Rect`s before widget emit.

**Test gate**:

- `test_ui_layout_row` — 3 children in a horizontal row resolve to
  3 adjacent rects matching the expected widths (fixed + flex +
  fixed).
- `test_ui_layout_column` — same in vertical orientation.
- `test_ui_layout_nested` — row > column > row produces the
  expected leaf-rect grid for a synthetic 3-level tree.
- `test_ui_layout_padding_spacing` — uniform padding + spacing
  subtracts cleanly from the parent rect; zero-padding is a
  no-op.
- `test_ui_layout_clipping` — child rect outside the parent's
  visible window emits a `ClipPush` / `ClipPop` pair around its
  draws.
- `test_ui_layout_overflow` — exceeding the layout stack cap
  (16 deep) asserts in debug, soft-warns + truncates in release.

**Files added**:
- `include/threadmaxx_ui/layout.hpp` — `beginRow` / `beginColumn` /
  `beginChild` / `endLayout` + `Size` / `SizeMode` enums
- `include/threadmaxx_ui/detail/clip_stack.hpp`
- `src/threadmaxx_ui/Layout.cpp`
- 6 `tests/ui/test_ui_layout_*.cpp`

**Risks**:
- Flex resolution is the part most likely to drift between debug
  and release — sum-of-flexes uses integer math throughout, no
  float accumulation.

**Out of scope**: grids (v1.x), tables (v1.x).

## Batch UI3 — Input + interaction (hover / focus / capture)

**Goal**: feed `UIInput` per frame; resolve hover, focus, active
state for synthetic widgets; expose capture flags to the host.
Pure logic — widget shapes ship in UI4.

**Test gate**:

- `test_ui_input_hover` — synthetic 3-widget layout + mouse pos →
  the expected widget is hovered; modeless overlap respects
  z-order (last drawn wins).
- `test_ui_input_focus` — Tab cycles through focusable widgets in
  registration order; Shift-Tab reverses; focus persists across
  frames; focus survives a different-widget-array layout if the
  ID matches.
- `test_ui_input_capture_mouse` — when a widget is being dragged,
  `wantsMouse()` is true; click outside the captured widget does
  not steal capture.
- `test_ui_input_capture_keyboard` — text-input widget focused →
  `wantsKeyboard()` is true; lose focus → false.
- `test_ui_input_determinism` — two contexts, same `UIInput`
  stream, same widget layout → byte-identical draw lists across
  100 frames.

**Files added**:
- `include/threadmaxx_ui/input.hpp` — `UIInput` POD + capture
  query API
- `include/threadmaxx_ui/detail/nav_state.hpp`
- `include/threadmaxx_ui/detail/input_router.hpp`
- 5 `tests/ui/test_ui_input_*.cpp`

**Risks**:
- Focus survival across re-layouts depends on stable `WidgetID`s;
  the test pins this against a re-ordered widget array.

**Out of scope**: keyboard shortcuts / menus (UI5).

## Batch UI4 — Primitive widgets

**Goal**: the FR-2 list — label, button, checkbox, radio, slider,
drag-scalar, input-text, separator, image placeholder, selectable
item, tooltip.

**Test gate**:

- `test_ui_widget_button` — click inside emits click event;
  release outside cancels; disabled button consumes hover but
  not click.
- `test_ui_widget_checkbox` — click toggles the bound `bool&`;
  visual reflects the value on the next frame.
- `test_ui_widget_radio` — three radios bound to the same enum;
  selecting one unselects the others.
- `test_ui_widget_slider` — drag changes the value linearly;
  clicking the track snaps to the click position; min/max clamps
  respected.
- `test_ui_widget_drag_scalar` — drag-to-edit float; Ctrl + drag
  fine-grain; Shift + drag coarse.
- `test_ui_widget_input_text` — typing appends to the buffer;
  backspace deletes; cursor advances; Enter commits + emits a
  `text-committed` event.
- `test_ui_widget_selectable` — exclusive selection in a list of
  10; selection state survives frames.
- `test_ui_widget_tooltip` — hover for ≥0.5s emits a tooltip rect
  positioned next to the host widget.
- `test_ui_widget_no_allocations` — 100 frames over a synthetic
  50-widget panel produce zero heap traffic after warmup.

**Files added**:
- `include/threadmaxx_ui/widget.hpp` — every primitive in FR-2
- `src/threadmaxx_ui/Widgets.cpp`
- 9 `tests/ui/test_ui_widget_*.cpp`

**Out of scope**: trees (UI5), menus (UI5), property inspection
(UI6).

## Batch UI5 — Trees + menus + popups

**Goal**: tree nodes with expand/collapse memory; menu bars; popups
(context menu + dropdown); keyboard navigation through menus.

**Test gate**:

- `test_ui_tree_expand` — clicking the expand chevron toggles
  open state; state persists across frames; `setOpen(id, bool)`
  works programmatically.
- `test_ui_tree_keyboard_nav` — Left collapses, Right expands,
  Up/Down move between visible nodes (skipping collapsed
  subtrees).
- `test_ui_menu_bar` — top-level menu items open submenus on
  click; click outside closes; hovering between siblings keeps the
  open menu pinned.
- `test_ui_popup_modal` — popup eats all mouse input until closed;
  Escape closes; click-outside closes.
- `test_ui_menu_keyboard_nav` — arrow keys navigate menu items;
  Enter activates; Escape pops one level.

**Files added**:
- `include/threadmaxx_ui/tree.hpp` — `treeNode` / `collapsingHeader`
- `include/threadmaxx_ui/menu.hpp` — `beginMenu` / `menuItem` /
  `beginPopup`
- `src/threadmaxx_ui/Tree.cpp`
- `src/threadmaxx_ui/Menu.cpp`
- 5 `tests/ui/test_ui_(tree|menu|popup)_*.cpp`

**Out of scope**: rich text in menu items (v1.x), pinned/torn-off
menus (v1.x).

## Batch UI6 — Property inspector

**Goal**: `inspect.hpp` — overloaded `inspect(ctx, label, T&)` for
the FR-13 list (bool, int, float, string, enums, vectors, handles).

**Test gate**:

- `test_ui_inspect_primitives` — `inspect(ctx, "x", int&)` renders
  a drag-scalar; editing it mutates the bound int; same for
  float, string, bool.
- `test_ui_inspect_enum` — `inspect(ctx, "mode", MyEnum&)` renders
  a dropdown; the host registers the value→name mapping; selection
  writes back.
- `test_ui_inspect_vector` — `inspect(ctx, "pos", Vec3&)` renders
  3 drag-scalars in a row, sharing one label; each component
  edits independently.
- `test_ui_inspect_handle` — `inspect(ctx, "id", EntityHandle&)`
  renders as a read-only label by default; opt-in editable via
  an overload.
- `test_ui_inspect_no_allocations` — 100 frames over a panel of
  20 mixed-type rows produce zero heap traffic after warmup.

**Files added**:
- `include/threadmaxx_ui/inspect.hpp` — overload set
- 5 `tests/ui/test_ui_inspect_*.cpp`

**Out of scope**: reflection-driven inspector (v1.x; depends on a
future `threadmaxx_reflect`), undo/redo (editor-side).

## Batch UI7 — Panels + drag/drop + 2D gizmos + debug HUD

**Goal**: movable / resizable panel windows; drag-source /
drop-target with typed payloads; screen-space 2D drag handles
(corner-resize, transform handles for 2D level editing); HUD/
overlay helpers (always-on debug text rows).

**Test gate**:

- `test_ui_panel_move` — drag the title bar moves the panel;
  bounds clamp to the host rect; double-click title bar
  collapses.
- `test_ui_panel_resize` — drag the bottom-right corner resizes;
  minimum size respected.
- `test_ui_dragdrop_lifecycle` — drag-source emits the payload
  on mouse-down + move-past-threshold; drop-target with matching
  type-hash receives it; mismatched type-hash silently rejects;
  Escape cancels mid-drag.
- `test_ui_gizmo_drag_2d` — mouse-down on a handle starts a drag;
  motion delta is published per frame; mouse-up commits; the
  callback fires exactly once per drag transition.
- `test_ui_debug_hud` — `debug::row(label, value)` appends a row
  to the always-on HUD; HUD rebuilds from scratch every frame
  (no state retained between).

**Files added**:
- `include/threadmaxx_ui/panel.hpp`
- `include/threadmaxx_ui/dragdrop.hpp`
- `include/threadmaxx_ui/gizmo.hpp` — 2D screen-space only
- `include/threadmaxx_ui/debug.hpp`
- `src/threadmaxx_ui/Panel.cpp`
- `src/threadmaxx_ui/DragDrop.cpp`
- `src/threadmaxx_ui/Gizmo.cpp`
- `src/threadmaxx_ui/Debug.cpp`
- 5 `tests/ui/test_ui_(panel|dragdrop|gizmo|debug)_*.cpp`

**Out of scope**: 3D world gizmos (editor-side), dock zones (v1.x).

## Batch UI8 — Vulkan reference backend + crowd bench

**Goal**: reference `IUIBackend` implementation against the
engine's existing Vulkan renderer. Convert `DrawList` to vertex
buffers + draw calls. Ship a crowd bench (`ui_crowd_bench`) +
an `examples/ui_demo/` shell so the contract is end-to-end
exercised.

**Test gate**:

- `test_ui_vulkan_backend` — gated on `TARGET threadmaxx_vulkan`;
  headless boot + one frame of synthetic content; bytes uploaded
  match the expected count.
- `bench/ui_crowd_bench` — 500 widgets across 8 panels @ 60 Hz
  draw frequency; bench gate < **1 ms / frame** for the UI build
  phase (excludes renderer cost).
- `examples/ui_demo/main.cpp` — headless demo that exercises every
  widget type for 600 frames and shuts down cleanly.

**Files added**:
- `src/threadmaxx_ui/backends/VulkanBackend.cpp`
- `tests/ui/test_ui_vulkan_backend.cpp` (gated)
- `bench/ui_crowd_bench.cpp` (gated on
  `THREADMAXX_BUILD_BENCHMARKS`)
- `examples/ui_demo/` + its `CMakeLists.txt`
- Root `CMakeLists.txt` — `add_subdirectory(examples/ui_demo)`

**Risks**:
- Vulkan backend pulls a non-trivial dep chain; gate carefully via
  `if (TARGET threadmaxx_vulkan)` so headless CI stays green.

**Out of scope**: per-glyph atlas streaming (v1.x), GPU-driven
geometry generation (NG-8 forbids it for v1.x).

## v1.0 close-out criteria

- ✅ Every batch UI1–UI8 landed and tested.
- ✅ Zero-alloc gate pinned at 500 widgets across 8 panels under
  the tracking allocator (`test_ui_crowd_no_alloc`).
- ✅ Determinism gate green: same input stream + same context →
  byte-identical draw list across 100 frames.
- ✅ Vulkan reference backend boots on the dev target.
- ✅ Bench `ui_crowd_bench` reports < 1 ms / frame UI build phase
  at 500 widgets / 8 panels / 60 Hz.
- ✅ Docs: `README.md`, `USER_GUIDE.md`, `MAINTAINER_GUIDE.md`,
  `CHANGELOG.md` landed under `include/threadmaxx_ui/`.
- ✅ ctest 100% on `build/` AND `build-werror/`
  (`-Wsign-conversion -Wconversion -Wold-style-cast -Werror`).
- ✅ Version stamped at 1.0.0 in
  `include/threadmaxx_ui/version.hpp` —
  `THREADMAXX_UI_VERSION = 10000`, `version_string() = "1.0.0"`.

## v1.x candidate batches (not blocking v1.0)

### v1.x — Docking + tabs

Panel docking with drag-to-dock zones, tab strips, layout
persistence to a `LayoutSnapshot` POD. Bench gate: 50 panels +
40 tabs must stay under 0.5 ms / frame additional cost.

### v1.x — Rich text + Unicode

Full Unicode via HarfBuzz; bidirectional text; per-glyph atlas
streaming. ASCII path stays the default fast path.

### v1.x — Tables

Sortable, resizable, scrollable tables. Common ask from
inspectors that show entity lists.

### v1.x — Theming presets + style stack

`pushStyle` / `popStyle`; named theme presets (light / dark /
custom). v1.0 ships one default theme.

### v1.x — Reflection-driven inspector

When `threadmaxx_reflect` lands, an `inspectReflected(ctx, label,
T&)` overload auto-generates the property panel from component
metadata. The hand-written `inspect.hpp` overloads stay the
fallback.

### v1.x — Animation / transition helpers

`anim::lerp(id, target, speed)` style helpers for smooth
toggle / expand / collapse animations. Optional; UI ships
non-animated by default.

### v1.x — Accessibility pass

High-contrast theme, screen-reader hint strings on every widget,
keyboard-only operation audit.

### v1.x — Multi-window support

Split the `UIBackend` interface into per-window adapters so a
single context can render across multiple OS windows. v1.0 is
single-window.

### v1.x — Capture-to-image diagnostic

Render the current frame's draw list to a PNG for visual
regression testing.

## Out of scope for the whole library

Per `DESIGN_NOTES.md` §§ NG-1..NG-10 — none of this lands at any
version:

- General-purpose application framework
- Windowing / platform abstraction (game owns this)
- Renderer ownership
- Hidden singleton state
- Forced docking / theming / multi-window
- Rich text editor or code editor in v1
- Asset import / filesystem browsing as a hard dep
- Reflection as a hard dep
- GPU-driven UI geometry generation
- UI system that depends on the editor being present
