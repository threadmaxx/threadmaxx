#include "InputSystem.hpp"

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Query.hpp>
#include <threadmaxx/World.hpp>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace tou2d {

namespace {

struct KeyRow {
    int thrust;
    int back;
    int turnLeft;
    int turnRight;
    int fireBasic;
    int fireSpecial;
    int menuButton;
};

// P1 ↑↓←→ + RShift + RCtrl + /
// P2 W/S/A/D + LShift + LCtrl + Tab
// P3 I/K/J/L + Y + H + U
// P4 numpad 8/2/4/6 + KP_0 + KP_DECIMAL + KP_ENTER
constexpr KeyRow kRows[4] = {
    {GLFW_KEY_UP,    GLFW_KEY_DOWN,  GLFW_KEY_LEFT,  GLFW_KEY_RIGHT,
     GLFW_KEY_RIGHT_SHIFT, GLFW_KEY_RIGHT_CONTROL, GLFW_KEY_SLASH},
    {GLFW_KEY_W,     GLFW_KEY_S,     GLFW_KEY_A,     GLFW_KEY_D,
     GLFW_KEY_LEFT_SHIFT,  GLFW_KEY_LEFT_CONTROL,  GLFW_KEY_TAB},
    {GLFW_KEY_I,     GLFW_KEY_K,     GLFW_KEY_J,     GLFW_KEY_L,
     GLFW_KEY_Y,           GLFW_KEY_H,             GLFW_KEY_U},
    {GLFW_KEY_KP_8,  GLFW_KEY_KP_2,  GLFW_KEY_KP_4,  GLFW_KEY_KP_6,
     GLFW_KEY_KP_0,        GLFW_KEY_KP_DECIMAL,    GLFW_KEY_KP_ENTER},
};

PlayerInput readKeys(GLFWwindow* w, std::uint8_t slot) noexcept {
    PlayerInput in;
    if (slot >= 4) return in;
    const KeyRow& r = kRows[slot];
    in.thrust      = (glfwGetKey(w, r.thrust)      == GLFW_PRESS) ? 1u : 0u;
    in.back        = (glfwGetKey(w, r.back)        == GLFW_PRESS) ? 1u : 0u;
    in.turnLeft    = (glfwGetKey(w, r.turnLeft)    == GLFW_PRESS) ? 1u : 0u;
    in.turnRight   = (glfwGetKey(w, r.turnRight)   == GLFW_PRESS) ? 1u : 0u;
    in.fireBasic   = (glfwGetKey(w, r.fireBasic)   == GLFW_PRESS) ? 1u : 0u;
    in.fireSpecial = (glfwGetKey(w, r.fireSpecial) == GLFW_PRESS) ? 1u : 0u;
    in.menuButton  = (glfwGetKey(w, r.menuButton)  == GLFW_PRESS) ? 1u : 0u;
    return in;
}

} // namespace

InputSystem::InputSystem(GLFWwindow* window, UserComponentIds ids) noexcept
    : window_(window), ids_(ids) {}

void InputSystem::preStep(threadmaxx::SystemContext& ctx) {
    if (!window_ || !ids_.playerInput.valid() || !ids_.localPlayer.valid()) {
        return;
    }
    // M4.2 — round over, freeze keyboard input. BotControlSystem also
    // checks the same flag so neither side keeps writing PlayerInput.
    // Ships drift to rest under existing damping; bullets in flight
    // continue to resolve to their final hit before the world idles.
    if (roundEnded_ && roundEnded_->load(std::memory_order_acquire)) {
        return;
    }
    const auto idsPi = ids_.playerInput;
    const auto idsLp = ids_.localPlayer;

    ctx.single([&](threadmaxx::Range /*r*/, threadmaxx::CommandBuffer& cb) {
        const auto& view = ctx.worldView();
        const auto chunkPtrs = view.chunks();
        for (std::size_t ci = 0, n = chunkPtrs.size(); ci < n; ++ci) {
            const auto* chunk = chunkPtrs[ci];
            if (!chunk) continue;
            if (!chunk->mask.has(idsLp.componentBit())) continue;

            const auto lpSpan = threadmaxx::user::chunkSpan<LocalPlayer>(*chunk, idsLp);
            const auto entities = chunk->entities;
            for (std::size_t row = 0; row < entities.size(); ++row) {
                const std::uint8_t slot = lpSpan[row].slot;
                const PlayerInput in = readKeys(window_, slot);
                threadmaxx::addUserComponent(cb, idsPi, entities[row], in);
            }
        }
    });
}

} // namespace tou2d
