# `threadmaxx_studio` — batch plan

Sibling-library implementation plan. `DESIGN_NOTES.md` is the
authoritative spec (architecture, sibling-gap analysis, locked
decisions); this doc is the schedule + scoreboard.

Status: **📋 not started**. Pending sibling prep (see
`DESIGN_NOTES.md` §11). Studio scope = 41 batches across 9
milestones. Companion prep batches in other libraries are tracked
in those libraries' own `FUTURE_WORK.md`.

## Conventions

Each batch is independently shippable:

- **Goal** — what the batch accomplishes in one sentence.
- **Test gate** — assertions that prove the batch landed.
- **Files** — what's added / modified.
- **Risks** — what could break.
- **Out of scope** — explicitly deferred to a later batch.

The library produces a static library `threadmaxx::studio` plus
public headers under `include/threadmaxx_studio/`. Concrete panels
live under `src/threadmaxx_studio/panels/`; each panel knows about
exactly one sibling's headers and is build-time-gated on that
sibling being linked.

The studio is a **panel host**, not a renderer and not a second
editor. See `DESIGN_NOTES.md` §1 for the per-concept ownership
table.

## Prerequisite batches (in other libraries)

Studio batches gate on these landing first; tracked in the sibling
library's own `FUTURE_WORK.md`.

| Prereq | Library | Studio batches unblocked |
|---|---|---|
| A9 — Diagnostics | `threadmaxx_animation` | ST15 |
| AU9 — Diagnostics | `threadmaxx_audio` | ST16 |
| N10 — Diagnostics | `threadmaxx_navmesh` | ST20 |
| P10 — Diagnostics | `threadmaxx_physics` | ST21 |
| NW11 — `PeerSummary` | `threadmaxx_network` | ST25 |
| A9 (assets) — resident enumerate | `threadmaxx_assets` | ST18 |
| E12 — Scene hierarchy | `threadmaxx_editor` | ST7 |
| E13 — Live profiler view | `threadmaxx_editor` | ST11 |
| E14 — Remote backend + wire | `threadmaxx_editor` | ST29 (gates all of M7) |
| E15 — Capture/replay | `threadmaxx_editor` | ST23 |
| Migration v1.0 (M1–M8 + close-out) | `threadmaxx_migration` | ST35 (gates all of M8) |

Under the locked D1 strict reading (see DESIGN_NOTES §12.4),
**no studio batch starts** until all 19 prereq batches land.
Under the proposed spirit reading, ST1 starts in parallel with
Phase 1 (sibling diagnostics) since M1–M3 batches have no prereq
gates.

## Library structure (target end-state)

```
include/threadmaxx_studio/
  threadmaxx_studio.hpp     # umbrella
  version.hpp
  config.hpp                # opt-in cmake gates, capacity caps
  studio.hpp                # StudioApp, PanelHost
  panel.hpp                 # IStudioPanel interface
  data_source.hpp           # IStudioDataSource + AttachMode enum
  panels/
    engine_inspector.hpp    # wraps editor::Inspector
    property_editor.hpp     # wraps editor::PropertyEditor + reflect
    hierarchy.hpp           # wraps editor::HierarchyView (E12)
    gizmo.hpp               # wraps editor::Gizmo
    world_diff.hpp          # wraps editor::WorldDiff
    frame_snapshot.hpp      # wraps Engine HudTraceSink
    profiler.hpp            # wraps editor::ProfilerView (E13)
    task_graph.hpp
    tuning.hpp
    resources.hpp
    animation.hpp           # gated TARGET threadmaxx::animation
    audio.hpp               # gated TARGET threadmaxx::audio
    input.hpp               # gated TARGET threadmaxx::input
    assets.hpp              # gated TARGET threadmaxx::assets
    ui_inspector.hpp        # gated TARGET threadmaxx::ui
    navmesh.hpp             # gated TARGET threadmaxx::navmesh
    physics.hpp             # gated TARGET threadmaxx::physics
    reflect.hpp             # gated TARGET threadmaxx::reflect
    replay.hpp              # gated editor E15
    network_session.hpp     # gated TARGET threadmaxx::network
    network_snapshot_delta.hpp
    network_interest.hpp
    network_desync.hpp
    network_packet_trace.hpp
    save_inspector.hpp      # gated TARGET threadmaxx::migration
    migration_step.hpp
    migration_schema_graph.hpp
    migration_validator.hpp
src/threadmaxx_studio/
  StudioApp.cpp
  PanelHost.cpp
  DirectDataSource.cpp
  RemoteDataSource.cpp      # M7
  StudioAgent.cpp           # M7 (game-side)
  panels/
    *.cpp                   # one per panel header
tests/studio/
  test_studio_*.cpp
examples/studio_demo/
  main.cpp                  # M9 close-out demo
bench/
  studio_overhead.cpp       # M9 close-out perf gate
```

## M1 — Studio shell + panel framework (4 batches)

Post-audit (DESIGN_NOTES §12) M1 dropped a batch — `DockMaster` is
gone (consumes `editor::LayoutManager`), and the console subsystem
became a thin panel wrapper of `editor::Console`.

### ST1 — Library scaffold + core interfaces

**Goal**: stand up the static lib, the version stamp, and the two
load-bearing interfaces (`IStudioPanel`, `IStudioDataSource`). No
panels yet, no `StudioApp` yet.

**Test gate**:
- `test_studio_version` — `THREADMAXX_STUDIO_VERSION = 100`,
  `version_string() = "0.1.0-dev"`.
- `test_studio_panel_interface_canary` — `IStudioPanel` is a pure
  virtual interface; `id()` / `title()` / `render()` /
  `onAttachChanged()` declared.
- `test_studio_data_source_canary` — `IStudioDataSource` is a pure
  virtual interface; every optional accessor (animationStats,
  audioStats, etc.) returns `std::nullopt` from a default impl.
- `test_studio_no_engine_link_canary` — TU includes
  `threadmaxx_studio/panel.hpp` + `data_source.hpp` without
  pulling any `threadmaxx/` core header.

**Files**: `version.hpp`, `config.hpp`, `panel.hpp`,
`data_source.hpp` (with `AttachMode` enum), umbrella header,
`src/CMakeLists.txt` (gated by `THREADMAXX_BUILD_STUDIO=OFF`),
four tests, root `CMakeLists.txt` gate.

**Risks**: pulling editor + reflect + ui transitively. Mitigate
by making `data_source.hpp` use forward declarations + `std::optional`
returns; nothing in M1 requires the sibling headers to be visible
in studio's umbrella.

**Out of scope**: `StudioApp`, concrete panels, mutations.

### ST2 — `StudioApp` + `PanelHost`

**Goal**: an actual app that hosts panels. `StudioApp` owns a
`PanelHost`; `PanelHost` owns the panel list. Layout state goes
through `editor::LayoutManager` (E10) — studio does NOT define a
parallel layout system.

**Test gate**:
- `test_studio_app_lifecycle` — construct StudioApp over an
  `editor::EditorSession`; `app.start()` / `app.stop()` round-trip.
- `test_studio_panel_register_unregister` — register a stub
  `IStudioPanel`; `panelHost.findPanel("stub-id")` returns it;
  unregister; subsequent find returns null.
- `test_studio_layout_persists_via_editor` — register two stub
  panels with distinct ids; save layout via `editor::LayoutManager`;
  restore; both panels recover their dock positions.

**Files**: `studio.hpp`, `src/StudioApp.cpp`, `src/PanelHost.cpp`,
three tests.

**Out of scope**: menu bar, status bar, concrete panels.

### ST3 — `MenuBar` + `StatusBar` + `ConsolePanel` (wrapper)

**Goal**: the chrome around the dockspace. Menu bar with
File/View/Window/Help; status bar with engine FPS, paused
indicator, attach target. `ConsolePanel` is a thin `IStudioPanel`
that renders an `editor::Console` instance (no new console
subsystem).

**Test gate**:
- `test_studio_menu_open_panel` — File→New Panel registers a stub
  panel; View→Close removes it.
- `test_studio_status_bar_text` — paused engine → status bar
  reports "PAUSED"; running engine reports FPS.
- `test_studio_console_panel_renders_editor_console` — `editor::Console`
  has a known log line; `ConsolePanel::render` produces a frame
  whose captured ops contain that line's text.

**Files**: `panels/menu_bar.hpp`, `panels/status_bar.hpp`,
`panels/console.hpp` + impls, three tests.

**Out of scope**: theme presets (folded into ST2), real-stream log
filter UI (post-v1.0 polish).

### ST4 — `DirectDataSource` (Shape A impl)

**Goal**: the in-process `IStudioDataSource` impl that every M2+
panel will consume. Direct engine + sibling pointer reads;
mutations routed through `editor::CommandStack`. Sets the contract
panels build against.

**Test gate**:
- `test_studio_direct_source_engine_snapshot` — Direct source on a
  live engine returns the engine's `FrameSnapshot` with matching
  tick/timing values.
- `test_studio_direct_source_world_snapshot` — returns
  `world().snapshot()` byte-for-byte.
- `test_studio_direct_source_submits_command` — submitCommand →
  command lands in `editor::CommandStack`; engine sees the
  mutation after `engine.step()`.

**Files**: `panels/data_source_impls.hpp` (forward declarations
only; `DirectDataSource` is in src), `src/DirectDataSource.cpp`,
three tests.

**Risks**: `optional<animation::DiagnosticsSnapshot>` etc. return
types — for any sibling NOT linked into this build, return
`std::nullopt`. Panels gracefully degrade.

**Out of scope**: `RemoteDataSource` (M7), per-sibling stat
extraction details (M4–M6 panel batches own those).

## M2 — Engine panels (5 batches; in-process Shape A)

### ST5 — `EntityInspectorPanel`

Wraps editor's E2 `Inspector`. `listEntities()` rendered as a
selectable table.

**Test gate**: `test_studio_entity_inspector_list` — 10-entity
engine → panel render contains 10 rows; clicking a row updates
`editor::Selection`.

**Files**: `panels/engine_inspector.hpp` + `.cpp`, one test.

### ST6 — `PropertyEditorPanel`

Wraps editor's E7 `PropertyEditor` + reflect's `TypeRegistry`.
Edits emit `editor::IEditCommand` instances via
`source.submitCommand()`.

**Test gate**: `test_studio_property_edit_emits_command` — select
entity, edit a Transform field via panel, command lands in
`CommandStack`, undo reverts the edit.

**Files**: `panels/property_editor.hpp` + `.cpp`, one test.

### ST7 — `HierarchyPanel` (**gates on editor E12**)

Tree view of `Parent`-chained entities. Until E12 ships, this batch
cannot start under the locked sequence.

**Test gate**: `test_studio_hierarchy_tree_roots`,
`test_studio_hierarchy_expand`.

**Files**: `panels/hierarchy.hpp` + `.cpp`, two tests.

### ST8 — `GizmoPanel`

Wraps editor's E8 `Gizmo`. Drag emits a `SetTransform` command.

**Test gate**: `test_studio_gizmo_translate_emits_command`.

**Files**: `panels/gizmo.hpp` + `.cpp`, one test.

### ST9 — `WorldDiffPanel`

Wraps editor's E9 `WorldDiff`. Picker for before/after snapshot
slots; renders the diff as a table.

**Test gate**: `test_studio_diff_render_summary`.

**Files**: `panels/world_diff.hpp` + `.cpp`, one test.

## M3 — Telemetry panels (5 batches)

### ST10 — `FrameSnapshotPanel`

Live FPS + frame-time histogram via Engine's existing `HudTraceSink`.

**Test gate**: `test_studio_frame_snapshot_updates`.

### ST11 — `ProfilerPanel` (**gates on editor E13**)

Flamegraph from `ChromeTraceWriter` stream via E13's `ProfilerView`.

**Test gate**: `test_studio_profiler_renders_systems`.

### ST12 — `TaskGraphPanel`

Renders `Engine::taskGraphSnapshot` as a node-edge layout.

**Test gate**: `test_studio_task_graph_layout`.

### ST13 — `TuningPanel`

`ITuningPolicy` status + `TuningTrace` viewer + apply-patch button.

**Test gate**: `test_studio_tuning_patch_apply`.

### ST14 — `ResourcesPanel`

`ResourceRegistry` browser + tracked `AssetReloaded` events.

**Test gate**: `test_studio_resources_list`.

## M4 — Sibling panels: animation, audio, input, assets, UI (5 batches)

### ST15 — `AnimationPanel` (**gates on animation A9**)

Animator state, active clips, blend weights, `PoseValidationReport`.

**Test gate**: `test_studio_animation_panel_lists_clips`.

### ST16 — `AudioPanel` (**gates on audio AU9**)

Bus graph, voice counts, per-bus dBFS meter.

**Test gate**: `test_studio_audio_bus_graph`.

### ST17 — `InputPanel`

Live `InputState` + binding inspector + `InputTrace` record/replay
controls. No sibling prep needed — input v1.0 surfaces sufficient.

**Test gate**: `test_studio_input_panel_records_trace`.

### ST18 — `AssetsPanel` (**gates on assets A9-resident**)

`assets::Registry` browser, refcount sort, reload trigger.

**Test gate**: `test_studio_assets_panel_reload`.

### ST19 — `UIInspectorPanel`

Peek into other `UIContext`s' `DrawList`. UI v1.x `ContextSnapshot`
is deferred (see DESIGN_NOTES §7.4 Tier 4); panel ships *stubbed*
in v1.0 — renders an "available in v1.x" placeholder when no
snapshot accessor is present.

**Test gate**: `test_studio_ui_inspector_draw_count`,
`test_studio_ui_inspector_stub_when_unavailable`.

## M5 — Sibling panels: navmesh, physics, reflect, replay (4 batches)

### ST20 — `NavmeshPanel` (**gates on navmesh N10**)

Query queue depth, BatchPathSolver throughput, obstacle overlay
viewer, path visualizer.

**Test gate**: `test_studio_navmesh_query_stats`.

### ST21 — `PhysicsPanel` (**gates on physics P10**)

Body count, contact stream, step ms, raycast visualizer.

**Test gate**: `test_studio_physics_body_count`.

### ST22 — `ReflectPanel`

`TypeRegistry` browser + field viewer with attributes. No sibling
prep needed — reflect v1.0 surfaces sufficient.

**Test gate**: `test_studio_reflect_panel_list_types`.

### ST23 — `ReplayPanel` (**gates on editor E15**)

`InputTrace` + `TuningTrace` + `WorldSnapshot` replay via E15's
`ReplaySession`.

**Test gate**: `test_studio_replay_loads_snapshot`.

## M6 — Network panel suite (5 batches)

### ST24 — `NetworkSessionPanel`

Connected peers, sequence/ack stats, RTT chart. Uses what network
v1.0 already exposes via `ServerSession` accessors.

**Test gate**: `test_studio_network_peer_list`.

### ST25 — `SnapshotDeltaPanel` (**gates on network NW11**)

Last N snapshots, per-channel bandwidth meter.

**Test gate**: `test_studio_network_bandwidth_chart`.

### ST26 — `InterestPanel`

Per-client AOI viewer (`InterestManager::buildVisibleSet`), entity-
count histogram. No prep needed.

**Test gate**: `test_studio_network_aoi_view`.

### ST27 — `DesyncPanel`

Live `SyncTracker`, commitHash divergence log, per-tick hash
table. No prep needed.

**Test gate**: `test_studio_network_desync_log`.

### ST28 — `PacketTracePanel`

Packet log with filter + decode-by-`PacketType`. No prep needed.

**Test gate**: `test_studio_network_packet_filter`.

## M7 — Out-of-process attach (Shape B) (6 batches; **gates on editor E14**)

The wire format is owned by `threadmaxx_editor` (D2 locked). Every
M7 batch consumes E14.

### ST29 — `StudioAgent` (game-side)

Embeds `network::ServerSession`; receives `IStudioDataSource`
queries over the editor v1.2 remote wire; serializes responses.
Loopback transport for the test rig; UDP for production.

**Test gate**: `test_studio_agent_send_snapshot`.

### ST30 — `RemoteDataSource` (studio-side)

`network::ClientSession`-backed `IStudioDataSource` impl. Cache
layer so panels don't pay a round-trip per accessor. Graceful
degradation when the cache is cold.

**Test gate**: `test_studio_remote_data_source_smoke`.

### ST31 — Mutation tunneling

`editor::IEditCommand` serializes over the editor v1.2 wire,
applies on the agent side via `editor::CommandStack`. Round-trip
test.

**Test gate**: `test_studio_remote_command_roundtrip`.

### ST32 — Auth gate

`StudioAgent` requires a token from the host before exposing
state. Release builds refuse attach unless
`THREADMAXX_STUDIO_AGENT_ENABLE_PROD=1`.

**Test gate**: `test_studio_agent_auth_reject`.

### ST33 — Bandwidth budget panel + interest filter contract

What the studio costs the game per tick; throttle controls. Pinned
contract: remote `IStudioDataSource` MUST opt into
`interest::ClientFocus` like any other peer — no special-cased
full-world reads.

**Test gate**: `test_studio_remote_bandwidth_throttle`,
`test_studio_remote_uses_interest_filter`.

### ST34 — Multi-shard MMORPG attach

List available shard processes (UDP discovery or static list),
pick one to inspect.

**Test gate**: `test_studio_multi_shard_pick`.

## M8 — Save / migration panels (4 batches; **gates on migration v1.0**)

### ST35 — `SaveInspectorPanel`

`WorldSnapshot` viewer + diff against current.

**Test gate**: `test_studio_save_inspector_diff`.

### ST36 — `MigrationStepPanel`

`MigrationPipeline` step-by-step execution viewer.

**Test gate**: `test_studio_migration_step_visualizer`.

### ST37 — `SchemaGraphPanel`

`MigrationRegistry` visualization.

**Test gate**: `test_studio_schema_graph_render`.

### ST38 — `MigrationValidatorPanel`

Runs validator over save corpus, surfaces warnings.

**Test gate**: `test_studio_migration_validator_warnings`.

## M9 — v1.0 close-out (3 batches)

### ST39 — Docs

`README.md`, `USER_GUIDE.md`, `MAINTAINER_GUIDE.md`,
`PANEL_AUTHORING_GUIDE.md`, `CHANGELOG.md`.

### ST40 — End-to-end demo

`examples/studio_demo/main.cpp` — attaches to rpg_demo, drives
every panel for 600 frames, exits 0.

### ST41 — Version stamp + perf gate

`THREADMAXX_STUDIO_VERSION = 10000`, `version_string() = "1.0.0"`.
Bench gate: studio overhead < 1 ms/frame at 1080p with 50 widgets
visible.

## v1.0 close-out criteria

- Every batch ST1–ST41 landed and tested.
- Every panel renders cleanly under both `DirectDataSource` (Shape A)
  and `RemoteDataSource` (Shape B). M7 establishes the second mode.
- End-to-end demo (`examples/studio_demo/`) attaches to a running
  engine and drives every registered panel for 600 frames.
- Docs landed (`README`, `USER_GUIDE`, `MAINTAINER_GUIDE`,
  `PANEL_AUTHORING_GUIDE`, `CHANGELOG`).
- `ctest --test-dir build -R '^studio\.'` 100% on `build/` and
  `build-werror/`.
- Bench `studio_overhead` reports < 1 ms / frame.
- Version stamped at `1.0.0` in
  `include/threadmaxx_studio/version.hpp`.

## v1.x candidate batches (not blocking v1.0)

### v1.x — Cloth panel

Lands when `threadmaxx_cloth` v1.0 ships. Animation already has
trampoline hooks via `cloth.hpp`; the cloth panel reads the real
solver state.

### v1.x — Vehicle panel

Lands when `threadmaxx_vehicle` v1.0 ships (hinted in
`physics/FUTURE_WORK.md`).

### v1.x — Scripting console

Lands when `threadmaxx_script` v1.0 ships (hinted in
`reflect/FUTURE_WORK.md`).

### v1.x — UI inspector (full)

When `threadmaxx_ui` ships `ContextSnapshot`, ST19's stub
`UIInspectorPanel` upgrades to a real cross-context inspector.

### v1.x — Asset browser (rich)

When editor v1.x asset browser ships, ST18's panel can swap to
that. Studio ships its own panel for v1.0.

### v1.x — 3D viewport panel

Game-renderer-in-an-ImGui-window. Out of scope for v1.0 (see §9
of `DESIGN_NOTES.md`).

### v1.x — Diff-as-PR-review

Editor v1.x H lands a Git-PR-style world diff renderer; studio's
WorldDiffPanel gets an upgrade option.

### v1.x — Theme presets + style stack

UI v1.x lands `pushStyle`/`popStyle`; studio panels can expose
theme selection.

### v1.x — Qt or web frontend

Studio's panel layer talks to `editor::IEditorBackend`. A non-ImGui
backend swap is mechanical when there's demand.

## Out of scope for the whole library

Per `DESIGN_NOTES.md` §9 — none of this lands at any version:

- Asset authoring (game/host concern)
- Animation graph editing (separate sibling)
- Save editing offline (migration library territory)
- Network packet replay against live game (anti-cheat risk)
- Renderer ownership
- Sidecar mutation path (every panel either reads or submits an
  `editor::IEditCommand`)
- ImGui as a hard dependency in the studio core (panels go through
  `editor::IEditorBackend`)
- Studio panels pushed into sibling libraries (sibling independence
  is load-bearing)
