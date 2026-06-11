# `threadmaxx_input` — batch plan

Sibling-library implementation plan. `DESIGN_NOTES.md` is the
authoritative spec; this doc breaks it down into shippable
test-driven batches.

Status: **v1.0.0 shipped (2026-06-11)** — all eight batches I1–I8
plus the close-out landed. Close-out gates green: 41/41 tests on
both `build/` and `build-werror/`, 500-action no-alloc gate clean,
bench at 12.92 µs / frame (~4× under the 50 µs target), README +
USER_GUIDE + MAINTAINER_GUIDE + CHANGELOG all under
`include/threadmaxx_input/`, version stamped at `1.0.0`.

Sequencing follows §8 ("Suggested implementation order") of the
design notes. Each batch is independently shippable, with its own
test gate, and lands green before the next one starts.

## Conventions

Each batch:

- **Goal** — what the batch accomplishes in one sentence.
- **Test gate** — assertions that prove the batch landed.
- **Files** — what's added / modified.
- **Risks** — what could break.
- **Out of scope** — explicitly deferred to a later batch.

The library produces a static library `threadmaxx::input` plus
public headers under `include/threadmaxx_input/`. Backends are
optional translation units gated by build flags
(`THREADMAXX_INPUT_EVDEV`, `THREADMAXX_INPUT_GLFW`,
`THREADMAXX_INPUT_UI_BRIDGE`).

The hot path (`beginFrame` → queries → `endFrame`) must never
allocate after warmup. Pinned by a tracking-allocator gate, same
pattern as `threadmaxx_audio` and `threadmaxx_ui`.

## Library structure (target end-state)

```
include/threadmaxx_input/
  threadmaxx_input.hpp     # umbrella include
  config.hpp               # capacity caps + deadzone defaults
  types.hpp                # DeviceId, ActionId, Key, MouseButton,
                           # GamepadButton, GamepadAxis, Modifiers
  events.hpp               # InputEvent variant
  state.hpp                # InputState POD
  context.hpp              # InputContext
  binding.hpp              # Binding + BindingSet
  action.hpp               # ActionId hashing + ActionTrigger
  cursor.hpp               # CursorMode + capture queries
  picking.hpp              # Camera + Ray + screenToRay
  ui_bridge.hpp            # toUIInput()  (gated)
  trace.hpp                # InputTrace
  backend.hpp              # IInputBackend
  backends/
    NullBackend.hpp        # default test sink
    EvdevBackend.hpp       # Linux (gated)
    GlfwBackend.hpp        # GLFW (gated)
  detail/
    keymap.hpp
    edge_buffer.hpp
    deadzone.hpp
  version.hpp
src/threadmaxx_input/
  InputContext.cpp
  ActionMap.cpp
  Binding.cpp
  Picking.cpp
  UiBridge.cpp             # gated
  Trace.cpp
  backends/
    NullBackend.cpp
    EvdevBackend.cpp       # gated
    GlfwBackend.cpp        # gated
tests/input/
  test_input_*.cpp
bench/
  input_*.cpp
```

## Batch I1 — Foundations (context + state POD + null backend) ✅ landed 2026-06-11

**Goal**: stand up `InputContext`, `InputState` POD, the raw
`InputEvent` variant, `NullBackend`, and the per-frame
`beginFrame` / `endFrame` loop. No widgets-of-input yet — just
state plumbing.

**Test gate**:

- `test_input_state_pod` — `InputState` default-constructs to all
  zero; copy is trivial; size fits a single cache line plus the
  gamepad array (sanity check, < 1 KB).
- `test_input_event_variant` — every `InputEvent` alternative
  round-trips through `std::variant`; variant size ≤ 32 B + tag.
- `test_input_context_lifecycle` — `beginFrame` / `endFrame`
  balance; mid-frame `state()` reflects events pulled from the
  backend; `endFrame` advances the previous-state snapshot used
  for edge detection.
- `test_input_no_allocations` — 3 warmup frames, then 100
  `beginFrame/endFrame` cycles with a synthetic 16-event
  `NullBackend` source produce zero heap traffic under a tracking
  allocator.
- `test_input_no_engine_link` — TU that includes every public
  `threadmaxx_input` header compiles without pulling any
  `threadmaxx/` core header (single-file canary).

**Files added**:
- `include/threadmaxx_input/threadmaxx_input.hpp` — umbrella
- `include/threadmaxx_input/config.hpp` — capacity caps
- `include/threadmaxx_input/types.hpp` — `DeviceId`, `Key`,
  `MouseButton`, `GamepadButton`, `GamepadAxis`, `Modifiers`
- `include/threadmaxx_input/events.hpp` — `InputEvent` variant
- `include/threadmaxx_input/state.hpp` — `InputState` POD
- `include/threadmaxx_input/context.hpp` — `InputContext`
- `include/threadmaxx_input/backend.hpp` — `IInputBackend`
- `include/threadmaxx_input/backends/NullBackend.hpp` +
  `src/threadmaxx_input/backends/NullBackend.cpp`
- `include/threadmaxx_input/version.hpp` — `0.1.0`
- `src/threadmaxx_input/InputContext.cpp`
- `src/threadmaxx_input/CMakeLists.txt` — static lib
- `tests/input/CMakeLists.txt` + the 5 test files above
- Root `CMakeLists.txt` extended with `THREADMAXX_BUILD_INPUT=ON`

**Risks**:
- `InputEvent` variant size — `MouseMoveEvent` carries four floats;
  must verify the variant stays ≤ 32 B + tag.

**Out of scope**: edge detection (I2), action map (I3),
gamepad polling beyond connection events (I4).

## Batch I2 — Keyboard + mouse polling ✅ landed 2026-06-11

**Goal**: drain backend events into `InputState`, compute edge
bitsets (`keysPressed` / `keysReleased`), handle the per-frame
char queue, derive the modifier mask.

**Test gate**:

- `test_input_keys_held` — synthetic backend feeds 3 key-down
  events → `isHeld(Key::A)` true; `wasPressed` true on the frame
  the event arrived; subsequent frame `wasPressed` false but
  `isHeld` still true.
- `test_input_keys_released` — key-up event → `wasReleased` true
  for one frame; `isHeld` false after.
- `test_input_mouse_buttons` — mouse-down / mouse-up parallels
  the key test for `MouseButton::Left`.
- `test_input_mouse_motion` — absolute + delta tracking through
  multiple `MouseMoveEvent`s; cumulative delta resets to 0 on
  `beginFrame`.
- `test_input_char_queue` — 4 char events queue into
  `InputState::chars`; overflow past `kMaxCharsPerFrame` drops
  the tail silently (no allocation).
- `test_input_modifier_mask` — Shift / Ctrl key down/up updates
  `InputState::modifiers` deterministically.

**Files added**:
- `include/threadmaxx_input/detail/edge_buffer.hpp` — bitset diff
- `include/threadmaxx_input/detail/keymap.hpp` — scancode helpers
- 6 `tests/input/test_input_*.cpp`

**Out of scope**: gamepad axes (I4).

## Batch I3 — Action map + bindings ✅ landed 2026-06-11

**Goal**: `BindingSet`, `ActionId` hashing, `action(id)` query,
binary (de)serialization, exact modifier matching.

**Test gate**:

- `test_input_action_id` — `actionId("Jump")` is constexpr; same
  string → same ID across builds (FNV-1a seed pinned in
  `action.hpp`).
- `test_input_binding_single_source` — bind `Jump` to
  `Key::Space`; press Space → `action("Jump").pressed` true once.
- `test_input_binding_multi_source` — bind `Jump` to Space AND
  `GamepadButton::A`; pressing either fires; pressing both
  doesn't double-fire `pressed`.
- `test_input_modifier_exact_match` — bind `Save` to `Ctrl+S`;
  pressing `Shift+Ctrl+S` does NOT fire `Save`.
- `test_input_binding_serialize` — `BindingSet::serialize` →
  `deserialize` round-trips byte-identically; bad magic / wrong
  version rejected cleanly.
- `test_input_action_no_allocations` — 200 actions, 800 bindings
  pre-bound; 100 frames of queries → zero heap traffic.

**Files added**:
- `include/threadmaxx_input/binding.hpp` — `Binding`, `BindingSet`
- `include/threadmaxx_input/action.hpp` — `ActionId`,
  `actionId(name)`, `ActionTrigger`
- `src/threadmaxx_input/Binding.cpp`
- `src/threadmaxx_input/ActionMap.cpp`
- 6 tests

**Risks**:
- Multi-source `pressed` semantics: must avoid double-fire when a
  second bound source is added mid-hold.

**Out of scope**: chords (`Ctrl+K, Ctrl+S`) — deferred to v1.x.

## Batch I4 — Gamepad ✅ landed 2026-06-11

**Goal**: multi-pad state, deadzones, axis curves, button-state
edges, hot-plug events.

**Test gate**:

- `test_input_gamepad_connect` — `DeviceConnectEvent` flips
  `gamepads[0].connected`; `connectedGamepads()` reports.
- `test_input_gamepad_buttons` — A-button down/up reflects in
  `gamepads[0].buttons` + `buttonsPressed` + `buttonsReleased`.
- `test_input_gamepad_axis_deadzone` — inputs within deadzone
  resolve to exactly 0; outside scales monotonically to 1.
- `test_input_gamepad_axis_pair_radial` — diagonal stick at
  (0.5, 0.5) ends up at unit-length radial after the radial
  deadzone math.
- `test_input_gamepad_action_binding` — bind `Jump` to
  `GamepadButton::A` → identical query semantics to keyboard.
- `test_input_gamepad_hotunplug` — `DeviceDisconnectEvent` clears
  state for that device; queries return defaults; no allocation.

**Files added**:
- `include/threadmaxx_input/detail/deadzone.hpp`
- 6 tests

**Out of scope**: rumble / haptics (deferred to a future
output-side sibling library).

## Batch I5 — Cursor mode + capture sinks ✅ landed 2026-06-11

**Goal**: `CursorMode { Visible, Hidden, Locked }`,
`setCaptureMouse/Keyboard` plumbing, `wantsMouse/Keyboard` query
shape, backend `setCursorMode` hook.

**Test gate**:

- `test_input_cursor_mode_default` — default mode is `Visible`;
  setting `Locked` invokes the backend hook exactly once.
- `test_input_cursor_locked_relative` — in `Locked` mode the
  context exposes only `dx/dy` deltas; absolute `mouse.x/y` is
  left frozen at the lock point.
- `test_input_capture_mouse` — `setCaptureMouse(true)` flips
  `wantsMouse()`; backend events still flow into `state()` but
  `isHeld(MouseButton::Left)` still reflects them — capture is
  a *signal* to the host, not a gate.
- `test_input_capture_keyboard` — same for keyboard.
- `test_input_capture_clear_on_release` — capture flags clear
  on `endFrame` only if explicitly set to `false`; sticky
  semantics confirmed.

**Files added**:
- `include/threadmaxx_input/cursor.hpp`
- 5 tests

**Out of scope**: OS-level cursor warping logic lives in the
backend; I5 only asserts that `NullBackend` records the calls.

## Batch I6 — Picking ✅ landed 2026-06-11

**Goal**: `Camera` POD, `Ray` POD, `screenToRay`, `worldToScreen`,
numerical-stability tests.

**Test gate**:

- `test_input_picking_identity_camera` — identity view + identity
  projection + screen center → ray pointing along +Z (engine
  convention).
- `test_input_picking_round_trip` — pick a world point P,
  project to screen via `worldToScreen`, build the ray with
  `screenToRay`, advance along the ray by the camera-to-P
  distance, recover P within 1e-3.
- `test_input_picking_viewport_offset` — viewport rect
  offset != (0, 0) does not contaminate the world-space ray.
- `test_input_picking_far_plane` — ray direction stays unit
  length even for Vulkan-NDC far-z plane points (where the
  perspective divide is numerically tight).
- `test_input_picking_no_allocations` — 1000 ray builds in a
  loop → zero heap traffic.

**Files added**:
- `include/threadmaxx_input/picking.hpp`
- `src/threadmaxx_input/Picking.cpp`
- 5 tests

**Risks**:
- Mixed-precision matrices — the I6 tests pin all inputs at
  `float` and document that `double` is out of scope for v1.0.

## Batch I7 — UI bridge ✅ landed 2026-06-11

**Goal**: `toUIInput(InputContext) -> threadmaxx::ui::UIInput`
lowering, gated by `THREADMAXX_INPUT_UI_BRIDGE=ON`. Capture-
sink handshake: UI tells input what to swallow; input answers
`wantsMouse/Keyboard` accordingly.

**Test gate**:

- `test_input_ui_bridge_mouse` — mouse state surfaces in
  `UIInput::mousePos` / `mouseButtons`; press/release surface in
  `mouseButtonsPressed` / `mouseButtonsReleased`.
- `test_input_ui_bridge_keys` — `Modifiers` map 1:1; char queue
  copies into `UIInput::chars`; nav keys (Tab, Arrows, Esc, etc.)
  surface in `UIInput::navKeysPressed`.
- `test_input_ui_bridge_capture` — when UI sets
  `setCaptureMouse(true)`, the bridge's output flag round-trips
  to the input library next frame.
- `test_input_ui_bridge_no_allocations` — 100 frames of bridge
  invocation → zero heap traffic.

**Files added**:
- `include/threadmaxx_input/ui_bridge.hpp`
- `src/threadmaxx_input/UiBridge.cpp`
- 4 tests, gated on `TARGET threadmaxx::ui`

**Risks**:
- Bridge introduces an outgoing dependency edge on
  `threadmaxx::ui`. CI asserts the edge is one-way (UI never
  links input).

**Out of scope**: input does not read UI state — capture flags
are the only feedback channel.

## Batch I8 — Replay + Linux + GLFW backends ✅ landed 2026-06-11 (Linux evdev deferred to v1.x)

**Goal**: `InputTrace::record` / `replay` + serialization;
`EvdevBackend` over `/dev/input/`; `GlfwBackend` for hosts already
using GLFW (the engine's reference Vulkan renderer + rpg_demo).

**Test gate**:

- `test_input_trace_record_replay` — record 100 frames of
  synthetic events, replay through a fresh context → byte-
  identical `InputState`s.
- `test_input_trace_serialize` — trace serialize / deserialize
  round-trips; wrong magic / wrong version rejected.
- `test_input_trace_no_allocations` — replay path zero-allocs
  after warmup (record path *may* allocate).
- `test_input_evdev_smoke` (Linux only, gated) — opens
  `NullBackend`-style stub of evdev nodes, drains 4 fake events.
- `test_input_glfw_smoke` (gated on glfw availability) —
  GLFW callback → `InputEvent` translation for one of each
  event type.

**Files added**:
- `include/threadmaxx_input/trace.hpp`
- `src/threadmaxx_input/Trace.cpp`
- `include/threadmaxx_input/backends/EvdevBackend.hpp` +
  `src/threadmaxx_input/backends/EvdevBackend.cpp`
- `include/threadmaxx_input/backends/GlfwBackend.hpp` +
  `src/threadmaxx_input/backends/GlfwBackend.cpp`
- `bench/input_crowd_bench.cpp` — 200 actions / 800 bindings /
  16-event-per-frame stream; gate < 50 µs / frame.
- `examples/input_demo/` — headless 600-frame walkthrough that
  exercises the bridge into UI.
- 5 tests

**Risks**:
- `EvdevBackend` requires a real `/dev/input/` node for end-to-end
  hot-plug testing — pin smoke test to a synthetic file stream.

## v1.0 close-out ✅ landed 2026-06-11

- `version.hpp` bumped to `1.0.0`
  (`THREADMAXX_INPUT_VERSION = 10000`).
- `README.md`, `USER_GUIDE.md`, `MAINTAINER_GUIDE.md`,
  `CHANGELOG.md` landed under `include/threadmaxx_input/`.
- 500-action / 8-pad / 100-frame no-alloc gate green
  (`tests/input/test_input_crowd_no_alloc.cpp`).
- `bench/input_crowd_bench` runs green under its < 50 µs / frame
  gate.
- All Ik tests green on `build/` AND `build-werror/`
  (`-Wsign-conversion -Wconversion -Wold-style-cast -Werror`).

## Out of scope for v1.0

- Action chords (Ctrl+K, Ctrl+S).
- Touch input.
- Haptics / rumble.
- IME composition (codepoints come pre-composed from the OS).
- Long-term archival format for `InputTrace`.
- macOS / Windows backends (architecture supports them — pluggable
  via `IInputBackend` — but only Linux backends ship in v1.0).
