# threadmaxx_input CHANGELOG

## v1.0.0 — 2026-06-11

Initial release. Input + binding sibling library for `threadmaxx`-based
games and tools. Shipped across eight batches (I1–I8); per-batch detail
in `FUTURE_WORK.md`.

### Highlights

- **Frame context + state** — `InputContext` owns the per-frame
  `InputState` POD; press / release edges derived as XOR diff over the
  previous frame; multiple contexts supported for split views.
- **Polling** — 256-bit keyboard bitset, mouse position + delta + 8
  buttons, 8 connected gamepads, 32-codepoint char queue, modifier
  mask.
- **Action map + bindings** — constexpr FNV-1a-32 `actionId(name)`;
  `Binding::Source` covers Key / MouseButton / GamepadButton /
  GamepadAxisPos / GamepadAxisNeg; multi-source de-dup on press;
  exact modifier match; binary serialize / deserialize through a
  tagged header (`'TMIB'`).
- **Gamepad** — multi-pad state with hot-plug, radial stick
  deadzones (inner/outer band, direction preserved at unit
  magnitude), 1D trigger threshold.
- **Cursor + capture** — `CursorMode { Visible, Hidden, Locked }`;
  Locked anchors absolute x/y and exposes only deltas. Sticky
  capture sinks for UI hand-off.
- **Picking** — `Camera` POD (column-major view + Vulkan-NDC
  projection), `screenToRay` / `worldToScreen`; numerically tight
  near the far plane.
- **UI bridge** — `toUIInput(ctx)` lowers the input state into
  `threadmaxx::ui::UIInput`. Gated build flag
  (`THREADMAXX_INPUT_HAS_UI_BRIDGE`).
- **Replay + backends** — `InputTrace` records the events drained on
  each `beginFrame`; serialize / deserialize through a tagged header
  (`'TMIN'`); replay into a fresh context produces byte-identical
  `InputState` snapshots. Backends: `NullBackend` (test sink, replay
  substrate) and `GlfwBackend` (header-only callback translation,
  does NOT link GLFW).

### Performance

- `bench/input_crowd_bench` at 200 actions / 800 bindings / 16 events
  per frame: **12.92 µs / frame** (~4× under the 50 µs gate).
- Zero-allocation contract pinned by `test_input_crowd_no_alloc`
  (500 actions / 2000 bindings / 8 connected pads / 16 events
  per frame, 100 measured frames).

### Test coverage

41 tests in `tests/input/`, all green on `build/` and `build-werror/`
(`-Wsign-conversion -Wconversion -Wold-style-cast -Werror`).
Categories: foundations (5) / polling (6) / actions (6) / gamepad (6) /
cursor+capture (5) / picking (5) / ui bridge (4, gated) / replay+backends
(4) / v1.0 crowd no-alloc gate.

### Public API

Every header under `include/threadmaxx_input/` (except `detail/`) is
part of the v1.x ABI contract. SemVer bump rules + deprecation policy
are documented in `MAINTAINER_GUIDE.md`.

### Deferred to v1.x

- Real evdev Linux backend (the `IInputBackend` interface is the
  integration point — host pluggable).
- Native macOS / Windows backends.
- Touch / pen input.
- Action chords (Ctrl+K then Ctrl+S two-step).
- IME / dead-key composition.
- Haptics / rumble (sibling output library).
