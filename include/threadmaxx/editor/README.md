# `editor` — editor, tooling, and hot-reload UI sibling library

## 1. Purpose

`editor` provides the interactive development layer for projects built on `threadmaxx`.

It is for:

* live scene inspection,
* entity/component editing,
* resource browsing,
* hot reload orchestration,
* debug overlays,
* profiling and telemetry views,
* world-state diffing,
* component graph visualization,
* simple in-engine tools panels,
* play-in-editor / simulate-in-editor workflows.

It is **not** for:

* core simulation logic,
* rendering backend ownership,
* physics ownership,
* networking ownership,
* navmesh ownership,
* animation math,
* ECS storage ownership,
* asset importing as a hard dependency of the engine.

That matches the roadmap’s boundary: editor and tooling live **above the engine**, and should not enter the core until the public API is stable enough to support them cleanly. 

## 2. Design principles

1. **Above the engine.** No changes to core simulation behavior.
2. **Hot-reload aware.** Rebuild/reload resources through the existing resource events.
3. **UI is optional.** Headless tooling should still work.
4. **Reflective, not magical.** The library inspects engine-exposed types and metadata; it should not depend on private internals.
5. **Command-based edits.** UI edits become engine commands, not direct mutation.
6. **Undo/redo first.** Every edit action should be reversible.
7. **Live-safe.** The editor must tolerate the simulation running while inspecting it.
8. **Renderer-agnostic UI backend.** Dear ImGui, SDL, custom UI, or web-based frontends can sit behind the same core.
9. **Small core, big tools.** Keep the runtime core thin and put heavy features in optional modules.
10. **Debuggability over cleverness.** The goal is clarity while authoring a game, not a framework maze.

## 3. Suggested package layout

```text id="m2e7kv"
include/threadmaxx/editor/
  editor.hpp             // umbrella include
  session.hpp            // editor session and connection to engine
  inspect.hpp            // entity, component, resource, and system inspection
  commands.hpp           // edit commands, undo/redo, transaction groups
  hotreload.hpp          // resource reload orchestration
  selection.hpp          // selection state and focus tracking
  hierarchy.hpp          // scene graph / entity tree views
  properties.hpp         // property panels and field editors
  telemetry.hpp          // FPS, frame time, system stats, trace overlays
  diff.hpp               // world/resource diffs, snapshot comparison
  gizmo.hpp              // transform gizmos, handles, selection rays
  asset_browser.hpp      // resource tree and previews
  console.hpp            // command console and scripting hooks
  layout.hpp             // docking/layout state
  serialization.hpp      // editor layout save/load
  backend.hpp            // UI backend interface
  detail/
    type_registry.hpp
    field_reflection.hpp
    undo_stack.hpp
    command_queue.hpp
    stable_id_map.hpp
```

If you want editor transport and UI split apart, keep the backend interface and the UI widgets separate so the same editor logic can power desktop, remote, or embedded tooling.

## 4. Core model

### 4.1 Session

The editor operates on a live or recorded `threadmaxx` session.

```cpp id="r7h4tc"
namespace threadmaxx::editor {

struct SessionId {
    std::uint64_t value{};
};

class EditorSession {
public:
    explicit EditorSession(threadmaxx::Engine& engine);

    SessionId id() const noexcept;

    threadmaxx::Engine& engine() noexcept;
    const threadmaxx::Engine& engine() const noexcept;
};

} // namespace threadmaxx::editor
```

### 4.2 Selection

```cpp id="d1p8sf"
namespace threadmaxx::editor {

struct SelectionId {
    std::uint64_t value{};
};

enum class SelectionKind : std::uint8_t {
    None,
    Entity,
    Resource,
    System,
    Event,
    TraceItem
};

struct Selection {
    SelectionKind kind{SelectionKind::None};
    std::uint64_t id{};
};

} // namespace threadmaxx::editor
```

### 4.3 Edit commands

Everything that changes state should go through a command object.

```cpp id="y4n6qa"
namespace threadmaxx::editor {

enum class EditResult : std::uint8_t {
    Applied,
    Rejected,
    Deferred
};

class IEditCommand {
public:
    virtual ~IEditCommand() = default;
    virtual std::string_view name() const noexcept = 0;
    virtual EditResult apply(threadmaxx::Engine& engine) = 0;
    virtual void undo(threadmaxx::Engine& engine) = 0;
};

} // namespace threadmaxx::editor
```

That is important because the roadmap’s core is deterministic and command-buffer driven; the editor should mirror that model rather than bypass it. 

## 5. Public API

### 5.1 Editor backend interface

```cpp id="w5c3nj"
namespace threadmaxx::editor {

class IEditorBackend {
public:
    virtual ~IEditorBackend() = default;

    virtual bool initialize() = 0;
    virtual void shutdown() = 0;

    virtual void beginFrame() = 0;
    virtual void endFrame() = 0;

    virtual void drawText(std::string_view text, float x, float y) = 0;
    virtual void drawRect(float x, float y, float w, float h) = 0;
};

} // namespace threadmaxx::editor
```

This lets the same editor logic render through ImGui, a custom UI, or even a remote browser frontend.

### 5.2 Command stack

```cpp id="u8b2mx"
namespace threadmaxx::editor {

class CommandStack {
public:
    void execute(std::unique_ptr<IEditCommand> command, threadmaxx::Engine& engine);
    bool canUndo() const noexcept;
    bool canRedo() const noexcept;

    void undo(threadmaxx::Engine& engine);
    void redo(threadmaxx::Engine& engine);

    void clear();
};

} // namespace threadmaxx::editor
```

### 5.3 Inspection API

```cpp id="z3q9fn"
namespace threadmaxx::editor {

struct EntitySummary {
    threadmaxx::EntityHandle handle{};
    std::string label;
    std::vector<std::string> components;
};

struct ResourceSummary {
    std::string name;
    std::string typeName;
    std::uint64_t refCount{};
    bool stale{};
};

struct SystemSummary {
    std::string name;
    std::uint32_t waveIndex{};
    float lastStepMs{};
    std::uint32_t jobs{};
};

class Inspector {
public:
    std::vector<EntitySummary> listEntities() const;
    std::vector<ResourceSummary> listResources() const;
    std::vector<SystemSummary> listSystems() const;

    std::optional<EntitySummary> entity(threadmaxx::EntityHandle handle) const;
};

} // namespace threadmaxx::editor
```

### 5.4 Hot reload

The roadmap already has hot-reload resource events and loader hooks, so the editor should orchestrate those rather than invent a second reload mechanism. 

```cpp id="k6m1ru"
namespace threadmaxx::editor {

struct ReloadRequest {
    std::string resourcePath;
    bool forceReimport = false;
};

struct ReloadResult {
    bool ok{};
    std::string message;
};

class HotReloadController {
public:
    ReloadResult requestReload(const ReloadRequest& request);
    std::vector<std::string> pendingReloads() const;
    void cancelReload(std::string_view resourcePath);
};

} // namespace threadmaxx::editor
```

### 5.5 Debug overlays and telemetry

```cpp id="p9x6va"
namespace threadmaxx::editor {

struct OverlayConfig {
    bool showFPS = true;
    bool showFrameTime = true;
    bool showSystemStats = true;
    bool showTraceEvents = false;
    bool showSelectionBounds = true;
};

class TelemetryOverlay {
public:
    void setConfig(const OverlayConfig& config);
    void update(const threadmaxx::Engine& engine);
};

} // namespace threadmaxx::editor
```

### 5.6 Layout and persistence

```cpp id="s1v4th"
namespace threadmaxx::editor {

struct LayoutState {
    std::string dockJson;
    std::string selectedPanel;
};

class LayoutManager {
public:
    void save(std::ostream& out) const;
    void load(std::istream& in);
};

} // namespace threadmaxx::editor
```

## 6. Integration with `threadmaxx`

The editor should consume the public engine API, not poke private state.

The roadmap already says the engine should expose stable public hooks for components, resources, events, snapshots, telemetry, and renderer-prep data; those are exactly what an editor needs.  

### 6.1 Engine-side data flow

* inspect entities through public query/snapshot APIs,
* inspect resources through resource registries and handles,
* inspect systems through stats and task-graph snapshots,
* inspect frames through trace and telemetry sinks,
* apply edits through commands.

### 6.2 Live simulation safety

The editor should never mutate live state directly from UI code. It should queue edit commands and let the engine commit them in the normal deterministic path. That keeps the tooling layer compatible with the engine’s commit discipline and rollback-friendly behavior. 

### 6.3 Replay / capture mode

The editor should be able to attach to:

* a live engine,
* a replayed session,
* a recorded snapshot,
* a remote target.

That makes it useful for debugging production captures, not just in-editor authoring.

## 7. What the library should not do

* no engine internals access,
* no simulation authority,
* no renderer backend ownership,
* no physics solver ownership,
* no navmesh ownership,
* no audio ownership,
* no networking protocol ownership,
* no hidden mutation path outside commands,
* no mandatory GUI toolkit dependency in the core.

That keeps it aligned with the roadmap’s “small stable contract” philosophy and avoids making the engine carry editor baggage before the public API is settled.  

## 8. Implementation order

1. session and command stack,
2. entity/resource inspection,
3. system stats panel,
4. hot-reload controller,
5. layout persistence,
6. telemetry overlays,
7. property editing and reflection,
8. gizmos and selection,
9. world diffing,
10. console/scripting hooks.

## 9. Tests to add

* command undo/redo round trips,
* selection persistence across frames,
* entity/resource/system inspection accuracy,
* hot-reload request and cancel behavior,
* layout save/load round trips,
* telemetry overlay updates from engine stats,
* snapshot diff correctness,
* editor-safe live session mutation tests,
* replay-session inspection tests,
* backend-agnostic UI smoke tests.
