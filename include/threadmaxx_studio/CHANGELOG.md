# Changelog — `threadmaxx_studio`

All notable changes to this sibling library. Format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) loosely.
Older entries are immutable; new releases append.

## [1.0.0] — 2026-06-13

First production release. Every batch ST1–ST41 landed with headless
tests; 49 tests green on `build/` and `build-werror/`.

### Added (M1 — Studio shell)
- `IStudioPanel`, `IStudioDataSource`, `AttachMode`
- `StudioApp` + `PanelHost` over `editor::EditorSession`
- `MenuBar`, `StatusBar`, `ConsolePanel`
- `DirectDataSource` (Shape A)

### Added (M2 — Engine-only panels)
- `EntityInspectorPanel`
- `PropertyEditorPanel` (reflect-backed)
- `GizmoPanel`
- `WorldDiffPanel`
- `FrameSnapshotPanel`

### Added (M3 — Engine telemetry)
- `TaskGraphPanel`
- `TuningPanel`
- `ResourcesPanel`
- `HierarchyPanel`
- `ProfilerPanel`
- `ReplayPanel`

### Added (M4 — Per-sibling panels)
- `AnimationPanel` (gated on `threadmaxx::animation`)
- `AudioPanel` (gated on `threadmaxx::audio`)
- `InputPanel` (gated on `threadmaxx::input`)
- `AssetsPanel` (gated on `threadmaxx::assets`)
- `UIInspectorPanel` (gated on `threadmaxx::ui`)
- `NavmeshPanel` (gated on `threadmaxx::navmesh`)
- `PhysicsPanel` (gated on `threadmaxx::physics`)
- `ReflectPanel` (gated on `threadmaxx::reflect`)

### Added (M5–M6 — Network panels)
- `NetworkSessionPanel`, `SnapshotDeltaPanel`,
  `InterestPanel`, `DesyncPanel`, `PacketTracePanel`
  (gated on `threadmaxx::network`)

### Added (M7 — Out-of-process attach)
- `StudioAgent` — game-side RPC endpoint over `network::ITransport`
- `RemoteDataSource` (Shape B)
- Auth gate with per-peer token + production-default deny
- Mutation tunneling via label → factory map
- Per-peer interest filter (`ClientFocus`)
- `BandwidthPanel`
- `ShardDirectory` + `ShardPickerPanel`

### Added (M8 — Save/migration panels)
- `SaveInspectorPanel` — metadata, record list, diff vs current
- `MigrationStepPanel` — per-step pipeline walker
- `SchemaGraphPanel` — registry-driven graph + DOT export
- `MigrationValidatorPanel` — corpus-wide warning aggregation

### Added (M9 — close-out)
- `README.md`, `USER_GUIDE.md`, `MAINTAINER_GUIDE.md`,
  `PANEL_AUTHORING_GUIDE.md`, this `CHANGELOG.md`
- `examples/studio_demo/` — end-to-end demo driving every shipped
  panel for 600 frames against a small embedded engine
- `bench/studio_overhead.cpp` — perf gate (< 1 ms / frame at 1080p
  with 50 visible widgets)
- Version stamp bumped from `0.1.0-dev` → `1.0.0`

### Performance
- v1.0 perf gate: under 1 ms / frame for 50 visible panels under
  the headless backend.

### Test footprint
- 49 studio tests, all green on `build/` and `build-werror/`.
- Includes the four engine-link / interface canaries that pin the
  layering rules.

## [0.1.0-dev] — 2026-06-12

Pre-release scaffold. The version stamp ships from ST1 forward; no
external API stability promise until v1.0.
