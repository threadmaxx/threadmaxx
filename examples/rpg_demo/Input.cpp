#include "Input.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <atomic>
#include <cmath>
#include <cstdint>

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

// 2026-05-22 audit (round 2) — per-axis last-press counter. The
// keyCallback fires for every press (rising edge); we stamp the
// pressed key with a monotonically increasing counter so
// pollContinuousInput can ask "which forward / strafe key was
// pressed most recently and is still held?". This gives us
// "non-stackable" WASD: holding W then tapping S immediately
// reverses direction (S wins because it was pressed last), and
// releasing S returns to W rather than zero. Releasing W while S
// is held stays at S.
//
// Each press increments a global counter and writes it to the
// per-key slot. Pollers read the per-key counter; the larger value
// is the more recent press. Wrap-around is fine for our purposes —
// reach is in the trillions of ticks.
std::atomic<std::uint64_t> g_pressCounter{0};
std::atomic<std::uint64_t> g_pressTimeW{0};
std::atomic<std::uint64_t> g_pressTimeS{0};
std::atomic<std::uint64_t> g_pressTimeUp{0};
std::atomic<std::uint64_t> g_pressTimeDown{0};
std::atomic<std::uint64_t> g_pressTimeA{0};
std::atomic<std::uint64_t> g_pressTimeD{0};
std::atomic<std::uint64_t> g_pressTimeLeft{0};
std::atomic<std::uint64_t> g_pressTimeRight{0};

// 2026-05-22 audit (round 2) — signs flipped. User reported that
// the mouse and Q/E yaw were "opposite to what I want". Forward in
// PlayerInputSystem is `(-sin(yaw), sp, -cos(yaw))`, so positive
// yaw rotates the view to look LEFT (toward -X). The natural
// expectation is "mouse right → look right", which means a
// positive mouse-delta should decrease yaw. Flipped accordingly.
constexpr float kMouseSensitivity = -0.0025f;  // radians per pixel (sign flipped)
constexpr float kKeyYawRate       =  0.030f;   // base step before sign

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
        case GLFW_KEY_LEFT_SHIFT:
        case GLFW_KEY_RIGHT_SHIFT:
            bit = kEdgeSprint;
            break;
        // Movement keys: stamp the press counter for non-stack
        // tie-break. No edge bit emitted — these are polled axes.
        case GLFW_KEY_W:
            g_pressTimeW.store(
                g_pressCounter.fetch_add(1, std::memory_order_acq_rel) + 1,
                std::memory_order_release);
            return;
        case GLFW_KEY_S:
            g_pressTimeS.store(
                g_pressCounter.fetch_add(1, std::memory_order_acq_rel) + 1,
                std::memory_order_release);
            return;
        case GLFW_KEY_UP:
            g_pressTimeUp.store(
                g_pressCounter.fetch_add(1, std::memory_order_acq_rel) + 1,
                std::memory_order_release);
            return;
        case GLFW_KEY_DOWN:
            g_pressTimeDown.store(
                g_pressCounter.fetch_add(1, std::memory_order_acq_rel) + 1,
                std::memory_order_release);
            return;
        case GLFW_KEY_A:
            g_pressTimeA.store(
                g_pressCounter.fetch_add(1, std::memory_order_acq_rel) + 1,
                std::memory_order_release);
            return;
        case GLFW_KEY_D:
            g_pressTimeD.store(
                g_pressCounter.fetch_add(1, std::memory_order_acq_rel) + 1,
                std::memory_order_release);
            return;
        case GLFW_KEY_LEFT:
            g_pressTimeLeft.store(
                g_pressCounter.fetch_add(1, std::memory_order_acq_rel) + 1,
                std::memory_order_release);
            return;
        case GLFW_KEY_RIGHT:
            g_pressTimeRight.store(
                g_pressCounter.fetch_add(1, std::memory_order_acq_rel) + 1,
                std::memory_order_release);
            return;
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

// 2026-05-22 audit (round 2) — resolve a +/- axis pair into a
// single -1 / 0 / +1 sign with "most recently pressed of the held
// keys wins". `posHeld` / `negHeld` are bool snapshots of
// `glfwGetKey == GLFW_PRESS` for the two ends; `posStamp` /
// `negStamp` are the per-key press counters.
float resolveAxisPair(bool posHeld, std::uint64_t posStamp,
                      bool negHeld, std::uint64_t negStamp) noexcept {
    if (posHeld && negHeld) {
        return (posStamp >= negStamp) ? 1.0f : -1.0f;
    }
    if (posHeld) return  1.0f;
    if (negHeld) return -1.0f;
    return 0.0f;
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
    // PlayerInputSystem). 2026-05-22 audit (round 2) — non-stacking:
    // pressing W then tapping S without releasing W now flips to
    // backward (S wins because it was pressed later). Resolved via
    // the per-key press counter stamped by keyCallback.
    const bool wHeld     = glfwGetKey(window, GLFW_KEY_W)     == GLFW_PRESS;
    const bool sHeld     = glfwGetKey(window, GLFW_KEY_S)     == GLFW_PRESS;
    const bool upHeld    = glfwGetKey(window, GLFW_KEY_UP)    == GLFW_PRESS;
    const bool downHeld  = glfwGetKey(window, GLFW_KEY_DOWN)  == GLFW_PRESS;
    const bool aHeld     = glfwGetKey(window, GLFW_KEY_A)     == GLFW_PRESS;
    const bool dHeld     = glfwGetKey(window, GLFW_KEY_D)     == GLFW_PRESS;
    const bool leftHeld  = glfwGetKey(window, GLFW_KEY_LEFT)  == GLFW_PRESS;
    const bool rightHeld = glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS;

    // Each axis: collapse W/Up into a single "forward+" slot using
    // the more recent of the two press stamps. Same for S/Down,
    // A/Left, D/Right. Then resolve the +/- pair.
    const std::uint64_t tW   = g_pressTimeW.load(std::memory_order_acquire);
    const std::uint64_t tS   = g_pressTimeS.load(std::memory_order_acquire);
    const std::uint64_t tUp  = g_pressTimeUp.load(std::memory_order_acquire);
    const std::uint64_t tDn  = g_pressTimeDown.load(std::memory_order_acquire);
    const std::uint64_t tA   = g_pressTimeA.load(std::memory_order_acquire);
    const std::uint64_t tD   = g_pressTimeD.load(std::memory_order_acquire);
    const std::uint64_t tL   = g_pressTimeLeft.load(std::memory_order_acquire);
    const std::uint64_t tR   = g_pressTimeRight.load(std::memory_order_acquire);

    const bool fwdPosHeld = wHeld || upHeld;
    const bool fwdNegHeld = sHeld || downHeld;
    const std::uint64_t fwdPosStamp = (tW > tUp ? tW : tUp);
    const std::uint64_t fwdNegStamp = (tS > tDn ? tS : tDn);
    const bool strPosHeld = dHeld || rightHeld;
    const bool strNegHeld = aHeld || leftHeld;
    const std::uint64_t strPosStamp = (tD > tR ? tD : tR);
    const std::uint64_t strNegStamp = (tA > tL ? tA : tL);

    const float fwd = resolveAxisPair(fwdPosHeld, fwdPosStamp,
                                      fwdNegHeld, fwdNegStamp);
    const float str = resolveAxisPair(strPosHeld, strPosStamp,
                                      strNegHeld, strNegStamp);
    // Diagonal-speed clamp (spec acceptance criterion).
    float fwdOut = fwd, strOut = str;
    const float mag2 = fwdOut * fwdOut + strOut * strOut;
    if (mag2 > 1.0f) {
        const float inv = 1.0f / std::sqrt(mag2);
        fwdOut *= inv;
        strOut *= inv;
    }
    g_input.forward = fwdOut;
    g_input.strafe  = strOut;
    // PlayerInputSystem reads this to decide whether sprint can
    // activate / persist (W/Up must still be the active forward
    // direction — strafing or going backwards cancels sprint).
    g_input.forwardKeyHeld = (fwdOut > 0.0f && (wHeld || upHeld)) ? 1u : 0u;

    // ---- Camera yaw / pitch deltas (per-tick, additive).
    //
    // 2026-05-22 audit (round 2) — Mouse-X and Q/E signs flipped so
    // "mouse right / E → look right". Q now turns left, E turns
    // right.
    // 2026-05-22 audit (round 3) — Mouse-Y also flipped per user
    // feedback. Mouse up (mdy < 0) now produces a positive pitchD,
    // raising `pitchRadians` and tilting the view up. Inline negation
    // of `kMouseSensitivity` here keeps the yaw sign convention
    // unchanged.
    const int64_t mdx = g_mouseDx.exchange(0, std::memory_order_acq_rel);
    const int64_t mdy = g_mouseDy.exchange(0, std::memory_order_acq_rel);
    float yawD   = static_cast<float>(mdx) * (0.001f *  kMouseSensitivity);
    float pitchD = static_cast<float>(mdy) * (0.001f * -kMouseSensitivity);
    if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) yawD += kKeyYawRate;
    if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) yawD -= kKeyYawRate;
    g_input.yawDelta   = yawD;
    g_input.pitchDelta = pitchD;
    // Zoom: scroll wheel only (set by the GLFW scroll callback).
    // Keyboard zoom keys removed to free Q/E for camera yaw.
}

} // namespace rpg
