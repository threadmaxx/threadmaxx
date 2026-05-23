#pragma once

#include <atomic>
#include <cstdint>

struct GLFWwindow;

namespace rpg {

/// Edge-triggered input bits — one tick per press, cleared on consume.
///
/// 2026-05-22 audit refactor — extended to match the RPG control spec
/// (rpg_first_person_input_system_spec.md):
///   * Attack moved from F to Mouse Left.
///   * Mouse Right → block (new gameplay slot).
///   * F → interact (new gameplay slot).
///   * Space → jump.
///   * R → toggle first/third-person camera.
enum : std::uint32_t {
    kEdgeSaveQuick     = 1u << 0,   // F5      — sync save
    kEdgeLoadQuick     = 1u << 1,   // F9      — load
    kEdgeTrace         = 1u << 2,   // F1      — toggle Chrome-trace
    kEdgeAttack        = 1u << 3,   // Mouse L — primary action (sword swing)
    kEdgeSaveAsync     = 1u << 4,   // F8      — async save (batch D3)
    kEdgeReloadShader  = 1u << 5,   // F12     — emit AssetReloaded (batch D7)
    kEdgeAimToggle     = 1u << 6,   // V       — toggle aim PIP visibility
    kEdgeBlock         = 1u << 7,   // Mouse R — secondary action (block)
    kEdgeInteract      = 1u << 8,   // F       — interact with nearby target
    kEdgeJump          = 1u << 9,   // Space   — jump when grounded
    kEdgeCameraToggle  = 1u << 10,  // R       — toggle first/third-person
    kEdgeSprint        = 1u << 11,  // LShift  — sprint trigger (requires W/Up held)
    /// §3.11 batch D11 — voxel harvest. The GAME_EXTENSION.md spec
    /// originally maps these to Mouse L / R, but those slots are taken
    /// by attack + parry (kEdgeAttack, kEdgeBlock). Using dedicated
    /// keys keeps combat undisturbed; a later batch can layer a "build
    /// mode" toggle that re-routes the mouse buttons.
    kEdgeBreakBlock    = 1u << 12,  // G — break the targeted block
    kEdgePlaceBlock    = 1u << 13,  // H — place a block from inventory
};

/// Polled-per-tick analog inputs + edge bits. Filled in by the main
/// thread's GLFW callbacks; consumed by systems on the sim thread.
struct InputState {
    /// Movement axes: forward (-1..1) and strafe (-1..1).
    float forward = 0.0f;
    float strafe  = 0.0f;
    /// 2026-05-22 audit (round 2) — non-zero while W or Up is the
    /// active forward direction (i.e. forward axis = +1). Used by
    /// PlayerInputSystem to gate sprint activation/maintenance.
    /// Strafe / backward inputs cannot sprint.
    std::uint32_t forwardKeyHeld = 0u;

    /// Camera control: yaw / pitch deltas in radians.
    float yawDelta   = 0.0f;
    float pitchDelta = 0.0f;
    /// Zoom delta in world units (positive = farther).
    float zoomDelta  = 0.0f;

    /// Bitmask of edge-triggered actions. Read-and-clear on consume.
    std::atomic<std::uint32_t> edges{0};
};

/// Singleton accessor. Populated by GLFW callbacks; one read per tick
/// from sim-thread systems. Move-only construction would be cleaner but
/// the GLFW callback signature forces a global anyway.
InputState& input();

/// Install GLFW callbacks against the provided window. Idempotent.
void installInputCallbacks(GLFWwindow* window);

/// Refresh continuous-state axes (forward/strafe/yawDelta/etc.) from
/// the window's current key + mouse state. Call once per frame on the
/// main thread between glfwPollEvents() and engine.step().
void pollContinuousInput(GLFWwindow* window, double dtSeconds);

/// Atomically take the edge bits and return them, clearing the field.
inline std::uint32_t takeEdges() noexcept {
    return input().edges.exchange(0, std::memory_order_acq_rel);
}

} // namespace rpg
