# `threadmaxx_studio` — User Guide

This guide walks a host author through standing up a studio over a
running engine. Familiarity with `threadmaxx_editor` is assumed —
studio sits on top of it.

## 1. Pick an attach mode

| Mode | Class | When |
|---|---|---|
| Shape A — Direct | `DirectDataSource` | In-process. Host owns the engine; studio reads / mutates directly. |
| Shape B — Remote | `RemoteDataSource` | Out-of-process. Studio binary talks to a `StudioAgent` running inside the game over `threadmaxx::network::ITransport`. |

Both implement the same `IStudioDataSource` interface. **Every panel
is mode-agnostic** — the host can swap a `DirectDataSource` for a
`RemoteDataSource` at runtime and the panels keep rendering.

## 2. Minimal in-process setup

```cpp
#include <threadmaxx_studio/threadmaxx_studio.hpp>
#include <threadmaxx_editor/threadmaxx_editor.hpp>
#include <threadmaxx_studio/panels/engine_inspector.hpp>
#include <threadmaxx_studio/panels/frame_snapshot.hpp>

threadmaxx::Engine                  engine{ {} };
threadmaxx::editor::EditorSession   session{engine};
threadmaxx::editor::HeadlessBackend backend;
threadmaxx::editor::CommandStack    cmds{engine};

threadmaxx::studio::DirectDataSource source{engine, cmds};
threadmaxx::studio::StudioApp        app{session};
app.start();

threadmaxx::studio::EntityInspectorPanel inspector;
threadmaxx::studio::FrameSnapshotPanel   snapshot;
app.panelHost().registerPanel(&inspector);
app.panelHost().registerPanel(&snapshot);

// Once per frame, after engine.step():
backend.beginFrame();
inspector.render(backend, source);
snapshot.render(backend, source);
backend.endFrame();
```

The host owns the actual ImGui / native frame loop. Studio does NOT
own the swap chain; it just hands you draw calls through
`IEditorBackend`.

## 3. Out-of-process setup (Shape B)

```cpp
// Game side — one StudioAgent per shard.
threadmaxx::network::LoopbackHub hub;
auto game_transport = hub.attach(0);
threadmaxx::studio::StudioAgent agent{*game_transport, gameSource};
agent.setAttachEnabled(true);             // disabled in NDEBUG by default
agent.setAuthToken("hunter2");
agent.setCommandStack(&cmds);

// Studio binary side — one RemoteDataSource per shard.
auto studio_transport = hub.attach(1);
threadmaxx::studio::RemoteDataSource remote{*studio_transport};
remote.authenticate("hunter2");
remote.requestEngineSnapshot();

// Pump both sides every frame:
agent.pump();
remote.pump();
```

The transport is plugin-shaped: the loopback hub above is what the
test suite uses, but any `network::ITransport` works. The wire format
is host-endian — see `network/README.md` for the same caveat that
applies to `WorldSnapshot`.

### Auth contract

In production builds (NDEBUG), `StudioAgent` rejects every request
until `setAttachEnabled(true)` AND a matching auth token is set and
the peer has authenticated. Define `THREADMAXX_STUDIO_AGENT_ENABLE_PROD`
to flip the production default. In dev builds the default permits
attach (so test suites do not have to plumb a token).

### Bandwidth budgeting

`RemoteDataSource::setRequestsPerTickBudget(N)` caps the number of
requests the studio side will emit per frame. Surplus requests bump
`RemoteDataSource::requestsDropped()`. The `BandwidthPanel`
visualizes both counters; if the corpus drops too many requests,
either widen the budget or reduce the panel count.

## 4. Picking panels

Every panel header is independent. Include only what you need:

```cpp
#include <threadmaxx_studio/panels/engine_inspector.hpp>
#include <threadmaxx_studio/panels/frame_snapshot.hpp>
#include <threadmaxx_studio/panels/property_editor.hpp>
#include <threadmaxx_studio/panels/gizmo.hpp>
#include <threadmaxx_studio/panels/tuning.hpp>
#include <threadmaxx_studio/panels/replay.hpp>
#include <threadmaxx_studio/panels/save_inspector.hpp>      // gated TARGET threadmaxx::migration
#include <threadmaxx_studio/panels/network_session.hpp>     // gated TARGET threadmaxx::network
```

Panel headers gated on a sibling target ALSO sit behind a
`THREADMAXX_STUDIO_HAS_<X>=1` compile definition. Host build systems
can check the definition before including:

```cmake
if (TARGET threadmaxx::studio)
    get_target_property(_defs threadmaxx::studio INTERFACE_COMPILE_DEFINITIONS)
    if (_defs MATCHES "THREADMAXX_STUDIO_HAS_NETWORK_PANELS=1")
        target_link_libraries(my_host PRIVATE threadmaxx::studio)
    endif()
endif()
```

## 5. Layout persistence

Studio reuses `editor::LayoutManager`. Panel visibility round-trips:

```cpp
threadmaxx::editor::LayoutManager layout{};
app.panelHost().saveTo(layout);

std::stringstream s;
layout.save(s);

// ... later ...

threadmaxx::editor::LayoutManager loaded;
loaded.load(s);
app.panelHost().restoreFrom(loaded);
```

Studio never defines its own layout format. Anything `LayoutManager`
can persist, studio can persist.

## 6. Mutations

Panels never call directly into the engine. Every mutation lands as
an `editor::IEditCommand` on the `CommandStack`:

```cpp
class HealCommand final : public threadmaxx::editor::IEditCommand {
public:
    HealCommand(threadmaxx::EntityHandle e, float oldHp, float newHp)
        : e_{e}, oldHp_{oldHp}, newHp_{newHp} {}
    std::string_view name() const noexcept override { return "Heal"; }
    void apply(threadmaxx::CommandBuffer& cb) override {
        cb.setHealth(e_, threadmaxx::Health{newHp_, 100.0f});
    }
    void undo(threadmaxx::CommandBuffer& cb) override {
        cb.setHealth(e_, threadmaxx::Health{oldHp_, 100.0f});
    }
private:
    threadmaxx::EntityHandle e_;
    float oldHp_, newHp_;
};
```

For Shape B, the studio side just calls
`source.submitCommand("Heal")` — the agent looks up the label in its
factory registry (`StudioAgent::registerCommandFactory`) and
constructs the command server-side. This is the seam that prevents
clients from forging arbitrary mutations.

## 7. Testing

Studio panels are designed for the headless backend. Every shipped
panel has a `tests/studio/test_studio_<name>.cpp` that builds the
panel, hands it a stub data source, calls `render()`, and asserts
against the captured draw ops. Host code can do the same.

```cpp
threadmaxx::editor::HeadlessBackend backend;
backend.initialize();
backend.beginFrame();
mypanel.render(backend, source);
auto& ops = backend.capturedFrame();
// ... assert ops.size() == ...
```

## 8. Performance

Per-frame overhead targets:

- < 1 ms / frame at 1080p with 50 panels visible.
- Headless backend renders deterministically (no allocation churn
  between frames).
- Shape B adds ~one RTT of latency per `EngineSnapshot`; the studio
  side caches the response and re-uses it across panels in the same
  frame.

If a host trips the budget, the first knobs to reach for:

1. Reduce visible panel count (`PanelHost::setVisible(id, false)`).
2. Throttle `RemoteDataSource` via
   `setRequestsPerTickBudget(N)`.
3. Use `editor::TelemetryOverlay` to find the offending panel.
4. Run `bench/studio_overhead` as a regression gate (see
   `MAINTAINER_GUIDE.md`).
