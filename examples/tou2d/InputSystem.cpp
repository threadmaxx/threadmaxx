#include "InputSystem.hpp"

#include "Replay.hpp"

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Query.hpp>
#include <threadmaxx/World.hpp>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace tou2d {

namespace {

// M6.0 — read one slot's PlayerInput from a KeyMap. Replaces the
// hard-coded `kRows` table the v1 reader used. Defaults: every Action
// indexes into `keymap.binding[slot][action]`; a binding value of
// `kKeyUnbound` (0) means "no key" — glfwGetKey is skipped to avoid
// querying GLFW_KEY_UNKNOWN's -1 path. The pre-M6.0 reader called
// glfwGetKey unconditionally; results are equivalent because every
// default action has a real key bound.
PlayerInput readMapped(GLFWwindow* w,
                       const KeyMap& km,
                       std::uint8_t slot) noexcept {
    PlayerInput in;
    if (!w)                       return in;
    if (slot >= kMaxHumans)       return in;
    const auto& row = km.binding[slot];
    auto bit = [w, &row](Action a) -> std::uint8_t {
        const std::uint16_t key = row[static_cast<std::size_t>(a)];
        if (key == kKeyUnbound) return 0u;
        return (glfwGetKey(w, static_cast<int>(key)) == GLFW_PRESS) ? 1u : 0u;
    };
    in.thrust      = bit(Action::Thrust);
    in.back        = bit(Action::Back);
    in.turnLeft    = bit(Action::TurnLeft);
    in.turnRight   = bit(Action::TurnRight);
    in.fireBasic   = bit(Action::FireDumb);
    in.fireSpecial = bit(Action::FireSpecial);
    in.menuButton  = bit(Action::MenuButton);
    return in;
}

// The legacy `kRows` table is now reachable as the default KeyMap.
// Kept here so all GLFW key constants live in this TU (DemoTypes.hpp
// stays GLFW-free).
KeyMap buildDefaultKeyMap() noexcept {
    KeyMap km{};
    // P1 ↑↓←→ + RShift + RCtrl + /
    auto setRow = [&km](std::uint8_t slot,
                        int thrust, int back, int tl, int tr,
                        int fd, int fs, int menu) {
        auto& row = km.binding[slot];
        row[static_cast<std::size_t>(Action::Thrust)]      = static_cast<std::uint16_t>(thrust);
        row[static_cast<std::size_t>(Action::Back)]        = static_cast<std::uint16_t>(back);
        row[static_cast<std::size_t>(Action::TurnLeft)]    = static_cast<std::uint16_t>(tl);
        row[static_cast<std::size_t>(Action::TurnRight)]   = static_cast<std::uint16_t>(tr);
        row[static_cast<std::size_t>(Action::FireDumb)]    = static_cast<std::uint16_t>(fd);
        row[static_cast<std::size_t>(Action::FireSpecial)] = static_cast<std::uint16_t>(fs);
        row[static_cast<std::size_t>(Action::MenuButton)]  = static_cast<std::uint16_t>(menu);
    };
    setRow(0, GLFW_KEY_UP,    GLFW_KEY_DOWN,  GLFW_KEY_LEFT,  GLFW_KEY_RIGHT,
              GLFW_KEY_RIGHT_SHIFT, GLFW_KEY_RIGHT_CONTROL, GLFW_KEY_SLASH);
    setRow(1, GLFW_KEY_W,     GLFW_KEY_S,     GLFW_KEY_A,     GLFW_KEY_D,
              GLFW_KEY_LEFT_SHIFT,  GLFW_KEY_LEFT_CONTROL,  GLFW_KEY_TAB);
    setRow(2, GLFW_KEY_I,     GLFW_KEY_K,     GLFW_KEY_J,     GLFW_KEY_L,
              GLFW_KEY_Y,           GLFW_KEY_H,             GLFW_KEY_U);
    setRow(3, GLFW_KEY_KP_8,  GLFW_KEY_KP_2,  GLFW_KEY_KP_4,  GLFW_KEY_KP_6,
              GLFW_KEY_KP_0,        GLFW_KEY_KP_DECIMAL,    GLFW_KEY_KP_ENTER);

    // M6 UI navigation — global to slot 0 (only one menu cursor in v1).
    // Slots 1..3 leave UI actions unbound; M6.1's UISystem reads slot 0
    // for menu navigation regardless of which slot owns the menu.
    auto& ui = km.binding[0];
    ui[static_cast<std::size_t>(Action::Pause)]    = GLFW_KEY_ESCAPE;
    ui[static_cast<std::size_t>(Action::UiUp)]     = GLFW_KEY_UP;
    ui[static_cast<std::size_t>(Action::UiDown)]   = GLFW_KEY_DOWN;
    ui[static_cast<std::size_t>(Action::UiLeft)]   = GLFW_KEY_LEFT;
    ui[static_cast<std::size_t>(Action::UiRight)]  = GLFW_KEY_RIGHT;
    ui[static_cast<std::size_t>(Action::UiAccept)] = GLFW_KEY_ENTER;
    ui[static_cast<std::size_t>(Action::UiCancel)] = GLFW_KEY_ESCAPE;

    return km;
}

const KeyMap& defaultKeyMap() noexcept {
    static const KeyMap km = buildDefaultKeyMap();
    return km;
}

PlayerInput readKeys(GLFWwindow* w, std::uint8_t slot) noexcept {
    return readMapped(w, defaultKeyMap(), slot);
}

} // namespace

PlayerInput readKeyboardSlot(GLFWwindow* window, std::uint8_t slot) noexcept {
    return readKeys(window, slot);
}

KeyMap makeDefaultKeyMap() noexcept {
    return buildDefaultKeyMap();
}

PlayerInput readKeyboardSlotMapped(GLFWwindow* window,
                                   const KeyMap& keymap,
                                   std::uint8_t slot) noexcept {
    return readMapped(window, keymap, slot);
}

InputSystem::InputSystem(GLFWwindow* window, UserComponentIds ids) noexcept
    : window_(window), ids_(ids) {}

void InputSystem::preStep(threadmaxx::SystemContext& ctx) {
    if (!ids_.playerInput.valid() || !ids_.localPlayer.valid()) {
        return;
    }
    // M5.4 — replay-driven mode bypasses GLFW entirely. The host
    // (main.cpp) must have already called `replay_->advance()` BEFORE
    // this step. window_ may be null in replay mode.
    const bool replayDriven = (replay_ != nullptr);
    if (!replayDriven && !window_) {
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
                // Bot slots: keyboard path returns empty PlayerInput
                // (slot >= 4) which BotControlSystem overwrites later
                // in preStep. Replay path mirrors that — `inputs()`
                // returns the default PlayerInput when `slot` exceeds
                // the recorded human count.
                // N4 — settings-driven KeyMap. When `keymapInstalled_`
                // the per-slot bindings come from settings_.controls;
                // otherwise the static default (pre-N4 behaviour).
                const PlayerInput in = replayDriven
                    ? replay_->inputs(slot)
                    : keymapInstalled_
                        ? readMapped(window_, keymap_, slot)
                        : readKeys(window_, slot);
                threadmaxx::addUserComponent(cb, idsPi, entities[row], in);
            }
        }
    });
}

} // namespace tou2d
