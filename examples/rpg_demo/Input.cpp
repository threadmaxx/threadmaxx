#include "Input.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <atomic>
#include <cmath>

namespace rpg {

namespace {

InputState g_input;
double     g_lastMouseX     = 0.0;
double     g_lastMouseY     = 0.0;
bool       g_mouseInitialized = false;
// 2026-05-22 audit refactor — mouse-look delta accumulator. The
// GLFW cursor-position callback fires on the main thread; the
// sim-thread `pollContinuousInput` drains it once per frame. A
// plain atomic-bits-into-float pair is enough because only one
// thread writes at a time (main → drain), the read clears.
std::atomic<int64_t> g_mouseDx{0};
std::atomic<int64_t> g_mouseDy{0};

constexpr float kMouseSensitivity = 0.0025f;  // radians per pixel

void keyCallback(GLFWwindow* /*window*/, int key, int /*scancode*/,
                 int action, int /*mods*/) {
    if (action != GLFW_PRESS) return;
    std::uint32_t bit = 0;
    switch (key) {
        // System / debug keys.
        case GLFW_KEY_F5:  bit = kEdgeSaveQuick;    break;
        case GLFW_KEY_F9:  bit = kEdgeLoadQuick;    break;
        case GLFW_KEY_F1:  bit = kEdgeTrace;        break;
        case GLFW_KEY_F8:  bit = kEdgeSaveAsync;    break;
        case GLFW_KEY_F12: bit = kEdgeReloadShader; break;
        case GLFW_KEY_V:   bit = kEdgeAimToggle;    break;
        // Gameplay action keys (rpg_first_person_input_system_spec.md).
        case GLFW_KEY_F:     bit = kEdgeInteract;     break;
        case GLFW_KEY_SPACE: bit = kEdgeJump;         break;
        case GLFW_KEY_R:     bit = kEdgeCameraToggle; break;
        default: return;
    }
    g_input.edges.fetch_or(bit, std::memory_order_release);
}

void mouseButtonCallback(GLFWwindow* /*window*/, int button,
                         int action, int /*mods*/) {
    if (action != GLFW_PRESS) return;
    std::uint32_t bit = 0;
    switch (button) {
        case GLFW_MOUSE_BUTTON_LEFT:  bit = kEdgeAttack; break;
        case GLFW_MOUSE_BUTTON_RIGHT: bit = kEdgeBlock;  break;
        default: return;
    }
    g_input.edges.fetch_or(bit, std::memory_order_release);
}

void cursorPosCallback(GLFWwindow* /*window*/, double xpos, double ypos) {
    if (!g_mouseInitialized) {
        g_lastMouseX = xpos;
        g_lastMouseY = ypos;
        g_mouseInitialized = true;
        return;
    }
    const double dx = xpos - g_lastMouseX;
    const double dy = ypos - g_lastMouseY;
    g_lastMouseX = xpos;
    g_lastMouseY = ypos;
    // Accumulate as integer micro-pixels so multiple callback fires
    // between sim ticks all merge cleanly. Sensitivity is applied
    // when the sim drains.
    g_mouseDx.fetch_add(static_cast<int64_t>(dx * 1000.0),
                        std::memory_order_release);
    g_mouseDy.fetch_add(static_cast<int64_t>(dy * 1000.0),
                        std::memory_order_release);
}

void scrollCallback(GLFWwindow* /*window*/, double /*xoff*/, double yoff) {
    // Negative = zoom in (closer); we treat positive scroll as zoom-in.
    g_input.zoomDelta += -static_cast<float>(yoff) * 0.5f;
}

void focusCallback(GLFWwindow* /*window*/, int focused) {
    // Reset mouse-look state on focus change so the cursor doesn't
    // jump on next focus-in. Matches the spec's "Mouse look must
    // not drift when the game window loses focus" requirement.
    if (focused == GLFW_FALSE) {
        g_mouseInitialized = false;
        g_mouseDx.store(0, std::memory_order_release);
        g_mouseDy.store(0, std::memory_order_release);
    }
}

} // namespace

InputState& input() { return g_input; }

void installInputCallbacks(GLFWwindow* window) {
    glfwSetKeyCallback(window, keyCallback);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    glfwSetCursorPosCallback(window, cursorPosCallback);
    glfwSetScrollCallback(window, scrollCallback);
    glfwSetWindowFocusCallback(window, focusCallback);
    // 2026-05-22 audit refactor — lock the cursor for FPS-style
    // mouse look. Esc still exits the demo via the main loop.
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    if (glfwRawMouseMotionSupported()) {
        glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    }
    g_mouseInitialized = false;
    g_mouseDx.store(0, std::memory_order_release);
    g_mouseDy.store(0, std::memory_order_release);
}

void pollContinuousInput(GLFWwindow* window, double /*dtSeconds*/) {
    // ---- Movement axes (player-local, rotated to world by yaw in
    // PlayerInputSystem). W/Up = forward; S/Down = back; A/Left =
    // strafe left; D/Right = strafe right.
    float fwd = 0.0f, str = 0.0f;
    if (glfwGetKey(window, GLFW_KEY_W)     == GLFW_PRESS) fwd += 1.0f;
    if (glfwGetKey(window, GLFW_KEY_S)     == GLFW_PRESS) fwd -= 1.0f;
    if (glfwGetKey(window, GLFW_KEY_UP)    == GLFW_PRESS) fwd += 1.0f;
    if (glfwGetKey(window, GLFW_KEY_DOWN)  == GLFW_PRESS) fwd -= 1.0f;
    if (glfwGetKey(window, GLFW_KEY_D)     == GLFW_PRESS) str += 1.0f;
    if (glfwGetKey(window, GLFW_KEY_A)     == GLFW_PRESS) str -= 1.0f;
    if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) str += 1.0f;
    if (glfwGetKey(window, GLFW_KEY_LEFT)  == GLFW_PRESS) str -= 1.0f;
    // Diagonal-speed clamp (spec acceptance criterion).
    const float mag2 = fwd * fwd + str * str;
    if (mag2 > 1.0f) {
        const float inv = 1.0f / std::sqrt(mag2);
        fwd *= inv;
        str *= inv;
    }
    g_input.forward = fwd;
    g_input.strafe  = str;

    // ---- Camera yaw / pitch deltas (per-tick, additive).
    //
    // Mouse look is the primary source — pull the accumulated
    // micropixel deltas and convert to radians. Q/E are an
    // accessibility fallback that adds a constant rate per tick
    // (held-key turn). Both feed the SAME yaw accumulator in
    // CameraSystem so they compose without ordering surprises.
    const int64_t mdx = g_mouseDx.exchange(0, std::memory_order_acq_rel);
    const int64_t mdy = g_mouseDy.exchange(0, std::memory_order_acq_rel);
    float yawD   = static_cast<float>(mdx) * (0.001f * kMouseSensitivity);
    float pitchD = static_cast<float>(mdy) * (0.001f * kMouseSensitivity);
    if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) yawD -= 0.030f;
    if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) yawD += 0.030f;
    g_input.yawDelta   = yawD;
    g_input.pitchDelta = pitchD;
    // Zoom: scroll wheel only (set by the GLFW scroll callback).
    // Keyboard zoom keys removed to free Q/E for camera yaw.
}

} // namespace rpg
