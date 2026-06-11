# `threadmaxx_input` — input and binding sibling library

## 1. Purpose

`threadmaxx_input` is the keyboard / mouse / gamepad abstraction
layer for games and tools built on `threadmaxx`. It owns the
per-frame input model, the rebindable action map, and the screen-
to-world picking helpers the editor needs to drive selection,
camera, and gizmo workflows.

It is for:

- frame-coherent polling of keyboard, mouse, and gamepad,
- edge detection (`wasPressed` / `wasReleased`) over raw state,
- multi-device support (player 1 KB+mouse, player 2 gamepad),
- text input (codepoint stream, IME-agnostic) for the UI library,
- action mapping — a "Jump" action bound to one or more raw inputs,
  rebindable at runtime and serializable,
- cursor mode (visible / hidden / locked / relative) and mouse
  capture queries,
- screen → world picking ray construction from a `Camera`,
- input recording and deterministic replay,
- a thin lowering into `threadmaxx::ui::UIInput` so the UI library
  never has to know what platform it's running on.

It is **not** for:

- platform / windowing — the host owns the window and feeds raw
  events,
- rendering or audio — input does not draw and does not play,
- gameplay logic — actions fire, the game decides what to do,
- 3D math beyond ray construction (see `threadmaxx_simd`),
- save game / serialization beyond `InputTrace` and `BindingSet`,
- ECS storage — game code reads action state, never the engine,
- haptics or rumble (deferred to a sibling output library).

That puts the cut at the line every editor / game I've reviewed
draws naturally: **above raw event pumping, below gameplay
interpretation**. The UI library, the camera controller, the gizmo
system, and the game's character controller all consume the same
`InputContext`.

## 2. Design principles

1. **Above the platform.** The library accepts raw input events
   through `IInputBackend::poll`; backends translate evdev / GLFW /
   Win32 / Wayland to a single event stream.
2. **Frame-coherent state.** Every query reflects the state as of
   the last `beginFrame()` — no mid-frame state changes.
3. **Edge + level.** `isHeld(key)` for level state, `wasPressed` /
   `wasReleased` for transitions. Both refresh once per frame.
4. **Action map is the gameplay-facing surface.** Game code talks
   to `action("Jump")`, not raw keys. Bindings load and save
   without code changes.
5. **Deterministic.** Same raw event stream + same `BindingSet` →
   byte-identical `InputState` snapshots and byte-identical action
   firing across runs.
6. **Zero-alloc hot path.** After warmup, `beginFrame/endFrame`
   produces zero heap traffic. Same gate as `threadmaxx_audio` and
   `threadmaxx_ui`.
7. **No engine coupling.** The library does not link
   `threadmaxx::threadmaxx`. It exports plain POD state + a UI
   bridge function.
8. **Renderer-neutral.** The picking-ray helpers consume a `Camera`
   POD (same shape as the engine's `render::Camera`), not a
   renderer handle.
9. **Multi-context, multi-device.** Several `InputContext`s can
   coexist (split-screen, editor pane + game viewport). Devices
   are explicitly enumerated and queried by `DeviceId`.
10. **Replay is first-class.** `InputTrace::record(ctx)` and
    `InputTrace::replay(ctx)` are public; tests use them for any
    multi-frame input scenario.

## 3. Package layout

```
include/threadmaxx_input/
  threadmaxx_input.hpp     # umbrella include
  config.hpp               # capacity caps, deadzone defaults
  types.hpp                # DeviceId, ActionId, Key, MouseButton,
                           # GamepadButton, GamepadAxis, Modifiers
  events.hpp               # InputEvent variant (raw event POD)
  state.hpp                # InputState POD (keys / mouse / gamepads)
  context.hpp              # InputContext (per-frame state owner)
  binding.hpp              # Binding, BindingSet, ActionMap
  action.hpp               # Action query API + ActionTrigger
  cursor.hpp               # CursorMode + capture queries
  picking.hpp              # Ray + screenToRay / worldToScreen
  ui_bridge.hpp            # toUIInput(InputContext&) -> UIInput
  trace.hpp                # InputTrace record / replay
  backend.hpp              # IInputBackend interface
  backends/
    NullBackend.hpp        # default; drops events / source for replay
    EvdevBackend.hpp       # Linux production backend
    GlfwBackend.hpp        # optional; matches engine's renderer host
  detail/
    keymap.hpp             # scancode <-> Key table
    edge_buffer.hpp        # frame-local "pressed-since" bitset
    deadzone.hpp           # radial / per-axis deadzone math
  version.hpp              # version macros + version_string()

src/threadmaxx_input/
  InputContext.cpp
  ActionMap.cpp
  Binding.cpp
  Picking.cpp
  UiBridge.cpp
  Trace.cpp
  backends/
    NullBackend.cpp
    EvdevBackend.cpp       # opt-in via THREADMAXX_INPUT_EVDEV
    GlfwBackend.cpp        # opt-in via THREADMAXX_INPUT_GLFW
tests/input/
  test_input_*.cpp
bench/
  input_*.cpp
```

The library produces a static lib `threadmaxx::input`. Backends
guarded by find_package: evdev is built when `libevdev` headers
are visible; GLFW backend is built when `glfw3` is.

## 4. Core data model

### 4.1 Devices and IDs

```cpp
namespace threadmaxx::input {

using DeviceId = std::uint16_t;
constexpr DeviceId kKeyboardDeviceId = 0;
constexpr DeviceId kMouseDeviceId    = 1;
constexpr DeviceId kGamepad0DeviceId = 2;  // ... up to kMaxGamepads

using ActionId = std::uint32_t;  // FNV-1a-64 truncated to 32; stable
                                 // across builds; conflicts log Warn.

enum class Key : std::uint16_t {
    Unknown = 0,
    A, B, C, ...,  // letters
    Num0, Num1, ..., Num9,
    F1, F2, ..., F24,
    Space, Enter, Tab, Escape, Backspace, Delete,
    Left, Right, Up, Down,
    Home, End, PageUp, PageDown, Insert,
    LShift, RShift, LCtrl, RCtrl, LAlt, RAlt, LSuper, RSuper,
    // ~120 entries; fits in a 256-bit bitset
};

enum class MouseButton : std::uint8_t {
    Left = 0, Right = 1, Middle = 2, X1 = 3, X2 = 4
};

enum class GamepadButton : std::uint8_t {
    A, B, X, Y, LBumper, RBumper, Back, Start, Guide,
    LStick, RStick, DPadUp, DPadDown, DPadLeft, DPadRight
};

enum class GamepadAxis : std::uint8_t {
    LStickX, LStickY, RStickX, RStickY, LTrigger, RTrigger
};

namespace Modifiers {
    constexpr std::uint8_t Shift = 1 << 0;
    constexpr std::uint8_t Ctrl  = 1 << 1;
    constexpr std::uint8_t Alt   = 1 << 2;
    constexpr std::uint8_t Super = 1 << 3;
}

} // namespace threadmaxx::input
```

### 4.2 Raw event stream

```cpp
namespace threadmaxx::input {

struct KeyEvent       { Key key; bool down; std::uint8_t modifiers; };
struct CharEvent      { std::uint32_t codepoint; };
struct MouseMoveEvent { float x, y; float dx, dy; };
struct MouseButtonEvent { MouseButton button; bool down; float x, y; };
struct MouseWheelEvent { float dx, dy; };
struct GamepadButtonEvent { DeviceId device; GamepadButton button; bool down; };
struct GamepadAxisEvent   { DeviceId device; GamepadAxis axis; float value; };
struct DeviceConnectEvent    { DeviceId device; bool gamepad; };
struct DeviceDisconnectEvent { DeviceId device; };

using InputEvent = std::variant<
    KeyEvent, CharEvent,
    MouseMoveEvent, MouseButtonEvent, MouseWheelEvent,
    GamepadButtonEvent, GamepadAxisEvent,
    DeviceConnectEvent, DeviceDisconnectEvent>;

} // namespace threadmaxx::input
```

The variant size is the dominant cache cost. Target ≤ 32 B; the
`MouseMoveEvent` is the largest at 16 B + tag.

### 4.3 Per-frame state

```cpp
namespace threadmaxx::input {

struct MouseState {
    float x{}, y{};
    float dx{}, dy{};
    float wheelDx{}, wheelDy{};
    std::uint8_t buttons{};         // bitset, MouseButton bit positions
    std::uint8_t buttonsPressed{};
    std::uint8_t buttonsReleased{};
};

struct GamepadState {
    bool connected{};
    std::uint16_t buttons{};        // bitset
    std::uint16_t buttonsPressed{};
    std::uint16_t buttonsReleased{};
    std::array<float, 6> axes{};    // GamepadAxis indexed
};

struct InputState {
    std::uint8_t modifiers{};       // Modifiers::Shift | ...
    detail::KeyBitset keys{};       // 256-bit
    detail::KeyBitset keysPressed{};
    detail::KeyBitset keysReleased{};
    MouseState mouse;
    std::array<GamepadState, kMaxGamepads> gamepads;
    std::array<std::uint32_t, kMaxCharsPerFrame> chars{};
    std::uint8_t charCount{};
};

} // namespace threadmaxx::input
```

`kMaxGamepads = 8`, `kMaxCharsPerFrame = 32` (overflow drops; never
allocates). Both are `config.hpp` knobs.

## 5. Public API

### 5.1 Context lifecycle

```cpp
namespace threadmaxx::input {

class InputContext {
public:
    InputContext();
    ~InputContext();

    void setBackend(IInputBackend* backend) noexcept; // borrowed
    void setBindings(const BindingSet& bindings);

    // Frame loop.
    void beginFrame(float deltaTimeSeconds);
    void endFrame();

    // Direct queries.
    const InputState& state() const noexcept;

    bool isHeld(Key k) const noexcept;
    bool wasPressed(Key k) const noexcept;
    bool wasReleased(Key k) const noexcept;

    bool isHeld(MouseButton b) const noexcept;
    bool wasPressed(MouseButton b) const noexcept;
    bool wasReleased(MouseButton b) const noexcept;

    bool isHeld(DeviceId pad, GamepadButton b) const noexcept;
    float axis(DeviceId pad, GamepadAxis a) const noexcept; // post-deadzone

    // Action queries.
    ActionTrigger action(ActionId id) const noexcept;
    ActionTrigger action(std::string_view name) const noexcept; // hashes

    // Capture sinks (UI library tells us to swallow input).
    void setCaptureMouse(bool capture) noexcept;
    void setCaptureKeyboard(bool capture) noexcept;
    bool wantsMouse() const noexcept;     // captured or UI hovered
    bool wantsKeyboard() const noexcept;  // captured or UI focused

    // Pre-allocation knobs.
    void reserveEvents(std::size_t n);
};

} // namespace threadmaxx::input
```

### 5.2 Bindings and actions

```cpp
namespace threadmaxx::input {

struct Binding {
    enum class Source : std::uint8_t {
        Key, MouseButton, GamepadButton, GamepadAxisPos, GamepadAxisNeg
    };
    Source source;
    std::uint8_t modifiers{};   // required modifiers
    std::uint16_t code{};       // Key / MouseButton / etc. cast to u16
    DeviceId device{};          // for gamepad sources; 0 for KB+mouse
    float threshold{0.5f};      // for axis sources
};

class BindingSet {
public:
    void bind(ActionId id, Binding b);
    void bind(std::string_view name, Binding b); // hashes name
    void clear(ActionId id);

    std::span<const Binding> bindingsFor(ActionId id) const noexcept;
    std::span<const std::pair<ActionId, Binding>> all() const noexcept;

    // (De)serialization. Binary, tagged with magic + version.
    std::vector<std::byte> serialize() const;
    bool deserialize(std::span<const std::byte> bytes);
};

struct ActionTrigger {
    bool held{};      // any bound source is currently held
    bool pressed{};   // any bound source transitioned to held this frame
    bool released{};  // any bound source transitioned to released this frame
    float value{};    // 0..1 for axis-derived; 1.0 for digital while held
};

constexpr ActionId actionId(std::string_view name) noexcept; // FNV-1a, constexpr

} // namespace threadmaxx::input
```

Bindings with required modifiers don't fire when the modifier mask
doesn't match exactly — `Ctrl+S` is not `Shift+Ctrl+S`. Action
queries OR across all bindings; `held` is true if any source is
held; `pressed` is true if any source transitioned to held *while
no source was already held* (avoids double-fire on multi-binding
chord activation).

### 5.3 Cursor mode

```cpp
namespace threadmaxx::input {

enum class CursorMode : std::uint8_t {
    Visible,     // normal pointer
    Hidden,      // hidden but absolute coords
    Locked       // hidden + relative coords + warped to center each frame
};

void setCursorMode(InputContext& ctx, CursorMode mode);
CursorMode cursorMode(const InputContext& ctx) noexcept;

} // namespace threadmaxx::input
```

`Locked` is what the editor's first-person camera mode needs; the
backend warps the OS cursor to viewport center each frame and the
library reports relative `dx/dy` only.

### 5.4 Picking

```cpp
namespace threadmaxx::input {

struct Camera {
    float view[16];        // column-major
    float projection[16];  // column-major, Vulkan-style NDC z ∈ [0, 1]
    float viewportX{}, viewportY{};
    float viewportW{}, viewportH{};
};

struct Ray {
    float origin[3];
    float direction[3];    // unit length
};

Ray screenToRay(const Camera& cam, float screenX, float screenY) noexcept;

struct ScreenPoint { float x{}, y{}; bool inFrontOfCamera{}; };
ScreenPoint worldToScreen(const Camera& cam, const float worldXyz[3]) noexcept;

} // namespace threadmaxx::input
```

The projection matrix is engine-compatible (Vulkan-style NDC). The
math inverts the combined view-projection and pushes a near-plane
point through it. No SIMD dependency; trivially testable.

### 5.5 UI bridge

```cpp
namespace threadmaxx::input {

// Lowers the input context's per-frame state into the UI library's
// POD. Caller owns the returned UIInput.
threadmaxx::ui::UIInput toUIInput(const InputContext& ctx) noexcept;

} // namespace threadmaxx::input
```

This is the only file that includes `threadmaxx_ui/input.hpp`; the
linker decides whether `threadmaxx::ui` is present. The bridge
header is opt-in via `THREADMAXX_INPUT_UI_BRIDGE`.

### 5.6 Recording and replay

```cpp
namespace threadmaxx::input {

class InputTrace {
public:
    void record(const InputContext& ctx); // appends events for current frame

    // Drives the next frame from a recorded entry. After replay,
    // backend events are merged in unless setBackend(nullptr).
    void replay(InputContext& ctx, std::uint64_t frameIndex);

    std::uint64_t frameCount() const noexcept;
    std::vector<std::byte> serialize() const;
    bool deserialize(std::span<const std::byte> bytes);
};

} // namespace threadmaxx::input
```

Wire format `[magic 'TMIN'][version u32][frameCount u64]` then per
frame `[eventCount u32][events...]`. Same caveat as `WorldSnapshot`:
host-endian POD, intended for short-lived test fixtures and
in-session repro, not long-term archival.

## 6. Backends

### 6.1 `IInputBackend`

```cpp
namespace threadmaxx::input {

class IInputBackend {
public:
    virtual ~IInputBackend() = default;

    // Drains queued raw events into `out`. Returns the number written.
    // out has capacity >= cap; backend writes at most cap entries and
    // queues the rest for the next call.
    virtual std::size_t poll(InputEvent* out, std::size_t cap) = 0;

    // Applies a cursor mode change (no-op for replay / null).
    virtual void setCursorMode(CursorMode mode) {}

    // Lists currently-connected gamepads by DeviceId.
    virtual std::span<const DeviceId> connectedGamepads() const = 0;
};

} // namespace threadmaxx::input
```

### 6.2 Concrete backends

- **`NullBackend`** — drops cursor changes, reports no gamepads,
  returns 0 events. The test default and the substrate for
  `InputTrace::replay`.
- **`EvdevBackend`** — Linux production backend over `/dev/input/`.
  Reads `evdev` event nodes, translates to `InputEvent`. Hot-plug
  via `inotify` on `/dev/input/`. Required for the editor on Linux.
- **`GlfwBackend`** — drop-in for hosts already using GLFW (the
  engine's reference Vulkan renderer + `rpg_demo` already link
  GLFW). Translates GLFW callbacks to the same event stream.
  Avoids forcing the engine to take a hard dependency on `evdev`.

Backends are independent translation units; build flags decide
which ones link. The library always provides `NullBackend`.

## 7. Implementation notes

### 7.1 Edge detection

`InputState::keysPressed` and `keysReleased` are derived in
`beginFrame()` from the diff between the previous frame's `keys`
bitset and the post-event-drain `keys` bitset. Same pattern for
mouse and gamepad buttons. Edge detection is therefore a single
XOR per bitset word, not a per-key transition log — cheap and
cache-friendly.

### 7.2 Modifier matching

`Binding::modifiers` is an exact-match mask, not a "must-contain"
mask. Rationale: if `Ctrl+S` matched any state with Ctrl held, then
`Shift+Ctrl+S` would also fire `Save` — wrong for editors. Hosts
that want "Ctrl regardless of Shift" can bind both explicitly or
post-filter via `wasPressed(Key::S) && modifiers & Modifiers::Ctrl`.

### 7.3 Deadzone math

`detail::deadzone` applies radial deadzones to stick pairs and
per-axis deadzones to triggers. Defaults: stick inner 0.15, outer
0.95, response curve linear; trigger threshold 0.05. All knobs are
on `BindingSet` per action — global defaults live in `config.hpp`.

### 7.4 Picking math

`screenToRay` builds an inverse view-projection matrix once per
call, sends the screen-space NDC point at `z = 0` (near plane,
Vulkan NDC) through it to get the world-space origin, sends `z = 1`
(far plane) through it to get a point on the ray, and normalizes
the difference. `worldToScreen` is the forward path: view *
projection, perspective divide, NDC → viewport.

### 7.5 Zero-alloc hot path

`InputContext` pre-reserves:
- the event drain buffer (`reserveEvents` warmup),
- the per-frame `chars` array (fixed-cap),
- the bitset words (fixed-cap),
- the gamepad array (fixed-cap).

`ActionMap` is open-addressing `unordered_map<ActionId, vector<Binding>>`
under the hood, but the vector is moved-from and recycled across
`bind()` calls. Action queries are pure reads.

`BindingSet::all()` returns a borrowed span over a flat vector
that mirrors the map for iteration; rebuilt on `bind`/`clear`,
not on query.

### 7.6 Determinism

The library performs no time-dependent decisions in `beginFrame` /
`endFrame` apart from accumulating `deltaTimeSeconds`. Identical
event streams + identical bindings + identical `deltaTimeSeconds`
sequences produce byte-identical `InputState`s and byte-identical
action firing. Pinned by the I8 replay test.

### 7.7 No-engine-coupling check

`threadmaxx_input` does not transitively include any
`threadmaxx/` core header. The UI bridge file is the single
exception and is gated by `THREADMAXX_INPUT_UI_BRIDGE`. CI checks
the include graph in I1's foundation test.

## 8. Suggested implementation order

Eight shippable batches plus a v1.0 close-out. Numbering is `Ik`.

1. **I1 — Foundations.** `InputContext`, `InputState`, raw event
   variant, `NullBackend`, frame begin/end, zero-alloc gate.
2. **I2 — Keyboard + mouse polling.** Event drain → state
   bitsets, edge detection, character queue, modifier mask.
3. **I3 — Action map + bindings.** `BindingSet`, `ActionId`
   hashing, `action(id)` query, binary (de)serialization,
   modifier-exact match.
4. **I4 — Gamepad.** Multi-pad state, deadzones, axis curves,
   button bindings, hot-plug events.
5. **I5 — Cursor mode + capture sinks.** `CursorMode`,
   `setCaptureMouse/Keyboard`, `wantsMouse/Keyboard` semantics.
6. **I6 — Picking.** `Camera` POD, `screenToRay`, `worldToScreen`,
   numerical-stability tests against the engine's projection.
7. **I7 — UI bridge.** `toUIInput`, gated by build flag, two-way
   capture handshake (UI tells input what to swallow).
8. **I8 — Replay / record + Linux + GLFW backends.** `InputTrace`
   serialize / replay, `EvdevBackend` over `/dev/input/`,
   `GlfwBackend` for GLFW-hosted apps.

Then v1.0 close-out: `version.hpp` bump to `1.0.0`, `README.md`,
`USER_GUIDE.md`, `MAINTAINER_GUIDE.md`, `CHANGELOG.md`, and the
500-action / 8-pad no-alloc gate. Same release shape as
`threadmaxx_ui` v1.0.0.

## 9. Open questions intentionally left for batches

- **Action chord vs simultaneous.** I3 ships single-source-or'd
  semantics. Chords (`Ctrl+K, Ctrl+S` two-step) are deferred to
  v1.x unless the editor pulls them in.
- **Touch input.** Not in the v1.0 surface. Backend interface is
  event-shaped, so adding `TouchEvent` variants in v1.x is
  additive.
- **Haptics / rumble.** Sibling output library, not this one.
- **IME / dead-key composition.** Backends pass the OS's already-
  composed codepoint through `CharEvent`. The library does not
  itself compose.
- **Mouse warp on `Locked`.** Backend implementations decide
  whether the warp happens before or after event drain; consistency
  pinned by an I5 test.
