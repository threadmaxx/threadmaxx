#include "Input.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace rpg {

namespace {

InputState g_input;
double     g_lastMouseX     = 0.0;
double     g_lastMouseY     = 0.0;
bool       g_mouseInitialized = false;

void keyCallback(GLFWwindow* /*window*/, int key, int /*scancode*/,
                 int action, int /*mods*/) {
    if (action != GLFW_PRESS) return;
    std::uint32_t bit = 0;
    switch (key) {
        case GLFW_KEY_F5: bit = kEdgeSaveQuick; break;
        case GLFW_KEY_F9: bit = kEdgeLoadQuick; break;
        case GLFW_KEY_F1: bit = kEdgeTrace; break;
        case GLFW_KEY_F:  bit = kEdgeAttack; break;
        default: return;
    }
    g_input.edges.fetch_or(bit, std::memory_order_release);
}

void scrollCallback(GLFWwindow* /*window*/, double /*xoff*/, double yoff) {
    // Negative = zoom in (closer); we treat positive scroll as zoom-in.
    g_input.zoomDelta += -static_cast<float>(yoff) * 0.5f;
}

} // namespace

InputState& input() { return g_input; }

void installInputCallbacks(GLFWwindow* window) {
    glfwSetKeyCallback(window, keyCallback);
    glfwSetScrollCallback(window, scrollCallback);
    g_mouseInitialized = false;
}

void pollContinuousInput(GLFWwindow* window, double /*dtSeconds*/) {
    // Continuous keys for movement + camera.
    float fwd = 0.0f, str = 0.0f;
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) fwd += 1.0f;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) fwd -= 1.0f;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) str += 1.0f;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) str -= 1.0f;
    g_input.forward = fwd;
    g_input.strafe  = str;

    // Camera yaw/pitch via arrow keys (more reliable across the
    // smoke than mouse capture; mouse mode is a follow-on).
    float yawD = 0.0f, pitchD = 0.0f;
    if (glfwGetKey(window, GLFW_KEY_LEFT)  == GLFW_PRESS) yawD   -= 1.5f;
    if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) yawD   += 1.5f;
    if (glfwGetKey(window, GLFW_KEY_UP)    == GLFW_PRESS) pitchD -= 1.0f;
    if (glfwGetKey(window, GLFW_KEY_DOWN)  == GLFW_PRESS) pitchD += 1.0f;
    // Scale by fixed step (60 Hz target); the caller integrates per tick.
    g_input.yawDelta   = yawD   * 0.016f;
    g_input.pitchDelta = pitchD * 0.016f;

    // Q / E zoom.
    float zoomD = 0.0f;
    if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) zoomD -= 4.0f * 0.016f;
    if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) zoomD += 4.0f * 0.016f;
    // Add to whatever the scroll callback may have already deposited.
    g_input.zoomDelta += zoomD;

    // Track mouse only to make installInputCallbacks idempotent — full
    // mouse-look is a follow-on.
    if (!g_mouseInitialized) {
        glfwGetCursorPos(window, &g_lastMouseX, &g_lastMouseY);
        g_mouseInitialized = true;
    }
}

} // namespace rpg
