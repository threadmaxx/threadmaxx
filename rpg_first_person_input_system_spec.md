# RPG Player Input System Design and Requirements

## Goal

Design a responsive, deterministic, and easy-to-extend first-player input system for an RPG. The input system must support movement, camera control, combat actions, interaction, jump, and camera mode switching while keeping player facing, movement direction, and camera behavior consistent.

The system should support both first-person and third-person gameplay.

## Core controls

### Movement
- `W` / `Up` — move forward relative to the player’s current facing direction
- `S` / `Down` — move backward relative to the player’s current facing direction
- `A` / `Left` — strafe left relative to the player’s current facing direction
- `D` / `Right` — strafe right relative to the player’s current facing direction

### Combat / actions
- `Mouse Left` — attack / action slot 1
- `Mouse Right` — block / action slot 2
- `F` — interact
- `Space` — jump

### Camera
- `R` — toggle camera mode between first-person and third-person
- `Mouse movement` — rotate camera; in gameplay modes the player yaw follows the camera yaw
- `Mouse scroll` — zoom camera in / out in third-person mode
- `Q` — turn player left and camera follows
- `E` — turn player right and camera follows

## Required behavior

### Movement relative to facing
Movement must always be interpreted in the player’s current facing frame, not the world frame.

That means:
- forward/back is along the player’s current yaw
- left/right is perpendicular to the player’s current yaw
- movement direction must update immediately when the player rotates

### Camera and player rotation coupling
- Mouse movement changes camera aim
- Player yaw follows camera yaw in both first-person and third-person gameplay
- `Q` and `E` provide discrete turn input and must rotate both player and camera together
- Camera must remain aligned with the player orientation unless a temporary state explicitly overrides it

### First-person mode
- Camera is placed at the player head / eye height
- Camera rotation directly drives the player’s facing direction
- Body visibility may be hidden or simplified
- Zoom is ignored or clamped to a very small range if supported

### Third-person mode
- Camera orbits behind the player
- Player facing still tracks camera yaw
- Mouse scroll adjusts camera distance within min/max zoom limits
- Camera should avoid clipping into geometry if collision handling is available

### Combat and interaction
- `Mouse Left` must trigger the primary combat action
- `Mouse Right` must trigger the secondary combat action / block
- `F` must activate nearby interactables when in range
- Actions should be edge-triggered where appropriate so one key press does not repeatedly fire unless held behavior is intentionally desired

### Jumping
- `Space` triggers jump only when the player is grounded or otherwise allowed to jump
- Jump input should not be lost if it occurs during a small grace window before landing, if the game supports coyote time or jump buffering

## Input model

The system should separate raw input from gameplay intent.

Recommended layers:

1. **Raw input**
   - keyboard state
   - mouse motion
   - mouse buttons
   - mouse wheel

2. **Mapped actions**
   - move forward/back/left/right
   - turn left/right
   - jump
   - interact
   - attack
   - block
   - switch camera mode
   - camera zoom
   - camera look

3. **Player intent**
   - desired movement vector
   - desired rotation / yaw delta
   - desired action flags
   - desired camera mode
   - desired zoom target

4. **Gameplay state update**
   - movement controller
   - animation controller
   - camera controller
   - combat controller
   - interaction controller

## Requirements

### Functional requirements
- Keyboard and mouse inputs must be fully configurable through a binding table
- Movement must remain correct after camera toggles
- Mouse look must continuously adjust yaw and pitch as needed
- Player yaw must follow camera yaw
- First-person and third-person modes must be switchable at runtime
- Third-person camera zoom must respect min/max bounds
- Input should work correctly at variable frame rates
- The system must support simultaneous movement and camera look
- The system must support simultaneous attack/block/interact where valid by game rules

### Non-functional requirements
- Low input latency
- Deterministic interpretation of input within a fixed tick when replayed
- Clear separation between input capture and gameplay execution
- Minimal per-frame allocation
- Stable behavior across keyboard layouts where practical
- Easy rebinding for accessibility and platform differences

## Suggested runtime state

```cpp
struct PlayerInputState {
    float moveForward = 0.0f;
    float moveRight = 0.0f;
    float turnYaw = 0.0f;
    float turnPitch = 0.0f;
    float zoomDelta = 0.0f;

    bool jumpPressed = false;
    bool interactPressed = false;
    bool attackPressed = false;
    bool blockPressed = false;
    bool toggleCameraPressed = false;

    bool mouseLookActive = true;
    bool isFirstPerson = true;
};
```

## Suggested control semantics

### Movement vector
- `W` adds positive forward movement
- `S` adds negative forward movement
- `A` adds negative right movement
- `D` adds positive right movement

The movement vector is then rotated by the player yaw to produce world-space motion.

### Turning
- Mouse movement updates yaw continuously
- `Q` and `E` apply discrete yaw rotation steps or a held turn rate
- Turning must update camera orientation and player orientation together

### Camera switching
- Pressing `R` toggles between first-person and third-person
- Switching should preserve current yaw and pitch where possible
- Third-person should restore the last valid camera distance or default zoom

### Mouse wheel
- In third-person mode, scroll adjusts camera distance
- In first-person mode, scroll may be ignored or repurposed only if explicitly desired

## Update order

Recommended per-tick order:

1. poll raw input
2. translate to player intent
3. apply camera rotation / zoom intent
4. derive movement direction from player yaw
5. resolve gameplay actions
6. feed movement and actions into animation / combat / interaction systems
7. publish camera pose for rendering

## Edge cases

- When switching camera modes, avoid sudden yaw or pitch jumps
- Mouse look must not drift when the game window loses focus
- Interact should only fire when a valid target is present
- Attack and block should respect cooldowns and stamina rules if present
- Jump should not trigger while in restricted states unless explicitly allowed
- Movement should clamp diagonal speed so pressing two movement keys does not create faster movement than intended

## Integration notes

This system should integrate cleanly with a fixed-step engine and a command-buffer commit model.

Recommended approach:
- capture input once per frame or tick
- store the interpreted result in a player control state object
- let gameplay systems read that state during update
- avoid directly mutating simulation state inside input polling code

## Optional future improvements

- configurable key rebinding
- gamepad support
- controller dead zones and stick curves
- aim assist
- camera collision and shoulder offset in third-person
- input buffering for combat combos
- accessibility presets
- action remapping UI

## Acceptance criteria

The implementation is complete when:
- movement follows player facing correctly
- mouse look updates camera and player rotation together
- `Q` / `E` turn the player and camera together
- `R` toggles first-person / third-person correctly
- scroll zoom works in third-person
- `Space`, `F`, `Mouse Left`, and `Mouse Right` trigger the correct gameplay actions
- input remains stable under a fixed-step simulation
- the system is easy to extend and rebinding-ready

