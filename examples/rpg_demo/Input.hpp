#pragma once

#include <atomic>
#include <cstdint>

struct GLFWwindow;

namespace rpg {

/// Edge-triggered input bits — one tick per press, cleared on consume.
enum : std::uint32_t {
    kEdgeSaveQuick    = 1u << 0,   // F5  — sync save
    kEdgeLoadQuick    = 1u << 1,   // F9  — load
    kEdgeTrace        = 1u << 2,   // F1  — toggle Chrome-trace
    kEdgeAttack       = 1u << 3,   // F   — sword swing
    kEdgeSaveAsync    = 1u << 4,   // F8  — async save (batch D3)
    kEdgeReloadShader = 1u << 5,   // F12 — emit AssetReloaded (batch D7)
};

/// Polled-per-tick analog inputs + edge bits. Filled in by the main
/// thread's GLFW callbacks; consumed by systems on the sim thread.
struct InputState {
    /// Movement axes: forward (-1..1) and strafe (-1..1).
    float forward = 0.0f;
    float strafe  = 0.0f;

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
