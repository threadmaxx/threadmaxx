# threadmaxx_ui CHANGELOG

## v1.0.0 — 2026-06-11

Initial release. Immediate-mode UI primitives for editor chrome +
in-game debug overlays. Shipped across eight batches (UI1–UI8);
per-batch detail in `FUTURE_WORK.md`.

### Highlights

- **Frame context + ID stack** — UIContext owns per-frame state;
  WidgetID hashed via FNV-1a-64; multiple contexts supported for
  split views.
- **Layout** — pure-function `resolveRow` / `resolveColumn` (fixed +
  flex with last-flex-absorbs-rounding-leftover); scoped layout +
  clip stacks with overflow counters.
- **Input + interaction** — UIInput POD fed by the host; last-
  registered-wins hover; sticky mouse capture; Tab / Shift-Tab /
  arrow-key focus cycle.
- **Widgets** — label, button (+ disabled style), checkbox, radio,
  slider, drag-scalar (Ctrl 0.5x / Shift 2x), input-text, separator,
  image placeholder, selectable, tooltip.
- **Trees + menus + popups** — retained open/close state; popup
  state machine (one open at a time); menu bar with latched hover-
  to-switch.
- **Property inspector** — `inspect()` overloads for bool / int32 /
  float / string / Vec3 / handle; `inspectEnum<E>` template. No
  reflection dependency.
- **Panels + drag/drop + gizmos + debug HUD** — movable / resizable
  panels with double-click collapse; typed-payload drag/drop
  (FNV-1a-64 hash); screen-space 2D drag handles; stateless HUD
  rows.
- **Backends** — NullBackend (test sink), VertexBackend (renderer-
  neutral tessellation to flat vertex / index / draw streams).

### Performance

- `bench/ui_crowd_bench` at 512 widgets, 8 panels:
  **0.211 ms / frame** UI build phase
  (~5x under the < 1 ms gate).
- Zero-allocation contract pinned at 512 simultaneously-visible
  widgets via `test_ui_crowd_no_alloc`.

### Test coverage

42 tests in `tests/ui/`, all green on `build/` and `build-werror/`
(`-Wsign-conversion -Wconversion -Wold-style-cast -Werror`).
Categories: foundations / layout / input / widgets / trees+menus /
inspector / panels+dragdrop+gizmos+HUD / backend + v1.0 gate.

### Public API

Every header under `include/threadmaxx_ui/` (except `detail/`) is
part of the v1.x ABI contract. SemVer bump rules + deprecation
policy are documented in `MAINTAINER_GUIDE.md`.
