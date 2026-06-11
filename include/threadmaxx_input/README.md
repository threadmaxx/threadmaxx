# `threadmaxx_input`

Keyboard / mouse / gamepad polling, rebindable action map, screen-to-
world picking, and deterministic replay for tools and games built on the
`threadmaxx` ecosystem. **Status**: v1.0.0 — production-ready.

## What

A small, deterministic, zero-alloc-hot-path input layer. Owns the
per-frame input model the UI library, the editor camera controller, the
character controller, and the gizmo system all consume. Renderer- and
windowing-agnostic — the host owns the OS window and feeds raw events
through `IInputBackend`.

The library covers eight pillars:

- **Frame context** — `InputContext` owns the per-frame `InputState`
  POD (mouse, keys, 8 gamepads, char queue, modifier mask). Construct
  one per editor pane / viewport; no globals.
- **Keyboard + mouse polling** — bitset-backed key state with XOR-diff
  edge detection (`wasPressed` / `wasReleased`), modifier mask, char
  queue with overflow-drop, mouse position + delta accumulation +
  scroll.
- **Action map + bindings** — constexpr FNV-1a-32 `actionId(name)`,
  `BindingSet` with key / mouse / gamepad / axis sources, exact
  modifier matching (Ctrl+S is NOT Shift+Ctrl+S), multi-source de-dup,
  binary serialize / deserialize.
- **Gamepad** — multi-pad state, hot-plug, radial-deadzone paired-axis
  stick reads, 1D trigger threshold, axis-as-button bindings.
- **Cursor + capture** — `CursorMode { Visible, Hidden, Locked }`;
  locked mode anchors absolute x/y and exposes only deltas. Sticky
  capture sinks (`setCaptureMouse/Keyboard`, `wantsMouse/Keyboard`)
  for UI / game hand-off.
- **Picking** — `Camera` POD (column-major view + Vulkan-NDC
  projection), `screenToRay` / `worldToScreen` with numerically tight
  normalize.
- **UI bridge** — optional `toUIInput(ctx)` lowers the input state into
  `threadmaxx::ui::UIInput` (gated on `TARGET threadmaxx::ui`); maps
  modifier + nav-key bits 1:1.
- **Replay + backends** — `InputTrace` records frame events,
  serialize/deserialize, replays into a fresh context byte-identically.
  Ships `NullBackend` (test sink, replay substrate) and `GlfwBackend`
  (translation-layer for GLFW hosts — does NOT link GLFW).

The library is engine-agnostic. The single optional outgoing edge is
the UI bridge (gated by `TARGET threadmaxx::ui` at configure time).

## Quick start

```cmake
target_link_libraries(my_target PRIVATE threadmaxx::input)
```

```cpp
#include <threadmaxx_input/threadmaxx_input.hpp>

using namespace threadmaxx::input;

// 1. Pick a backend. NullBackend in tests / replay; GlfwBackend for GLFW hosts.
GlfwBackend backend;
InputContext ctx;
ctx.setBackend(&backend);

// 2. Configure actions.
BindingSet bs;
bs.bind("Jump",      Binding::key(Key::Space));
bs.bind("Jump",      Binding::gamepadButton(GamepadButton::A));
bs.bind("Save",      Binding::key(Key::S, Modifiers::Ctrl));
bs.bind("MoveX",     Binding::gamepadAxisPositive(GamepadAxis::LStickX, 0.3f));
ctx.setBindings(bs);

// 3. In the host's window callbacks, push raw events into the backend:
//    backend.pushGlfwKey(key, scancode, action, mods);
//    backend.pushGlfwCursorPos(xpos, ypos);
//    ...

// 4. Per tick: drain + query.
ctx.beginFrame(1.0f / 60.0f);
if (ctx.action("Jump").pressed)    doJump();
if (ctx.action("Save").pressed)    saveScene();
const auto stick = ctx.stickXY(kGamepad0DeviceId, Stick::Left);
ctx.endFrame();
```

`USER_GUIDE.md` walks each pillar in detail.
`MAINTAINER_GUIDE.md` documents the versioning and ABI policy.

## Performance

`bench/input_crowd_bench` is the throughput gate. On the v1.0 dev target
(Linux x86_64, Release build):

| Actions | Bindings | Events/frame | Avg / frame |
|--------:|---------:|-------------:|------------:|
|     200 |      800 |           16 | **12.9 µs** (~4× under the 50 µs gate) |

Zero-allocation contract pinned by `test_input_crowd_no_alloc` (500
actions / 2000 bindings / 8 connected pads / 16 events per frame, 100
measured frames after a 5-frame warmup, zero heap traffic under a
tracking allocator) and the per-batch no-alloc gates.

## Tests

41 tests in `tests/input/`, all green on `build/` and `build-werror/`
(`-Wsign-conversion -Wconversion -Wold-style-cast -Werror`). Categories:

- **Foundations** (5) — POD shape, event variant, lifecycle,
  no-alloc, header cycle canary.
- **Polling** (6) — keys held / released, mouse buttons / motion,
  char queue overflow, modifier mask.
- **Action map** (6) — constexpr id, single source, multi source,
  modifier exact match, binary round-trip, no-alloc.
- **Gamepad** (6) — connect, buttons, axis deadzone, paired-axis
  radial, action bindings, hot-unplug.
- **Cursor + capture** (5) — default mode, locked-relative,
  mouse / keyboard capture, sticky semantics.
- **Picking** (5) — identity camera, round trip, viewport offset,
  far plane unit length, no-alloc.
- **UI bridge** (4) — mouse, keys+nav, capture handshake, no-alloc.
- **Replay + backends** (4) — record / replay round-trip, serialize,
  no-alloc, GLFW translation smoke.

## Out of scope

- Action chords (Ctrl+K, Ctrl+S two-step) — v1.x.
- Touch / pen input — v1.x.
- Haptics / rumble — output-side sibling library.
- IME / dead-key composition — backends pass pre-composed codepoints.
- Long-term archival format for `InputTrace`.
- Real evdev Linux backend — `IInputBackend` is the integration point;
  v1.x.
- macOS / Windows native backends — same story; GLFW adapter covers
  both today.
