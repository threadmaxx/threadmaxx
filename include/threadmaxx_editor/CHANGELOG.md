# threadmaxx_editor — Changelog

## v1.0.0 — 2026-06-12

First stable release. Every E-batch (E1-E10) landed; gates green on
both `build/` and `build-werror/`; 34 tests across the editor slice.

### Added

- `EditorSession` — non-owning view of a live `Engine&`, atomically-
  incremented `SessionId`, attach/detach `IEditorBackend`.
- `IEditorBackend` + `HeadlessBackend` — capture-only backend used by
  the entire test suite.
- `Inspector` — `listEntities()` / `listResources()` / `listSystems()`
  + per-entity `entity(handle)` lookup. Resource enumeration via opt-in
  `trackResource<T>(id, name)`.
- `IEditCommand` + `CommandStack` — undo/redo deck shared with an
  engine-registered pump-system that drains pending applies/undos on
  every `preStep` via `ctx.single()`. Commits land on the engine's
  deterministic-commit path.
- `HotReloadController` — `requestReload({path})` → `markResourceStale`;
  `pendingReloads()` tracks in-flight reloads; `AssetReloaded`
  subscription drains matching paths and re-binds tracked ids to the
  freshly-installed (index, generation).
- `TelemetryOverlay` — polls `Engine::frameSnapshot()`; renders FPS +
  frame time + per-system stats through the backend.
- `SelectionState` — single-source-of-truth for the currently selected
  entity / resource / system. Entity selections auto-clear on
  generation mismatch.
- `PropertyEditor` — reflection-driven property panel built on
  `threadmaxx_reflect`. `addBuiltinBindings()` wires Transform /
  Velocity / Acceleration / Health / Faction / UserData /
  BoundingVolume; game-side `addBinding(...)` plugs in custom
  components.
- `TranslateGizmo` — pure-math 3-axis translate handles, ray hit-test,
  drag delta projection, ready-to-execute `SetTransform`-style
  `IEditCommand`.
- `WorldDiff::compute(a, b)` — per-entity Added / Removed / Modified
  with byte-wise component value comparison.
- `LayoutManager` save/load — key=value\n stream with \ / n escape.
- `Console` — `registerCommand(verb, handler)` + `exec(stack, line)`,
  64-entry newest-first history.

### E11 — ImGui backend (shipped 2026-06-12, opt-in)

`ImGuiBackend` (`include/threadmaxx_editor/backends/imgui.hpp` +
`src/threadmaxx_editor/backends/ImGuiBackend.cpp`) translates
`drawText` / `drawRect` into Dear ImGui calls.

Opt-in via `-DTHREADMAXX_EDITOR_FETCH_IMGUI=ON` — the root build
FetchContent-pulls Dear ImGui v1.91.5, assembles a static `imgui`
target, and `threadmaxx_editor` exports
`THREADMAXX_EDITOR_HAS_IMGUI_BACKEND=1`. Two tests
(`test_editor_imgui_backend_smoke`, `test_editor_imgui_overlay`)
exercise the backend in null/headless ImGui mode.

### Out of v1.0 (deferred to v1.x)

- Asset browser (game-specific; v1.x ships a generic
  ResourceRegistry-backed tree).
- Scene hierarchy / entity tree view (Parent-graph rooted; flat
  `listEntities()` covers most v1.0 usage).
- Transaction groups (multi-command-as-one-undo).
- Recursive struct introspection beyond depth 1 in property editing.
- Live profiler / flamegraph view.
- Capture / replay attach mode.
- Diff-as-PR-review rendering of `WorldDiff` output.
- File-system watcher for hot reload (game-side / OS-specific).
- 3-way merge for collaborative editing.

These do not affect the v1.0 contract; the headless test suite covers
every shipped surface.
