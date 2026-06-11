# `threadmaxx_input` — User Guide

The library hands you `InputContext`. Everything else flows through it.
This guide walks the pillars in the order you'll typically wire them up.

## 1. Context + backend

```cpp
#include <threadmaxx_input/threadmaxx_input.hpp>
using namespace threadmaxx::input;

GlfwBackend backend;             // or NullBackend / your own IInputBackend
InputContext ctx;
ctx.setBackend(&backend);
ctx.reserveEvents(256);          // bypasses first-frame buffer growth
backend.reserve(256);
```

The backend is borrowed — the host owns the lifetime. `NullBackend`
exists for tests and as the substrate for `InputTrace::replay`.

## 2. Per-frame loop

```cpp
// Push raw events into the backend from your window callbacks.
backend.pushGlfwKey(GLFW_KEY_W, scancode, action, mods);
backend.pushGlfwCursorPos(xpos, ypos);

// Then once per game tick:
ctx.beginFrame(deltaSeconds);

if (ctx.isHeld(Key::W))           moveForward();
if (ctx.wasPressed(Key::Space))   jump();
if (ctx.wasReleased(MouseButton::Left)) finishClick();

ctx.endFrame();
```

`beginFrame` drains the backend, applies events to `InputState`, and
derives press/release edges as the XOR diff against the previous frame.
`endFrame` advances the frame index.

## 3. Actions and bindings

```cpp
BindingSet bs;
bs.bind("Jump",    Binding::key(Key::Space));
bs.bind("Jump",    Binding::gamepadButton(GamepadButton::A));
bs.bind("Save",    Binding::key(Key::S, Modifiers::Ctrl));
bs.bind("Forward", Binding::gamepadAxisPositive(GamepadAxis::LStickY, 0.3f));

ctx.setBindings(bs);

if (ctx.action("Jump").pressed) doJump();
if (ctx.action("Save").pressed) saveScene();
const float forward = ctx.action("Forward").value;  // 0..1
```

Action queries return `ActionTrigger { held, pressed, released, value }`.
Multi-source bindings OR for `held`; `pressed` only fires when no other
bound source was already held (no double-fire on chord activation).

**Modifier matching is exact**. `Ctrl+S` does NOT fire on `Shift+Ctrl+S`.
If you want "Ctrl regardless of Shift," bind both explicitly.

`actionId("Name")` is constexpr — keep the IDs in a header if you want
to bypass the string hash at the call site.

Binding sets serialize / deserialize through a tagged binary header —
load from disk on startup, write back on user rebind.

## 4. Gamepad

```cpp
if (ctx.isGamepadConnected(kGamepad0DeviceId)) {
    const auto leftStick = ctx.stickXY(kGamepad0DeviceId, Stick::Left);
    const float rightTrig = ctx.trigger(kGamepad0DeviceId, Trigger::Right);
    if (ctx.isHeld(kGamepad0DeviceId, GamepadButton::A)) ...
}
```

The library applies a radial deadzone to stick pairs (inner cuts a
non-circular dead band, outer maps to magnitude 1; direction preserved)
and a 1D threshold to triggers. Defaults in `config.hpp`; per-context
overrides via `ctx.setDeadzoneConfig({...})`.

`DeviceConnectEvent` / `DeviceDisconnectEvent` from the backend flip
`connected` and (on disconnect) clean the slot.

## 5. Cursor mode + capture

```cpp
// First-person camera: lock the cursor.
ctx.setCursorMode(CursorMode::Locked);
// Use ctx.state().mouse.dx / dy for look — absolute x/y is frozen.

// Switching back:
ctx.setCursorMode(CursorMode::Visible);

// Hand-off to the UI:
if (ctx.wantsMouse())    skipGameMouseHandling();
if (ctx.wantsKeyboard()) skipGameKeyboardHandling();
```

`setCaptureMouse(true)` / `setCaptureKeyboard(true)` are signals the
UI library sets; the host wires them into `ctx.setCaptureMouse(...)`
each frame. Capture is sticky — `endFrame` does NOT auto-release.

## 6. Picking

```cpp
Camera cam;
// ... fill cam.view + cam.projection (Vulkan-style NDC, column-major)
cam.viewportX = 0.0f;
cam.viewportY = 0.0f;
cam.viewportW = 1280.0f;
cam.viewportH = 720.0f;

const Ray r = screenToRay(cam, mouseX, mouseY);
// r.origin sits on the near plane; r.direction is unit length.

const auto sp = worldToScreen(cam, worldPoint);
if (sp.inFrontOfCamera) drawHud(sp.x, sp.y);
```

Use whatever scene query you want to convert the ray into a hit (BVH,
spatial hash, physics raycast). The library only builds the math.

## 7. UI bridge (opt-in)

```cpp
#if THREADMAXX_INPUT_HAS_UI_BRIDGE
#include <threadmaxx_input/ui_bridge.hpp>

// Each frame:
const auto uiIn = toUIInput(ctx);
uiCtx.setInput(uiIn);
uiCtx.beginFrame();
// ...
uiCtx.endFrame();

// Wire UI's capture signals back:
ctx.setCaptureMouse(uiCtx.wantsMouseCapture());
ctx.setCaptureKeyboard(uiCtx.wantsKeyboardCapture());
#endif
```

The bridge maps modifier and nav-key bits 1:1 (Tab+Shift → ShiftTab),
copies the char queue (ASCII fast-path; non-ASCII stamps `'?'`), and
rounds wheel Y to integer lines.

## 8. Replay

```cpp
// Record while you play:
InputTrace trace;
for (...) {
    ctx.beginFrame(dt);
    trace.recordCurrentFrame(ctx);
    ctx.endFrame();
}

// Persist / share:
auto bytes = trace.serialize();
saveFile("repro.tmin", bytes);

// Reproduce in a fresh process:
InputTrace replay;
replay.deserialize(loadFile("repro.tmin"));
for (std::uint64_t i = 0; i < replay.frameCount(); ++i) {
    replay.replayTo(backend, i);
    ctx.beginFrame(1.0f / 60.0f);
    ctx.endFrame();
}
```

`record` allocates on the per-frame vector grow; `replay` is zero-alloc
after warmup.

## 9. Common gotchas

- **`action("X").pressed` never fires** — check that you actually called
  `setBindings()`. Empty BindingSet → idle ActionTrigger.
- **Two bindings double-fire `pressed`** — they shouldn't (the library
  de-dups). If you see it, the two sources may have been released and
  re-acquired in the same frame; the XOR diff sees them as a single
  transition.
- **`Ctrl+S` doesn't fire on `Shift+Ctrl+S`** — by design. Bind both
  explicitly if you want either.
- **Cursor stuck after locking** — the host's `IInputBackend::setCursorMode`
  override is responsible for actually warping / hiding the OS cursor;
  `NullBackend` only records the call.
- **`InputTrace` round-trip drops events** — it shouldn't; check that
  you're reading the right slice (`replay.frame(i).size()`).
- **No-alloc gate fails after adding bindings mid-game** — `setBindings`
  copies into the context; do it during setup, not per-frame.
