# threadmaxx_editor — User Guide

## Setting up an editor session

```cpp
#include <threadmaxx/Engine.hpp>
#include <threadmaxx_editor/threadmaxx_editor.hpp>

threadmaxx::Engine engine{cfg};
NoopGame game;
engine.initialize(game);

threadmaxx::editor::EditorSession session{engine};
threadmaxx::editor::HeadlessBackend backend;
session.setBackend(&backend);   // installs + initialize()s
```

For ImGui (E11 / v1.x): swap `HeadlessBackend` for `ImGuiBackend`.

## Inspecting state

```cpp
threadmaxx::editor::Inspector ins{engine};
for (const auto& e : ins.listEntities()) {
    // e.handle, e.label, e.components
}
for (const auto& s : ins.listSystems()) {
    // s.name, s.waveIndex, s.lastStepMs, s.jobs
}
```

The resource panel uses opt-in tracking (the engine's `ResourceRegistry`
is type-keyed and doesn't enumerate by name):

```cpp
auto meshId = engine.resources().addRefCounted<Mesh>(Mesh{...});
ins.trackResource(meshId.id(), "models/hero.obj");
for (const auto& r : ins.listResources()) {
    // r.name, r.typeName, r.refCount, r.stale
}
```

## Editing through commands

`IEditCommand` is the v1.0 mutation contract. Define apply/undo
recipes that emit through a `CommandBuffer`:

```cpp
struct SetTransform final : threadmaxx::editor::IEditCommand {
    threadmaxx::EntityHandle target;
    threadmaxx::Transform from, to;
    std::string_view name() const noexcept override { return "SetTransform"; }
    void apply(threadmaxx::CommandBuffer& cb) override { cb.setTransform(target, to); }
    void undo (threadmaxx::CommandBuffer& cb) override { cb.setTransform(target, from); }
};

threadmaxx::editor::CommandStack stack{engine};
stack.execute(std::make_unique<SetTransform>(handle, oldT, newT));
engine.step();    // pump flushes on the next preStep boundary
stack.undo();
engine.step();
```

Constructing a `CommandStack` automatically registers a system on the
engine that drains the pending queue each `preStep` via
`ctx.single()`. Commits land on the engine's deterministic-commit
path — no special-casing required.

## Hot reload

```cpp
threadmaxx::editor::HotReloadController ctl{engine};
ctl.trackResource(meshId.id(), "models/hero.obj");

// Fires markResourceStale; loader picks it up on its next update().
ctl.requestReload({"models/hero.obj", false});

// On AssetReloaded the controller drops the path from pendingReloads
// and re-binds its tracked id to the new (index, generation).
```

## Telemetry overlay

```cpp
threadmaxx::editor::TelemetryOverlay overlay{engine};
overlay.render(backend);     // emits FPS / frame time / per-system lines
```

`OverlayConfig` toggles individual lines and sets the anchor point.

## Selection

```cpp
threadmaxx::editor::SelectionState sel{engine.world()};
sel.select(entityHandle);
if (sel.currentSelection().kind == threadmaxx::editor::SelectionKind::Entity) {
    auto h = sel.currentSelection().entity;
    // ...
}
```

Stale entity handles (generation mismatched) auto-clear on next
access.

## Reflection-driven property editing

`PropertyEditor` binds `threadmaxx_reflect`'s `TypeInfo` to the
editor:

```cpp
threadmaxx::reflect::TypeRegistry reg;
threadmaxx::editor::PropertyEditor ed{engine, reg};
ed.addBuiltinBindings();   // wires Transform / Health / ...

auto val = ed.readField(handle, "Health", "current");
float cur{};
val->get(cur);

ed.setField(stack, handle, "Health", "current",
            threadmaxx::reflect::Value::make<float>(7.0f));
engine.step();
```

Custom components opt in via `addBinding({typeName, TypeInfo*, get,
set})`. `THREADMAXX_REFLECT(MyComp, field, ...)` provides the
`TypeInfo*`; supply read/write trampolines for the binding.

## Gizmos

```cpp
threadmaxx::editor::TranslateGizmo giz;
auto frame = giz.frameFor(entityPos);

// Hit-test a screen-space ray → axis.
auto hit = giz.hitTest(frame, ray);
if (hit != threadmaxx::editor::GizmoAxis::None) {
    giz.beginDrag(hit);
}
// On drag update:
if (auto r = giz.updateDrag(deltaAlongAxis)) {
    auto newT = oldT;
    newT.position = newT.position + r->delta;
    stack.execute(threadmaxx::editor::TranslateGizmo::makeTranslateCommand(
        handle, oldT, newT));
}
giz.endDrag();
```

## World diff

```cpp
auto before = engine.world().snapshot();
// ... game runs, things change ...
auto after = engine.world().snapshot();
auto diff = threadmaxx::editor::WorldDiff::compute(before, after);
for (const auto& e : diff.entries) {
    // e.kind ∈ {Added, Removed, Modified}, e.handle, e.componentChanges
}
```

## Layout & console

```cpp
threadmaxx::editor::LayoutManager layout;
layout.state().panels["Inspector"] = true;
layout.save(stream);

threadmaxx::editor::Console console;
console.registerCommand({"setX", [&](std::span<const std::string> args) {
    return std::make_unique<MySetXCommand>(/*...*/);
}});
console.exec(stack, "setX 5");
```

## Threading

The editor is sim-thread-by-convention. `HotReloadController`'s
`requestReload` / `cancelReload` may be called from any thread; the
underlying `AssetReloaded` subscription fires on the engine's sim
thread during event drain.
