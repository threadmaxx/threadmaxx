# `threadmaxx_editor` — batch plan

Sibling-library implementation plan. `DESIGN_NOTES.md` is the
authoritative spec; this doc breaks it down into shippable
test-driven batches.

Status: **E1 landed 2026-06-12**. Remaining batches 📋 planned.
Sequencing follows the §8 "implementation order" of the design
notes, regrouped into shippable units that each carry their own
tests.

## Conventions

Each batch is independently shippable:

- **Goal** — what the batch accomplishes in one sentence.
- **Test gate** — assertions that prove the batch landed.
- **Files** — what's added / modified.
- **Risks** — what could break.
- **Out of scope** — explicitly deferred to a later batch.

The library produces a static library `threadmaxx::editor` plus
public headers. The **core stays UI-toolkit-agnostic**: every
piece of editor logic (inspection, commands, hot-reload,
selection, gizmo math) is testable headless. Concrete UI
implementations (ImGui first; remote / native later) bind into the
core through `IEditorBackend`.

Tests run against a `HeadlessBackend` that records every draw call
into a captured-frame structure. Real-UI backends ship later and
are not part of the v1.0 test suite.

## Library structure (target end-state)

```
include/threadmaxx_editor/
  threadmaxx_editor.hpp    # umbrella
  session.hpp              # EditorSession
  inspect.hpp              # Inspector
  commands.hpp             # IEditCommand / CommandStack
  hotreload.hpp            # HotReloadController
  selection.hpp            # Selection
  hierarchy.hpp            # entity tree
  properties.hpp           # property editors
  telemetry.hpp            # TelemetryOverlay
  diff.hpp                 # snapshot diffs
  gizmo.hpp                # transform gizmos
  asset_browser.hpp        # resource tree
  console.hpp              # command console
  layout.hpp               # docking / layout state
  serialization.hpp        # layout save/load
  backend.hpp              # IEditorBackend
  detail/
    type_registry.hpp
    field_reflection.hpp
    undo_stack.hpp
    command_queue.hpp
    stable_id_map.hpp
src/threadmaxx_editor/
  EditorSession.cpp
  Inspector.cpp
  CommandStack.cpp
  HotReloadController.cpp
  TelemetryOverlay.cpp
  Gizmo.cpp
  WorldDiff.cpp
  backends/
    HeadlessBackend.cpp    # for tests (deterministic capture)
    ImGuiBackend.cpp       # E11 / v1.x
tests/editor/
  test_editor_*.cpp
```

## Batch E1 — Foundations (session + headless backend) ✅ landed 2026-06-12

**Goal**: `EditorSession` wraps an `Engine&`. `IEditorBackend`
contract is exercisable through a `HeadlessBackend` that records
every `drawText` / `drawRect` call. Used as the test substrate for
every later batch.

**Test gate**:

- `test_editor_session_attach` — construct EditorSession over an
  Engine; `session.engine()` returns the same instance.
- `test_editor_headless_backend_capture` — call beginFrame,
  drawText("hello", 10, 20), drawRect(0,0,100,100), endFrame;
  the captured frame has exactly those two ops in order.
- `test_editor_session_destroy_order` — destroying the session
  before the engine is safe; destroying the engine while the
  session is alive logs a warning (configurable).

**Files**: `session.hpp`, `backend.hpp`, umbrella header,
`src/EditorSession.cpp`, `src/backends/HeadlessBackend.cpp`,
three tests.

**Out of scope**: ImGui backend (E11 / v1.x), real layout (E10).

## Batch E2 — Inspector (entity / resource / system listing) ✅ landed 2026-06-12

**Goal**: `Inspector` reads engine-exposed data and produces
summaries. No mutation, no UI — just data-extraction logic.

**Test gate**:

- `test_editor_inspector_entities` — engine with 10 spawned
  entities → `listEntities()` returns 10 summaries with correct
  component lists.
- `test_editor_inspector_resources` — register 3 resources in the
  engine's ResourceRegistry → `listResources()` returns matching
  summaries with correct ref counts.
- `test_editor_inspector_systems` — register 4 systems →
  `listSystems()` returns 4 summaries with correct wave indices
  and lastStepMs values.
- `test_editor_inspector_entity_specific` — `entity(handle)`
  returns Some for a live handle, None for a stale one.

**Files**: `inspect.hpp`, `src/Inspector.cpp`.

**Risks**: ref-counting access — the engine's
`ResourceHandle<T>::refCount` requires a registry lookup. Cache
inspection results once per frame in the Inspector to avoid
hammering the engine.

**Out of scope**: live filtering / search (E10 console).

## Batch E3 — Command stack + IEditCommand + undo/redo ✅ landed 2026-06-12

**Goal**: every editor mutation funnels through a command. The
command knows how to apply + how to undo. `CommandStack` tracks
the history.

**Test gate**:

- `test_editor_command_basic` — register a `SetTransform` command;
  execute it; engine's world reflects the new transform; undo
  reverts.
- `test_editor_command_stack_redo` — apply A, undo A, redo A →
  world matches post-A state.
- `test_editor_command_stack_new_after_undo` — apply A, undo A,
  apply B → redo of A is no longer possible.
- `test_editor_command_clear` — `clear()` empties the stack.

**Files**: `commands.hpp`, `detail/undo_stack.hpp`,
`src/CommandStack.cpp`.

**Risks**: command commits must respect the engine's
deterministic-commit discipline. Use the engine's `CommandBuffer`
in `apply()` rather than mutating world state directly.

**Out of scope**: transaction groups (multiple commands as one
undo unit) — E10 / v1.x.

## Batch E4 — Hot reload controller

**Goal**: orchestrate the engine's existing
`Engine::markResourceStale<T>` + `AssetReloaded` event channel.
Editor-side state tracks pending reloads.

**Test gate**:

- `test_editor_hotreload_request` — `requestReload({"foo.png"})`
  → engine fires `markResourceStale`; loader picks it up.
- `test_editor_hotreload_event_observed` — `AssetReloaded` event
  fires → controller's `pendingReloads()` no longer contains the
  reloaded path.
- `test_editor_hotreload_cancel` — `cancelReload("foo.png")`
  removes it from pending without crashing the loader.

**Files**: `hotreload.hpp`, `src/HotReloadController.cpp`.

**Risks**: this batch is the editor's first cross-thread surface
(events fire on the engine's sim thread; UI typically reads on a
render thread). Document the thread-safety expectations clearly.

**Out of scope**: file-system watcher (game-side / OS-specific —
controller exposes the hooks, but the watcher loop is not in v1.0).

## Batch E5 — Telemetry overlay

**Goal**: a polled view of engine `FrameSnapshot` data — FPS,
frame time, per-system stats, optional trace event count. Renders
through the backend so the headless suite can verify the draw
calls.

**Test gate**:

- `test_editor_telemetry_basic` — overlay enabled; advance the
  engine one tick; backend's captured frame contains "FPS: ..." +
  "Frame: ... ms" text ops.
- `test_editor_telemetry_config_off` — `showFPS = false` removes
  the FPS line from the captured frame.
- `test_editor_telemetry_system_stats` — `showSystemStats = true`
  produces one text op per registered system.

**Files**: `telemetry.hpp`, `src/TelemetryOverlay.cpp`.

**Out of scope**: graphical overlays (frame-time graph, profiler
flamegraph) — v1.x with a real UI backend.

## Batch E6 — Selection state

**Goal**: `Selection` POD + selection-history tracking. The
editor "what's currently selected" is a first-class concept the
other panels (property editor, gizmo) consume.

**Test gate**:

- `test_editor_selection_set` — `setSelection(entity, handle)` →
  `currentSelection()` returns the same.
- `test_editor_selection_clear` — `clearSelection()` →
  `currentSelection().kind == None`.
- `test_editor_selection_stale_entity` — entity handle whose
  generation no longer matches is auto-cleared on next access
  (avoids dangling).

**Files**: `selection.hpp`.

**Out of scope**: multi-select (v1.x), selection sets / groups.

## Batch E7 — Property editing + reflection

**Goal**: a property panel that reads a selected entity's
components and exposes their fields for edit. Each edit becomes a
command (so it's undoable).

**Test gate**:

- `test_editor_property_read` — select an entity with a known
  `Transform`; property reader returns `position.x`, `.y`, `.z`
  with the right values.
- `test_editor_property_edit` — set `position.x = 5` via the
  property editor; the resulting command applies; undo reverts.
- `test_editor_property_custom_type` — register a custom
  reflection for a user component; field appears in the panel.

**Files**: `properties.hpp`, `detail/field_reflection.hpp`.

**Risks**: C++ reflection is still pre-standard. Recommendation:
ship a **manual registration** API for v1.0 — `registerField<T,
&T::x>("x")` — and migrate to reflection codegen when C++26
lands.

**Out of scope**: nested struct editing beyond depth=1 (v1.x).

## Batch E8 — Gizmos

**Goal**: transform gizmo (translate/rotate/scale handles) math.
The library produces a `GizmoFrame` describing handle positions +
hit tests + active-drag state; the backend draws it. Math is
testable without a backend.

**Test gate**:

- `test_editor_gizmo_handle_positions` — gizmo for an
  identity-transform entity → 3 axis handles at unit positions in
  X/Y/Z.
- `test_editor_gizmo_hit_test` — ray through the X-handle picks
  it; ray through empty space doesn't pick anything.
- `test_editor_gizmo_drag_translate` — drag the X handle by
  (5,0,0) world units → resulting command sets transform's
  position to (5,0,0) (or +5 relative, configurable mode).

**Files**: `gizmo.hpp`, `src/Gizmo.cpp`.

**Out of scope**: rotation arcs UI (math is in scope; UI is
backend-specific). World-space vs. local-space mode toggle (v1.x
ergonomics polish).

## Batch E9 — World diff

**Goal**: compare two `WorldSnapshot`s and produce a list of
per-entity differences. Useful for debugging, save-game
inspection, and (later) the replay-session attach mode from
DESIGN_NOTES §6.3.

**Test gate**:

- `test_editor_diff_identical` — diff(s, s) returns empty.
- `test_editor_diff_added` — diff(snapshot1, snapshot1 + 5
  entities) returns 5 `Added` entries.
- `test_editor_diff_modified` — diff against a snapshot where
  one entity's Transform changed returns 1 `Modified` entry with
  the field-level diff.
- `test_editor_diff_removed` — diff against a snapshot missing 2
  entities returns 2 `Removed` entries.

**Files**: `diff.hpp`, `src/WorldDiff.cpp`.

**Out of scope**: 3-way diff / merge (v1.x — useful for
collaborative editing, not yet a real need).

## Batch E10 — Layout persistence + console

**Goal**: save/load editor layout (which panels are visible, dock
arrangement, selected tabs) to a stream. Plus a basic console for
typed commands (lives behind the same `IEditCommand` infrastructure).

**Test gate**:

- `test_editor_layout_roundtrip` — save layout, load into a fresh
  manager, all panels match.
- `test_editor_console_exec` — type `setTransform 5 0 0`, verify
  the resulting command applies.
- `test_editor_console_history` — up-arrow recovers previous
  commands.

**Files**: `layout.hpp`, `console.hpp`, `serialization.hpp`,
`detail/stable_id_map.hpp`.

**Out of scope**: scripting (Lua / Python) — game-side; library
just exposes the `IEditCommand` hook.

## v1.0 close-out criteria

- ✓ Every batch E1–E10 landed and tested headlessly.
- ✓ HeadlessBackend exercises the full editor surface across the
  test suite (no behavior depends on a real UI toolkit).
- ✓ End-to-end demo (lives in `examples/editor_demo/`) attaches
  the editor to a small running engine and shows: inspector panel,
  property edit + undo, hot-reload trigger, telemetry overlay,
  selection + gizmo drag, layout save/load.
- ✓ Docs: README, USER_GUIDE, MAINTAINER_GUIDE, plus a
  `BACKEND_PORTING_GUIDE.md` for future UI-toolkit authors.
- ✓ ctest 100% on `build/` and `build-werror/`.
- ✓ Version stamped at 1.0.0 in
  `include/threadmaxx_editor/version.hpp`.

## v1.x candidate batches (not blocking v1.0)

### v1.x (E11) — ImGui backend

The expected first real UI binding. Implements `IEditorBackend`
against Dear ImGui. Lives under
`src/threadmaxx_editor/backends/ImGuiBackend.cpp`. Standalone
binary `examples/editor_demo` becomes the integration smoke test.

### v1.x — Remote backend

Editor-as-server architecture: a browser frontend renders the UI;
the editor backend serializes draw calls to JSON over a WebSocket.
Useful for debugging deployed builds and for remote-pair-editing.

### v1.x — Asset browser

DESIGN_NOTES §3 lists `asset_browser.hpp`. v1.0 deferred because
asset structure is game-specific; v1.x ships a generic resource
tree that reflects whatever the engine's ResourceRegistry holds.

### v1.x — Scene hierarchy / entity tree view

DESIGN_NOTES §3 lists `hierarchy.hpp`. v1.0 deferred because the
flat `listEntities()` from E2 covers most usage; v1.x adds a tree
view rooted on the engine's `Parent` component graph.

### v1.x — Transaction groups

Multi-command-as-one-undo. Useful for "rotate + scale" combo
operations. Hooks into CommandStack via begin/commit transaction
calls.

### v1.x — Property editor: deep struct introspection

Recursive into nested PODs. v1.0 stopped at depth 1; v1.x makes
the field reflection format recursive.

### v1.x — Live profiler view

A flamegraph / timeline view of the engine's `FrameSnapshot`. The
data is already there via `ChromeTraceWriter`; v1.x renders it
through the backend rather than asking the user to load JSON in
chrome://tracing.

### v1.x — Capture / replay mode

DESIGN_NOTES §6.3 lists this. Editor attaches to a saved snapshot
stream instead of a live engine; same inspector/diff API, no
mutation.

### v1.x — Diff-as-PR-review

The world-diff from E9 rendered as a Git-PR-style "what changed
between save A and save B" view. Useful for QA and version
control of game saves.

## Out of scope for the whole library

Per DESIGN_NOTES §7 — none of this lands at any batch:

- Engine internals access (everything goes through public API)
- Simulation authority
- Renderer backend ownership
- Physics solver ownership
- Navmesh ownership
- Audio ownership
- Networking protocol ownership
- Hidden mutation path outside commands
- Mandatory GUI toolkit dependency in the core
