# threadmaxx_editor

**Editor / tooling / hot-reload sibling library for `threadmaxx`.**

`threadmaxx_editor` is the interactive development layer that sits
above the engine. It is for scene inspection, entity / component
editing, resource browsing, hot reload orchestration, debug overlays,
profiling, world diffing, and other "while-the-game-is-running"
tooling.

The library is **UI-toolkit-agnostic**: every panel emits its UI
through `IEditorBackend`. `HeadlessBackend` ships in the box for
deterministic testing; the Dear ImGui binding lands as a follow-on
batch (E11).

## Status

**v1.0.0 shipped 2026-06-12.** Every E-batch (E1-E10) landed with
headless tests; the v1.0 close-out gates green on `build/` and
`build-werror/`.

## Public surface

| Header | Purpose |
|---|---|
| `session.hpp` | `EditorSession` — bound view of a live `Engine&`. |
| `backend.hpp` | `IEditorBackend` — abstract draw-call sink. |
| `backends/headless.hpp` | `HeadlessBackend` — capture every call into ordered ops for tests. |
| `inspect.hpp` | `Inspector` — read-only summaries of entities / resources / systems. |
| `commands.hpp` | `IEditCommand` + `CommandStack` — undo/redo on the engine's deterministic-commit path. |
| `hotreload.hpp` | `HotReloadController` — orchestration over `markResourceStale` + `AssetReloaded`. |
| `telemetry.hpp` | `TelemetryOverlay` — FPS / frame time / per-system stats overlay. |
| `selection.hpp` | `SelectionState` — single-source-of-truth for the currently-selected entity / resource / system. |
| `properties.hpp` | `PropertyEditor` — reflection-driven property panel built on `threadmaxx_reflect`. |
| `gizmo.hpp` | `TranslateGizmo` — pure-math 3-axis translate handles, hit-tests, drag → command. |
| `diff.hpp` | `WorldDiff::compute(a, b)` — pairwise `WorldSnapshot` diff. |
| `layout.hpp` | `LayoutManager` — save/load editor layout to a stream. |
| `console.hpp` | `Console` — typed command console wired to `CommandStack`. |
| `types.hpp` | `SessionId`, `SelectionKind`, `EditResult`. |
| `version.hpp` | `THREADMAXX_EDITOR_VERSION` + `version_string()`. |

## Design principles

1. **Above the engine** — no changes to core simulation behavior.
2. **Reflective, not magical** — uses `threadmaxx_reflect` for type
   introspection; never reaches into engine private state.
3. **Command-based edits** — every mutation is an `IEditCommand`
   funneled through the engine's `CommandBuffer` path.
4. **Undo/redo first** — every edit is reversible.
5. **UI is optional** — the headless backend exercises the full
   editor surface in the test suite.
6. **Renderer-agnostic UI backend** — ImGui, SDL, web, native all
   plug into `IEditorBackend`.

See `DESIGN_NOTES.md` for the full spec and `MAINTAINER_GUIDE.md` for
the contribution model.

## Building

`threadmaxx_editor` is opted in by `-DTHREADMAXX_BUILD_EDITOR=ON`
(the default). Static library `threadmaxx::editor`; PUBLIC depends on
`threadmaxx::threadmaxx`; PUBLIC depends on `threadmaxx::reflect`
when both are built (which exports
`THREADMAXX_EDITOR_HAS_REFLECT=1`).
