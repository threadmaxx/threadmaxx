# `threadmaxx_studio` — Design Notes

> **Status:** Design locked 2026-06-12. No code yet — sequential prep
> path begins with Tier 1 (sibling diagnostics + editor v1.x bridges).
> Studio M1 starts only after Tier 1, Tier 2, and `threadmaxx_migration`
> v1.0 all close out. Companion `FUTURE_WORK.md` will be authored to
> match this sequence when prep work is complete.

> **Locked decisions (from §10 review, 2026-06-12):**
>
> - **D1 — sibling prep order:** *All tiers first (sequential)*.
>   Land Tier 1 (8 batches) + Tier 2 (1 large batch) + Tier 3
>   (`threadmaxx_migration` v1.0, 9 batches) before studio M1.
> - **D2 — wire format owner:** *`threadmaxx_editor`*. The editor's
>   v1.x remote backend defines the canonical wire; studio M7 is a
>   host + cache layer on top.
> - **D3 — M8 scope:** *Migration first, then M8 inside studio v1.0*.
>   Studio v1.0 includes the save/migration panels; migration v1.0
>   is a prerequisite.

## 1. What is `threadmaxx_studio`?

A **panel host + attach environment** for the `threadmaxx_*` suite.
Sharper framing (post-audit, 2026-06-12): studio is *not* the editor,
it is *not* a renderer, and it does not own mutation semantics.
Studio's contribution is exactly two things:

1. **A panel-host shell**: the main window, the `IStudioPanel`
   plugin interface, panel registration / lifecycle / dock
   integration, menu bar, status bar.
2. **An attach environment**: the `IStudioDataSource` interface and
   its two concrete impls — Shape A (in-process) and Shape B
   (out-of-process via `threadmaxx_editor`'s remote backend wire).

Everything else lives in the sibling that already owns the concept:

| Concept | Owner (sibling) | Studio role |
|---|---|---|
| Inspector, PropertyEditor, Gizmo, WorldDiff, HotReloadController, TelemetryOverlay, SelectionState | `threadmaxx_editor` | Wraps these in `IStudioPanel` impls; never re-implements |
| Layout state + serialization | `threadmaxx_editor::layout.hpp` (E10) | Consumes; does NOT define a separate "DockMaster" |
| Console + log routing | `threadmaxx_editor::console.hpp` (E10) | Consumes; does NOT define a separate `ConsoleSink` |
| Edit commands + undo/redo | `threadmaxx_editor::CommandStack` (E3) | Routes panel mutations through it; never adds a sidecar path |
| Remote attach wire format | `threadmaxx_editor` (v1.2 remote backend) | Consumes the wire; does NOT define its own |
| Widgets, draw lists, dock primitives | `threadmaxx_ui` | Consumes via `threadmaxx_editor::ImGuiBackend` |
| Reflection, type browsing | `threadmaxx_reflect` | Consumes for property panels |
| Save format, migration pipeline | `threadmaxx_migration` (v1.0 prerequisite) | Consumes for save panels |
| Per-sibling diagnostics PODs | each sibling's v1.1 `diagnostics.hpp` | Consumes for panels |

If a feature is missing in a sibling, studio's policy is "wait for
the sibling to ship it, don't grow studio". The §7 gap analysis is
the active prerequisite list.

## 2. Use cases (in priority order)

1. **Engine development companion** — same process as the game,
   ImGui overlay rendered into the host's existing swapchain.
   Inspector + property editor + telemetry + gizmo + hot reload +
   command stack. Replaces the ad-hoc `HudTraceSink` overlays the
   demos currently use.
2. **Headless replay debugger** — load a `FrameSnapshot` log +
   `WorldSnapshot` stream, scrub through ticks, see what the engine
   was doing at the divergence point. No engine running.
3. **Out-of-process attach** — separate `threadmaxx_studio` binary
   talks to a running game over `threadmaxx_network`'s transport.
   This is the **MMORPG shard inspector** use case: the studio
   never blocks the simulation, and the agent on the game side is
   tiny.
4. **CI artifact viewer** — chrome-trace JSONs, world snapshots,
   migration reports rendered as panels rather than raw files.

## 3. Architecture

### 3.1 Three-layer cake

```
┌─────────────────────────────────────────────────────────────┐
│  Panel layer  (concrete IStudioPanel impls in              │
│  studio/src/panels/, one per sibling-or-engine concern)    │
├─────────────────────────────────────────────────────────────┤
│  Shell        (StudioApp, PanelHost, MenuBar, StatusBar,   │
│  layer        wraps editor::layout + editor::Console)      │
├─────────────────────────────────────────────────────────────┤
│  Renderer     (ImGui backend reused from threadmaxx_editor │
│  layer        E11 + a host-provided platform driver)       │
└─────────────────────────────────────────────────────────────┘
```

Studio is *not* in the renderer business. It re-uses
`threadmaxx_editor::ImGuiBackend` and asks the host to feed
`ImGui::NewFrame()` / `ImGui::Render()`. The host owns the actual
Vulkan/GL/WebGPU drawing.

### 3.1.1 `IStudioPanel` — the plugin interface

Every panel — engine inspector, animation diagnostics, network
desync, save migration viewer — implements one polymorphic
interface:

```cpp
class IStudioPanel {
public:
    virtual ~IStudioPanel() = default;

    // Stable id; layout serialization uses this as the dock key.
    virtual std::string_view id() const noexcept = 0;
    virtual std::string_view title() const noexcept = 0;

    // Called once per frame while open. Read state via the source,
    // emit mutations via source->submitCommand(...).
    virtual void render(editor::IEditorBackend& backend,
                        IStudioDataSource& source) = 0;

    // Optional: react to attach mode changes (shape A → shape B).
    virtual void onAttachChanged(AttachMode /*newMode*/) {}
};
```

Concrete panels live under `src/threadmaxx_studio/panels/`. Each
panel knows about exactly one sibling's headers; build-time gating
keeps panels out of the binary when the corresponding sibling
isn't linked (same pattern editor uses for its own gated
backends). Panels are NOT pushed into the sibling libraries
themselves — that would violate sibling independence.

### 3.2 Two deployment shapes

**Shape A — in-process companion**
```
Game process
├─ Engine + sibling libs (running)
├─ threadmaxx_studio (library)
└─ ImGui (host-provided)
```
Panels read engine + sibling state directly. Mutation goes through
`threadmaxx_editor`'s command stack. Zero latency.

**Shape B — out-of-process attach**
```
Game process                 Studio process
├─ Engine + siblings         ├─ threadmaxx_studio
└─ StudioAgent (small lib)   └─ threadmaxx_network client
        │                              │
        └──── transport (UDP/loopback) ┘
```
`StudioAgent` is a thin lib the game embeds. The studio process
spawns a `threadmaxx_network::ClientSession`, requests state
snapshots, sends mutation commands. The agent applies them through
the engine's normal commit path. This is the **MMORPG shard
inspector** mode.

The split between the panel layer and the data layer is the same
in both shapes — only the data source changes. Panels consume a
`StudioDataSource` interface; in Shape A the impl is direct
pointers, in Shape B it's the network mirror cache.

### 3.3 `StudioDataSource` — the panel/transport boundary

```cpp
class IStudioDataSource {
public:
    virtual ~IStudioDataSource() = default;

    // Engine — what every telemetry panel needs.
    virtual FrameSnapshot frameSnapshot() const = 0;
    virtual std::vector<TaskGraphNode> taskGraph() const = 0;
    virtual WorldSnapshot worldSnapshot() const = 0;

    // Sibling stat surfaces.
    virtual std::optional<animation::DiagnosticsSnapshot> animationStats() const = 0;
    virtual std::optional<audio::MixerSnapshot> audioStats() const = 0;
    virtual std::optional<navmesh::QueryServiceStats> navmeshStats() const = 0;
    virtual std::optional<physics::SceneStats> physicsStats() const = 0;
    virtual std::optional<network::PeerSummary> networkStats() const = 0;
    virtual std::optional<input::InputState> inputState() const = 0;
    virtual std::optional<assets::RegistryStats> assetStats() const = 0;

    // Mutation — funnels through editor command stack in Shape A
    // or serializes over network in Shape B.
    virtual void submitCommand(std::unique_ptr<editor::IEditCommand>) = 0;
};
```

The `std::optional` returns let the studio panels gracefully
degrade when the host process doesn't link a given sibling.

### 3.4 No new types if a sibling already owns the concept

Studio does NOT redefine `FrameSnapshot`, `PoseValidationReport`,
`MixerStats`, `PathRequest`, etc. The studio is a consumer; siblings
are the authority. The studio's only original types are the panel
state (open/closed, dock position, filter strings) and the
`IStudioDataSource` interface.

## 4. Sibling consumption matrix

What every studio panel reads from where. **Bold** = surface not yet
shipped at sibling v1.0; see §7 for the gap analysis.

| Panel cluster | Engine `threadmaxx` | editor | reflect | ui | assets | input | animation | audio | navmesh | physics | network |
|---|---|---|---|---|---|---|---|---|---|---|---|
| Engine inspector | `World::snapshot`, `Engine::stats`, `Inspector` | E2 Inspector | — | widgets | — | — | — | — | — | — | — |
| Property editor | mut via cb | E7 PropertyEditor | TypeRegistry browser | widgets | — | — | — | — | — | — | — |
| Hierarchy tree | `Parent` component | **editor v1.x scene-hierarchy** | — | tree widget | — | — | — | — | — | — | — |
| Gizmo | `Transform` mut | E8 Gizmo | — | 2D handle prim | — | input picking | — | — | — | — | — |
| World diff | `WorldSnapshot` | E9 WorldDiff | — | widgets | — | — | — | — | — | — | — |
| Telemetry overlay | `FrameSnapshot` | E5 TelemetryOverlay | — | debug HUD | — | — | — | — | — | — | — |
| Live profiler | ChromeTrace stream | **editor v1.x live profiler** | — | widgets | — | — | — | — | — | — | — |
| Task graph | `taskGraphSnapshot` | — | — | widgets | — | — | — | — | — | — | — |
| Tuning policy | `TuningTrace` | — | — | widgets | — | — | — | — | — | — | — |
| Resource registry | `ResourceRegistry` | E2 Inspector | — | widgets | — | — | — | — | — | — | — |
| Animation panel | — | — | bridge | widgets | — | — | `Animator`, `PoseValidationReport`, **animation v1.x `AnimatorStats`** | — | — | — | — |
| Audio mixer | — | — | bridge | widgets | — | — | — | `AudioDiagnostics`, **audio v1.x `MixerSnapshot`** | — | — | — |
| Navmesh viewer | — | — | — | widgets | — | — | — | — | `NavMesh::meta`, **navmesh v1.x `QueryServiceStats`** | — | — |
| Physics panel | — | — | — | widgets | — | — | — | — | — | `BodyState`, **physics v1.x `SceneStats`** | — |
| Network panel | `commitHash` | — | — | widgets | — | — | — | — | — | — | `DesyncReport`, `SyncTracker`, **network v1.x `PeerSummary`** |
| Input panel | — | — | — | widgets | — | `InputState`, `InputTrace` | — | — | — | — | — |
| Assets browser | — | — | — | widgets, tree | `Registry::Stats`, `AssetReloaded` | — | — | — | — | — | — |
| Reflect browser | — | — | `TypeRegistry`, `FieldInfo` | tree, widgets | — | — | — | — | — | — | — |
| UI inspector | — | — | — | self-introspect `UIContext`, `DrawList`, **ui v1.x `ContextSnapshot`** | — | — | — | — | — | — | — |
| Save / migration | `WorldSnapshot` | — | — | widgets | — | — | — | — | — | — | — |
| ↑ that panel also needs | **threadmaxx_migration v1.0** (not started) | — | — | — | — | — | — | — | — | — | — |

## 5. Determinism + commit-path discipline

Studio mutations route through `threadmaxx_editor::IEditCommand`,
which already uses the engine's `CommandBuffer`. This means:

- Mutations from the studio are commit-ordered just like any system
  write.
- Undo/redo works because the editor's CommandStack records every
  command.
- The engine's `commitHash` stays correct → desync detection in
  attach mode still works.

The studio MUST NOT add a sidecar mutation path. Every studio panel
either reads or submits a command — no third option.

## 6. Milestone + batch plan (provisional)

Nine milestones, ~40 batches. Order matters: M2/M3 establish the
panel framework; M4–M6 build per-sibling clusters; M7 adds remote
attach; M8 covers save/migration once the migration lib lands;
M9 closes out.

### M1 — Studio shell + panel framework (4 batches; post-audit, 1 dropped)

Dropped from the original draft (per audit #4): ST3 `DockMaster`
(editor `layout.hpp` already owns dock persistence) and ST5
`ConsolePanel` as a standalone batch (folded into ST3 as a
*panel that wraps* `editor::Console`, not a new console
subsystem).

| Batch | Goal | Test gate |
|---|---|---|
| ST1 | Library scaffold: CMake, `version.hpp` (`0.1.0-dev`), umbrella, `IStudioPanel` interface, `IStudioDataSource` interface, `AttachMode` enum, no-engine-link canary, opt-in `THREADMAXX_BUILD_STUDIO=OFF` | `test_studio_version`, `test_studio_panel_interface_canary`, `test_studio_data_source_canary` |
| ST2 | `StudioApp` + `PanelHost` over editor's `ImGuiBackend`. Hosts an `editor::LayoutManager` (E10) for dock state; `register/unregister/findPanel` lifecycle | `test_studio_app_lifecycle`, `test_studio_panel_register_unregister`, `test_studio_layout_persists_via_editor` |
| ST3 | `MenuBar` (File/View/Window/Help) + `StatusBar` (sim FPS, paused, attach target) + `ConsolePanel` (thin `IStudioPanel` wrapper over `editor::Console`) | `test_studio_menu_open_panel`, `test_studio_status_bar_text`, `test_studio_console_panel_renders_editor_console` |
| ST4 | `DirectDataSource` impl of `IStudioDataSource` (Shape A). Direct engine/sibling pointer reads; mutations routed through `editor::CommandStack`. Sets up the contract every subsequent panel consumes | `test_studio_direct_source_engine_snapshot`, `test_studio_direct_source_submits_command` |

### M2 — Engine panels (5 batches; in-process Shape A)

Renumbered to ST5–ST9 after M1 dropped a batch.

| Batch | Goal | Test gate |
|---|---|---|
| ST5 | `EntityInspectorPanel` — wraps editor's E2 `Inspector` | `test_studio_entity_inspector_list` |
| ST6 | `PropertyEditorPanel` — wraps editor's E7 + reflect's `TypeRegistry` | `test_studio_property_edit_emits_command` |
| ST7 | `HierarchyPanel` — tree view of `Parent`-chained entities. **Blocked on editor v1.x scene-hierarchy** (see §7) | `test_studio_hierarchy_tree_roots`, `test_studio_hierarchy_expand` |
| ST8 | `GizmoPanel` — wraps editor's E8 Gizmo, dispatches via UI's screen-space drag | `test_studio_gizmo_translate_emits_command` |
| ST9 | `WorldDiffPanel` — wraps editor's E9 `WorldDiff`; before/after snapshot picker | `test_studio_diff_render_summary` |

### M3 — Telemetry panels (5 batches)

Renumbered to ST10–ST14.

| Batch | Goal | Test gate |
|---|---|---|
| ST10 | `FrameSnapshotPanel` — live FPS + frame-time histogram via `HudTraceSink` (already exists) | `test_studio_frame_snapshot_updates` |
| ST11 | `ProfilerPanel` — flamegraph from `ChromeTraceWriter` stream. **Blocked on editor E13 live profiler view** | `test_studio_profiler_renders_systems` |
| ST12 | `TaskGraphPanel` — Graphviz-ish layout of `Engine::taskGraphSnapshot` | `test_studio_task_graph_layout` |
| ST13 | `TuningPanel` — `ITuningPolicy` status + `TuningTrace` viewer + apply-patch button | `test_studio_tuning_patch_apply` |
| ST14 | `ResourcesPanel` — `ResourceRegistry` browser + tracked `AssetReloaded` events | `test_studio_resources_list` |

### M4 — Sibling panels: animation, audio, input, assets, UI (5 batches)

Renumbered to ST15–ST19.

| Batch | Goal | Test gate |
|---|---|---|
| ST15 | `AnimationPanel` — Animator state, active clips, blend weights, `PoseValidationReport`. **Needs animation A9 `AnimatorStats`** | `test_studio_animation_panel_lists_clips` |
| ST16 | `AudioPanel` — bus graph + voice counts + per-bus meter. **Needs audio AU9 `MixerSnapshot`** | `test_studio_audio_bus_graph` |
| ST17 | `InputPanel` — live `InputState` + binding inspector + `InputTrace` record/replay controls | `test_studio_input_panel_records_trace` |
| ST18 | `AssetsPanel` — `assets::Registry` browser, refcount sort, reload trigger. **Needs assets A9 resident enumerate** | `test_studio_assets_panel_reload` |
| ST19 | `UIInspectorPanel` — peek into other `UIContext`s' `DrawList`. **Needs ui v1.x `ContextSnapshot` (deferred — panel ships stubbed)** | `test_studio_ui_inspector_draw_count` |

### M5 — Sibling panels: navmesh, physics, reflect, replay (4 batches)

Renumbered to ST20–ST23.

| Batch | Goal | Test gate |
|---|---|---|
| ST20 | `NavmeshPanel` — query queue depth, BatchPathSolver throughput, obstacle overlay viewer, path visualizer. **Needs navmesh N10 `diagnostics.hpp`** | `test_studio_navmesh_query_stats` |
| ST21 | `PhysicsPanel` — body count, contact stream, step ms, raycast visualizer. **Needs physics P10 `diagnostics.hpp`** | `test_studio_physics_body_count` |
| ST22 | `ReflectPanel` — `TypeRegistry` browser + field viewer with attributes | `test_studio_reflect_panel_list_types` |
| ST23 | `ReplayPanel` — `InputTrace` + `TuningTrace` + `WorldSnapshot` replay. **Needs editor E15 capture/replay** | `test_studio_replay_loads_snapshot` |

### M6 — Network panel suite (5 batches; largest single sibling cluster)

Renumbered to ST24–ST28.

| Batch | Goal | Test gate |
|---|---|---|
| ST24 | `NetworkSessionPanel` — connected peers, sequence/ack stats, RTT chart | `test_studio_network_peer_list` |
| ST25 | `SnapshotDeltaPanel` — last N snapshots, per-channel bandwidth meter. **Needs network NW11 `PeerSummary`** | `test_studio_network_bandwidth_chart` |
| ST26 | `InterestPanel` — per-client AOI viewer (visible set, interest radius), entity-count histogram | `test_studio_network_aoi_view` |
| ST27 | `DesyncPanel` — live `SyncTracker`, commitHash divergence log, per-tick hash table | `test_studio_network_desync_log` |
| ST28 | `PacketTracePanel` — packet log with filter + decode-by-`PacketType` | `test_studio_network_packet_filter` |

### M7 — Out-of-process attach (Shape B) (6 batches; was 7 — interface batch folded into M1 ST1)

Renumbered to ST29–ST34. Drop note: the old "ST30 — IStudioDataSource
interface" batch is gone; that interface lands in M1 ST1 so every
preceding panel batch (M2–M6) can already consume it. The `Direct`
impl ships in M1 ST4. M7 now starts with the *Shape B* impl.

| Batch | Goal | Test gate |
|---|---|---|
| ST29 | `StudioAgent` library (game-side) — embeds `network::ServerSession` (loopback first), serializes `IStudioDataSource` queries onto `editor` v1.2 remote wire. **Needs editor E14 remote backend** | `test_studio_agent_send_snapshot` |
| ST30 | `RemoteDataSource` (studio-side) — `network::ClientSession`, cache layer, gracefully-degrading reads | `test_studio_remote_data_source_smoke` |
| ST31 | Mutation tunneling: editor commands serialize over the editor v1.2 wire, apply via `StudioAgent` | `test_studio_remote_command_roundtrip` |
| ST32 | Auth gate — `StudioAgent` requires a token from the host before exposing state; refuses attach in release builds without `THREADMAXX_STUDIO_AGENT_ENABLE_PROD=1` | `test_studio_agent_auth_reject` |
| ST33 | Bandwidth budget panel — what the studio costs the game per tick; throttle controls; remote `IStudioDataSource` MUST opt into `interest::ClientFocus` (no infinite-AOI shortcut) | `test_studio_remote_bandwidth_throttle`, `test_studio_remote_uses_interest_filter` |
| ST34 | Multi-shard MMORPG attach — list available shard processes, pick one to inspect; UDP-or-loopback transport selection | `test_studio_multi_shard_pick` |

### M8 — Save / migration panel (4 batches; **BLOCKED on threadmaxx_migration v1.0**)

Renumbered to ST35–ST38.

| Batch | Goal | Test gate |
|---|---|---|
| ST35 | `SaveInspectorPanel` — `WorldSnapshot` viewer + diff against current | `test_studio_save_inspector_diff` |
| ST36 | `MigrationStepPanel` — `MigrationPipeline` step-by-step execution viewer | `test_studio_migration_step_visualizer` |
| ST37 | `SchemaGraphPanel` — registry visualization (Graphviz-style or simple tree) | `test_studio_schema_graph_render` |
| ST38 | `MigrationValidatorPanel` — runs validator over save corpus, surfaces warnings | `test_studio_migration_validator_warnings` |

### M9 — v1.0 close-out (3 batches)

Renumbered to ST39–ST41.

| Batch | Goal | Test gate |
|---|---|---|
| ST39 | Docs (README, USER_GUIDE, MAINTAINER_GUIDE, CHANGELOG, PANEL_AUTHORING_GUIDE) | doc presence |
| ST40 | End-to-end demo `examples/studio_demo/` — attaches to rpg_demo, drives every panel | smoke run 600 frames |
| ST41 | Version bump to `1.0.0`, soak + perf gate (studio overhead < 1 ms/frame at 1080p, 50 widgets visible) | `test_studio_v1_close_out`, `bench/studio_overhead` |

### Total

After audit: **41 studio batches** (was 43; M1 dropped 1, M7 dropped 1).

## 7. Sibling-gap analysis (the section you asked for)

What MUST land in siblings before the studio can ship cleanly. If
any of these slips to after a studio panel is built, the panel
needs to be rewritten against the new sibling API — that's the
"rework" cost the original question called out.

### 7.1 Surfaces that don't exist yet (would force panel rewrites)

| # | Sibling | Missing surface | Studio batch impacted | Severity |
|---|---|---|---|---|
| G1 | `threadmaxx_animation` | `AnimatorStats` POD: active clip count, blend weights snapshot, per-graph state. Today the only diagnostics surface is `PoseValidationReport` (pose-correctness, not runtime stats) | ST15 | High — panel useless without it |
| G2 | `threadmaxx_audio` | Promote `AudioDiagnostics` from class to a documented `MixerSnapshot` POD with: bus graph nodes, voice slot occupancy, per-bus dBFS meter, listener count | ST16 | High |
| G3 | `threadmaxx_navmesh` | Brand-new `diagnostics.hpp`: `QueryServiceStats` (pending + storedCount + cancellations), `BatchPathSolverStats` (qps), `MeshMeta` already partially there | ST20 | High |
| G4 | `threadmaxx_physics` | Brand-new `diagnostics.hpp`: `SceneStats` (body counts by type, constraint count, contact events/tick, step ms breakdown), `BackendStats` (which backend, Jolt-only fields) | ST21 | High |
| G5 | `threadmaxx_network` | `PeerSummary` POD per `PeerId`: bytes-in/out/tick, ack window saturation, last RTT, last-seen tick. Some of this exists inside `ServerSession`; needs to be public | ST25 | Medium — studio could approximate from packet trace |
| G6 | `threadmaxx_ui` | `ContextSnapshot` — peek into another `UIContext`'s widget tree + draw list bytes. Used to inspect OTHER UI contexts (game HUD) from the studio | ST19 | Medium — only blocks the UI inspector panel (ships stubbed) |
| G7 | `threadmaxx_assets` | Surface `Registry::Stats` and the resident asset list ergonomically. `A6` shipped `Stats` counters; needs an "enumerate residents" call | ST18 | Low — Stats already exists |

### 7.2 Editor v1.x items that block studio panels (severity ranked)

The editor explicitly listed these as v1.x in its `FUTURE_WORK.md`.
If they ship AFTER the studio's panels are built, every panel
that depended on a stub workaround needs rewriting.

| # | Editor v1.x item | Studio batch impacted | Rework cost if deferred |
|---|---|---|---|
| E-v1.x-A | **Scene hierarchy / entity tree view** (`hierarchy.hpp`) — editor E12 | ST7 | HIGH — studio would build a placeholder tree panel that gets replaced |
| E-v1.x-B | **Live profiler view** (flamegraph) — editor E13 | ST11 | HIGH — studio would ship a Chrome-trace-JSON viewer, then swap to the real profiler |
| E-v1.x-C | **Capture / replay mode** (DESIGN_NOTES §6.3) — editor E15 | ST23 | HIGH — replay panel built against ad-hoc snapshot reader needs rewiring |
| E-v1.x-D | **Remote backend** (editor-as-server) — editor E14 | ST29–ST34 (all of M7) | CRITICAL — M7 IS this feature. Per D2 (locked), editor owns the wire; studio M7 becomes a thin host + cache layer over it |
| E-v1.x-E | Asset browser | ST18 | LOW — independent surface; studio can build its own |
| E-v1.x-F | Property editor: deep struct introspection | ST6 | LOW — studio's panel stops at depth 1 like editor v1.0 does |
| E-v1.x-G | Transaction groups | none directly | LOW — CommandStack works either way |
| E-v1.x-H | Diff-as-PR-review | ST9 | LOW — ST9 builds against E9's flat diff; PR view is an additive renderer |

### 7.3 Missing entire sibling libraries

| # | Missing sibling | Studio impact | Severity |
|---|---|---|---|
| L1 | **`threadmaxx_migration`** — design notes only, zero batches landed | M8 entirely (ST37–ST40) blocked | HIGH — M8 cannot ship at all; either land migration v1.0 first OR drop M8 from studio v1.0 |
| L2 | `threadmaxx_cloth` — design notes only; animation has `cloth.hpp` trampoline hooks | Future studio v1.x cloth panel | LOW — not in studio v1.0 scope |
| L3 | `threadmaxx_vehicle` — hinted in `physics/FUTURE_WORK.md` | Future studio v1.x vehicle panel | LOW |
| L4 | `threadmaxx_script` — hinted in `reflect/FUTURE_WORK.md` | Future studio v1.x scripting console | LOW |

### 7.4 Recommended sibling-prep ordering

**Tier 1 — must ship before studio M4–M6 (panel data sources):**
- A. Diagnostics surfaces: G1 + G2 + G3 + G4 + G5 + G7 (5 small per-sib
  batches: each library gets a single "v1.1 diagnostics" batch that
  adds the POD + accessor; no breaking ABI change).
- B. Editor v1.x scene-hierarchy (E-v1.x-A) — needed for ST8.
- C. Editor v1.x live profiler (E-v1.x-B) — needed for ST12.

**Tier 2 — must ship before studio M7 (remote attach):**
- D. Editor v1.x remote backend (E-v1.x-D) — needed for ST30+.
  Decide whether the wire format is owned by editor or studio.
  Recommendation: editor owns the wire (it's the one that
  enumerates the panel surface) and studio is the host.

**Tier 3 — must ship before studio M8 (save/migration):**
- E. `threadmaxx_migration` v1.0 (8 batches + close-out, per its
  existing `FUTURE_WORK.md`).

**Tier 4 — optional / can ship after studio v1.0:**
- F. UI v1.x `ContextSnapshot` (G6) — defer; studio's UI inspector
  ships as a stub if not present.
- G. Editor v1.x asset browser (E-v1.x-E), deep struct (E-v1.x-F),
  diff-as-PR-review (E-v1.x-H) — defer to studio v1.x.
- H. `threadmaxx_cloth`, `threadmaxx_vehicle`, `threadmaxx_script`
  — future studio v1.x.

### 7.5 Estimated sibling-prep size

| Tier | Batches | Notes |
|---|---|---|
| Tier 1 | 6 sibling diagnostics + 2 editor v1.x = **8** | Mostly small additive surfaces. ~1–2 days each. |
| Tier 2 | 1 editor v1.x remote backend = **1 (large)** | The transport choice is the big design decision here; rest is wiring. |
| Tier 3 | migration v1.0 = **9** | The full M1–M8 + close-out batch plan already exists in `include/threadmaxx_migration/FUTURE_WORK.md` |
| **Subtotal** | **~18 batches before studio M4** | Studio M1–M3 (shell + engine + telemetry panels) can run in PARALLEL with Tier 1 since they only consume engine + editor v1.0 surfaces already shipped |

## 8. Risk + tradeoff register

| # | Risk | Mitigation |
|---|---|---|
| R1 | ImGui pinning — studio depends on the same ImGui version editor E11 fetched (`v1.91.5`). Bumping ImGui ripples through every panel | Lock the version at studio v1.0; bump only in v1.x with a regression test |
| R2 | Out-of-process protocol drift — studio + agent must agree on wire format. If editor's remote backend defines the wire (Tier 2), studio inherits the contract | Make the editor's wire format the canonical one; studio is a consumer |
| R3 | Panel overhead in-process — 40 panels × 50 widgets at 60Hz costs real ms. UI library's bench gate is 0.211 ms/512 widgets/8 panels — extrapolates to ~2 ms for the full studio. Acceptable for dev, not for prod | Studio MUST be opt-in. Default-OFF CMake flag. Production builds compile it out |
| R4 | Auth — Shape B attach over the network exposes engine state. A malicious peer could mutate world state | Token-based auth (ST34); refuse attach in release builds unless `THREADMAXX_STUDIO_AGENT_ENABLE_PROD=1` |
| R5 | Save/migration coupling — M8 is the heaviest sibling dep (entire library missing). If migration slips, M8 doesn't ship | M8 is the last functional milestone for a reason. Studio v1.0 can ship as M1–M7 + M9 if migration isn't ready |
| R6 | MMORPG-scale shard inspection — the network's `InterestManager` is per-client; the studio is "client zero" with infinite AOI. That could pump huge bandwidth | Studio's remote data source MUST opt into `interest::ClientFocus` like any other peer — no special-cased full-world reads |
| R7 | ImGui-or-not: some hosts want a different UI (Qt for desktop tooling). Today studio is ImGui-only | Studio's panel layer talks to editor's `IEditorBackend`, not ImGui directly. Swap backends per editor's BACKEND_PORTING_GUIDE.md when needed. Out of scope for v1.0 |
| R8 | Cross-platform: editor's ImGui backend is Linux-tested. Studio on macOS/Windows needs the host platform driver (which is host-provided, not studio's job) | Document the host requirements; ship Linux-only test gates for v1.0 |

## 9. Out of scope for v1.0

- Scripting console (Lua / Python) — depends on `threadmaxx_script`.
- 3D world preview in the studio (game-renderer-in-an-ImGui-window).
  The studio reads + diagnoses; it doesn't render the game scene.
- Asset *authoring* (mesh edit, texture paint, BMFont generator) —
  studio inspects existing assets via `threadmaxx_assets`; creation
  pipeline is separate.
- Animation graph *editing* — studio shows the graph state; building
  graphs visually is a separate, much-larger sibling (could be
  `threadmaxx_animgraph_editor` later).
- Save *editing* — Shape B's mutation path covers live game; offline
  save editing is migration-library territory.
- Network *packet replay against a live game* — too risky (could
  trigger anti-cheat or desync). Studio reads packet traces; it does
  not inject.

## 10. Decisions (D1/D2/D3) — answered 2026-06-12

D1, D2, D3 were resolved in the review pass that closed §10. The
decision text and recommendations are preserved in `CLAUDE_ARCHIVE.md`
discussion history; for active use, §11 below is the authoritative
locked sequence.

---

## 11. Locked prep sequence (per §10 decisions, 2026-06-12)

Per D1 (sequential, all tiers first), D2 (editor owns the wire), and
D3 (migration v1.0 lands before studio M8 inside studio v1.0), the
total path from here to studio v1.0 is:

### Phase 1 — sibling diagnostics surfaces (Tier 1A, 6 batches)

Each library gets a single "v1.1 diagnostics" batch that adds the
documented snapshot POD + accessor. Minor version bumps; no breaking
ABI changes. Order is by dependency depth: smaller / leaner libraries
first so the studio's eventual data source has the simpler surfaces
to wrap.

| # | Library | Batch label | Adds |
|---|---|---|---|
| T1A.1 | `threadmaxx_animation` | A9 — Diagnostics | `AnimatorStats` POD, `Animator::stats() const`, blend-weight + clip-active snapshot |
| T1A.2 | `threadmaxx_audio` | AU9 — Diagnostics | `MixerSnapshot` POD (was `AudioDiagnostics` class), bus graph + voice slots + per-bus meter |
| T1A.3 | `threadmaxx_navmesh` | N10 — Diagnostics | `diagnostics.hpp`: `QueryServiceStats`, `BatchPathSolverStats` |
| T1A.4 | `threadmaxx_physics` | P10 — Diagnostics | `diagnostics.hpp`: `SceneStats`, `BackendStats` |
| T1A.5 | `threadmaxx_network` | NW11 — Diagnostics | `PeerSummary` POD per `PeerId`: bytes/tick + ack window + RTT + last-seen |
| T1A.6 | `threadmaxx_assets` | A9 — Resident enumerate | `Registry::enumerateResidents()` + extended `Stats` |

(`threadmaxx_ui` `ContextSnapshot` deferred per §7.4 — only blocks the
UI inspector panel, can ship in studio v1.x.)

### Phase 2 — editor bridge batches (Tier 1B, 2 batches)

| # | Library | Batch label | Adds |
|---|---|---|---|
| T1B.1 | `threadmaxx_editor` | E12 — Scene hierarchy tree | `hierarchy.hpp` with tree-of-`Parent` model + `HierarchyView` accessor (unblocks studio ST8) |
| T1B.2 | `threadmaxx_editor` | E13 — Live profiler view | `ProfilerView` wrapping `ChromeTraceWriter` data into a flamegraph-ready model (unblocks studio ST12) |

### Phase 3 — editor remote backend (Tier 2, 1 large batch + close-out)

| # | Library | Batch label | Adds |
|---|---|---|---|
| T2.1 | `threadmaxx_editor` | E14 — Remote backend + wire format | `IEditorBackend` impl over `threadmaxx_network::ITransport`; wire format spec (`include/threadmaxx_editor/REMOTE_WIRE_FORMAT.md`); editor v1.2 close-out |

This batch is the single biggest in the prep series. It defines the
contract the studio's M7 will consume; getting this right is the
load-bearing prep decision.

### Phase 4 — editor capture/replay (Tier 1C, 1 batch)

| # | Library | Batch label | Adds |
|---|---|---|---|
| T1C.1 | `threadmaxx_editor` | E15 — Capture/replay mode | `ReplaySession` reading `WorldSnapshot` stream; same Inspector API works on it (unblocks studio ST24) |

### Phase 5 — `threadmaxx_migration` v1.0 (Tier 3, 9 batches)

Picks up the existing `include/threadmaxx_migration/FUTURE_WORK.md`
plan verbatim:

| # | Batch | Goal |
|---|---|---|
| T3.1 | M1 | Foundations (versioning + records) |
| T3.2 | M2 | Save metadata + commitHash anchoring |
| T3.3 | M3 | MigrationRegistry + type aliases |
| T3.4 | M4 | Field rename / remap helpers |
| T3.5 | M5 | MigrationPipeline |
| T3.6 | M6 | WorldSnapshot adapter |
| T3.7 | M7 | Component codec bridge |
| T3.8 | M8 | Validation + reports + offline tool |
| T3.9 | v1.0 close-out | docs, version stamp, end-to-end fixture |

### Phase 6 — `threadmaxx_studio` v1.0 (M1–M9, 41 batches post-audit)

Picks up §6's batch plan with M8 in scope.

### Total scope

| Phase | Batches |
|---|---|
| 1 — sibling diagnostics | 6 |
| 2 — editor scene-hierarchy + profiler | 2 |
| 3 — editor remote backend (E14) | 1 (large) |
| 4 — editor capture/replay (E15) | 1 |
| 5 — migration v1.0 | 9 |
| 6 — studio v1.0 (M1–M9, post-audit) | 41 |
| **Total** | **60 batches** to studio v1.0 |

Each batch ships under the standing per-batch auto-commit
authorization.

## 12. Audit retrospective (2026-06-12, post-§11 draft)

A second-opinion review of the draft (sections §§1–11) raised seven
critiques; this section records which landed in the doc.

### Accepted (now reflected in the doc)

| # | Critique | Where the fix landed |
|---|---|---|
| A1 | "Studio framing leaks into 'second editor' territory" | §1 rewritten as "panel host + attach env"; per-concept ownership table makes editor's territory explicit |
| A2 | "Panels should be a polymorphic plugin interface" | §3.1.1 added: `IStudioPanel` interface. Concrete panels live in studio's tree (not pushed into siblings — that would violate sibling independence; project soul evidence: every sibling library is self-contained) |
| A4 | "Reduce duplication with editor (layout, console, command stack)" | M1 dropped a batch: ST3 `DockMaster` is gone (studio consumes `editor::LayoutManager`); ST5 `ConsolePanel` is now a thin `IStudioPanel` wrapper of `editor::Console`, not a new console subsystem |
| A5 | "Make wire-format ownership more explicit" | §1 ownership table now names the editor as the wire owner in row 5; §6 M7's drop note ties it explicitly to E14 |
| A7 | "Better build-order doc" | §6 added per-milestone renumbering notes; §12.3 below records the first-paint dependency chain |

### Partially accepted

| # | Critique | What was kept, what was dropped |
|---|---|---|
| A3 | "Trim initial scope — M1–M3 is enough" | **Kept the full M1–M9 v1.0 scope.** The user's ask (`continue with the threadmaxx_network sibling library, do all the batches one after another and finally the close out batch for v1` was the pattern they keep using; same for this library) plus the project soul (every sibling shipped v1.0 complete in one close-out, not MVP-then-grow) outweigh "ship a smaller v1.0". **Took the build-order half:** §6 now annotates which batches are independently shippable vs which gate on prep |

### Declined

| # | Critique | Why |
|---|---|---|
| A6 | "Rewrite Decision Request as a governance section" | Moot — D1/D2/D3 already answered in the §10 review; the locked outcomes in §11 are the working contract |

### 12.1 New observation surfaced by the cleaner framing

Under the panel-host framing (audit A1+A2), **studio M1–M3 has zero
sibling-prep deps**:

- M1 (ST1–ST4): scaffold + panel host + menu/status + DirectDataSource.
  Consumes only engine `0.1.0+`, editor v1.0 (E1–E10 already shipped),
  ui v1.0 (already shipped). No prep work needed.
- M2 (ST5–ST9): engine panels wrapping editor E2/E7/E8/E9. Only ST7
  (hierarchy) blocks on editor E12; the other four can ship today.
- M3 (ST10–ST14): telemetry panels. Only ST11 (profiler) blocks on
  editor E13; the other four can ship today.

This was not visible under the original draft because the draft had
M1 owning `DockMaster` + `ConsoleSink` (now folded into editor's
existing surface). After audit A4, M1–M3 cleanly precedes Tier 1.

### 12.2 Open question — strict vs spirit reading of D1

D1 was answered "all tiers first (sequential)" under the original
framing. Under the cleaner framing, two readings are possible:

- **Strict reading** — no studio code until Phase 5 closes. 19 prep
  batches before first studio paint. Respects the literal answer to
  D1. Total time-to-first-studio-batch: longest.
- **Spirit reading** — sequential *where deps demand it*. Studio
  M1–M3 starts in parallel with Tier 1; sibling panels (M4–M6)
  unblock as each sibling's diagnostics batch lands; M7 gates on
  editor E14; M8 gates on migration v1.0. Same total ordering of
  blockable work, faster first paint, no extra rework risk
  (M1–M3 only consume already-shipped v1.0 APIs).

The author's recommendation: **spirit reading**. Argument: the
locked D1 was made under an inflated framing of what studio M1–M3
required. The audit revealed those batches are deps-free; gating
them sequentially behind ~19 prep batches buys nothing and
deferred no risk.

The user owns this call — see §12.4.

### 12.3 First-paint dependency chain (per-batch gate map)

For the spirit reading, the actual gating chain looks like:

```
                Phase 1 sib diagnostics       Phase 2 editor E12+E13     Phase 3 E14    Phase 4 E15    Phase 5 migration v1.0
                A9   AU9  N10  P10  NW11 A9'   E12              E13       E14            E15            (M1-M8 + close-out)
                 │    │    │    │    │   │     │                │         │              │               │
                 ▼    ▼    ▼    ▼    ▼   ▼     ▼                ▼         ▼              ▼               ▼
Studio M1-M3 ───┤                                                                                                  unblocks panel data sources
                │
Studio M4 ──────┴── ST15(A9) ─ ST16(AU9) ─ ST17 ─ ST18(A9') ─ ST19 (stubbed for ui ContextSnapshot)
Studio M5 ──────────────────── ST20(N10) ─ ST21(P10) ─ ST22 ─ ST23(E15)
Studio M6 ──────────────────────────────── ST24 ─── ST25(NW11) ─ ST26 ─ ST27 ─ ST28
Studio M7 ──────────────────────────────────────────────────────────── ST29(E14) ─ ST30 ─ ST31 ─ ST32 ─ ST33 ─ ST34
Studio M8 ────────────────────────────────────────────────────────────────────────────────────────── ST35(migr) ─ ST36 ─ ST37 ─ ST38
Studio M9 ──────────────────────────────────────────────────────────────────────────────────────────────────────────────── ST39 ─ ST40 ─ ST41
                                                                                                                            (after every other batch is green)
```

Each "ST*(X)" notation means "studio batch ST*N* gates on sibling
batch *X* having landed".

### 12.4 D4 — answered 2026-06-12: **spirit reading**

Studio M1–M3 begins in parallel with Phase 1 (sibling diagnostics +
editor E12/E13). Each later studio milestone gates on its specific
prep batch per the chain in §12.3. Same total ordering of risky
work as strict; faster first paint; no rework risk since M1–M3
only consumes already-shipped v1.0 APIs.

**Operational contract under spirit reading:**

- Studio ST1 can start any time after this design lock.
- Studio ST5–ST9 (M2 engine panels) can start after ST1–ST4 ship,
  with the exception of ST7 (hierarchy) which gates on editor E12.
- Studio ST10–ST14 (M3 telemetry) can start after M2 progress, with
  the exception of ST11 (profiler) which gates on editor E13.
- M4+ batches gate on the §12.3 chain.
- Either lane (studio or prep) can pause without blocking the other.
