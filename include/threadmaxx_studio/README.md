# threadmaxx_studio

**Panel host + attach environment for the `threadmaxx_*` suite.**

`threadmaxx_studio` is the diagnostics shell that hosts every per-sibling
inspector panel against a running engine, a paused replay, or a remote
shard. It is **not** the editor (that's `threadmaxx_editor`), and it is
**not** a renderer — every panel renders through `editor::IEditorBackend`
and reads world state through `IStudioDataSource`, so the same panel
binary serves both in-process (Shape A) and out-of-process (Shape B)
attach modes.

## Status

**v1.0.0 shipped 2026-06-13.** Every batch ST1–ST41 landed with headless
tests; the v1.0 close-out gates green on `build/` and `build-werror/`.

## Use cases

1. **Engine development companion** — same process as the game, ImGui
   overlay rendered into the host's existing swapchain. Inspector,
   property editor, telemetry, gizmo, hot reload, command stack.
2. **Headless replay debugger** — load a `FrameSnapshot` log + a
   `WorldSnapshot`, scrub through ticks, see what the engine was doing
   at the divergence point. No engine running.
3. **Out-of-process attach** — separate studio binary talks to a running
   game through `threadmaxx_network`'s loopback or remote transport.
   The MMORPG shard inspector case: studio never blocks the simulation
   and the agent on the game side is tiny.
4. **CI artifact viewer** — `FrameSnapshot` Chrome-trace JSONs, world
   snapshots, migration validation reports rendered as panels.

## Public surface

| Header | Purpose |
|---|---|
| `version.hpp` | `THREADMAXX_STUDIO_VERSION` + `version_string()`. |
| `panel.hpp` | `IStudioPanel` — the polymorphic panel interface. |
| `data_source.hpp` | `IStudioDataSource` + `AttachMode` enum + `EngineFrameSummary`. |
| `studio.hpp` | `StudioApp` + `PanelHost` — top-level shell. |
| `direct_data_source.hpp` | `DirectDataSource` (Shape A; in-process). |
| `remote_data_source.hpp` | `RemoteDataSource` (Shape B; out-of-process). |
| `agent.hpp` | `StudioAgent` — game-side RPC endpoint for Shape B. |
| `shard_directory.hpp` | `ShardDirectory` — multi-shard selection. |
| `panels/*.hpp` | One header per concrete panel (~30 panels). |

## Building

Opt in via `-DTHREADMAXX_BUILD_STUDIO=ON` (the default). Static
library `threadmaxx::studio`; PUBLIC depends on `threadmaxx::editor`,
optionally on every other sibling (PUBLIC link when the sibling target
exists; matching `THREADMAXX_STUDIO_HAS_*` compile definitions gate
the panel sources).

The library itself does **not** PUBLIC-link `threadmaxx::threadmaxx`.
The core engine link is private to `DirectDataSource.cpp` only — kept
that way so a future Shape-B-only studio binary does not have to drag
the engine in.

## Design principles

1. **Above the engine** — never reaches around the data source.
2. **Renderer-agnostic** — panels emit through `editor::IEditorBackend`.
3. **Two attach modes, one panel binary** — `IStudioDataSource` is the
   only difference between Shape A and Shape B.
4. **Sibling-independent at link time** — every panel is gated by
   `if (TARGET threadmaxx::<sibling>)` in CMake; turning a sibling off
   removes the panel cleanly.
5. **Editor owns the wire** — Shape B is layered over the editor v1.x
   remote backend; studio does not invent its own protocol.
6. **Headless tests, every panel** — the headless backend exercises
   every panel from `tests/studio/test_studio_*.cpp`.

See `DESIGN_NOTES.md` for the full spec, `USER_GUIDE.md` for the
host-author walkthrough, `MAINTAINER_GUIDE.md` for the contribution
model, and `PANEL_AUTHORING_GUIDE.md` for the per-panel recipe.

## At a glance

- ~30 panels across 9 sibling libraries plus 5 engine-only panels
- Shape A + Shape B both pinned by the test suite (49 tests, all green)
- < 1 ms / frame overhead at 1080p with 50 visible widgets
  (`bench/studio_overhead`)
- Headless renders bit-stable across runs (every panel test is
  deterministic)
