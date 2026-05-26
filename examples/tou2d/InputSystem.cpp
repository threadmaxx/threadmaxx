#include "InputSystem.hpp"

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Query.hpp>
#include <threadmaxx/World.hpp>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace tou2d {

namespace {

PlayerInput readKeysP1(GLFWwindow* w) noexcept {
    PlayerInput in;
    in.thrust      = (glfwGetKey(w, GLFW_KEY_UP)        == GLFW_PRESS) ? 1u : 0u;
    in.back        = (glfwGetKey(w, GLFW_KEY_DOWN)      == GLFW_PRESS) ? 1u : 0u;
    in.turnLeft    = (glfwGetKey(w, GLFW_KEY_LEFT)      == GLFW_PRESS) ? 1u : 0u;
    in.turnRight   = (glfwGetKey(w, GLFW_KEY_RIGHT)     == GLFW_PRESS) ? 1u : 0u;
    in.fireBasic   = (glfwGetKey(w, GLFW_KEY_RIGHT_SHIFT)   == GLFW_PRESS) ? 1u : 0u;
    in.fireSpecial = (glfwGetKey(w, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS) ? 1u : 0u;
    in.menuButton  = (glfwGetKey(w, GLFW_KEY_SLASH)     == GLFW_PRESS) ? 1u : 0u;
    return in;
}

} // namespace

InputSystem::InputSystem(GLFWwindow* window, UserComponentIds ids) noexcept
    : window_(window), ids_(ids) {}

void InputSystem::preStep(threadmaxx::SystemContext& ctx) {
    if (!window_ || !ids_.playerInput.valid() || !ids_.localPlayer.valid()) {
        return;
    }
    const PlayerInput p1 = readKeysP1(window_);
    const auto idsPi = ids_.playerInput;
    const auto idsLp = ids_.localPlayer;

    // Single-job preStep — at most a handful of LocalPlayer entities in
    // M1 (one). Walking chunks via worldView keeps this allocation-free
    // even when M3 brings 2-4 players.
    ctx.single([&](threadmaxx::Range /*r*/, threadmaxx::CommandBuffer& cb) {
        const auto& view = ctx.worldView();
        const auto chunkPtrs = view.chunks();
        for (std::size_t ci = 0, n = chunkPtrs.size(); ci < n; ++ci) {
            const auto* chunk = chunkPtrs[ci];
            if (!chunk) continue;
            if (!chunk->mask.has(idsLp.componentBit())) continue;
            // PlayerInput presence is optional on the first tick — we
            // attach it via addUserComponent regardless. The presence
            // bit guards the read-side; the write-side always emits.

            const auto lpSpan = threadmaxx::user::chunkSpan<LocalPlayer>(*chunk, idsLp);
            const auto entities = chunk->entities;
            for (std::size_t row = 0; row < entities.size(); ++row) {
                // M1 wires P1 only; other slots stay neutral.
                if (lpSpan[row].slot == 0) {
                    threadmaxx::addUserComponent(cb, idsPi, entities[row], p1);
                } else {
                    threadmaxx::addUserComponent(cb, idsPi, entities[row], PlayerInput{});
                }
            }
        }
    });
}

} // namespace tou2d
