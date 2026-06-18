#pragma once

#include "DemoTypes.hpp"

#include <threadmaxx/System.hpp>

#include <atomic>
#include <memory>

struct GLFWwindow;

namespace tou2d {

class ReplayPlayer;

/// M5.4 — sample the keyboard state for slot `slot ∈ [0, 4)`. Returns
/// a zero-initialized PlayerInput for any out-of-range slot. Public so
/// main.cpp can capture inputs for the replay recorder using the SAME
/// reader the system uses — guarantees the recorded stream matches
/// what `InputSystem::preStep` reads on the very next step (both
/// happen between `glfwPollEvents()` calls, so the GLFW key state is
/// frozen). `window` may be null; result is zero in that case.
PlayerInput readKeyboardSlot(GLFWwindow* window, std::uint8_t slot) noexcept;

/// M6.0 — build the default KeyMap. Returns the original-TOU per-slot
/// bindings (P1=arrows + RShift/RCtrl + /, P2=WSAD + LShift/LCtrl + Tab,
/// P3=IJKL + Y/H/U, P4=numpad 8/2/4/6 + KP_0/KP_DECIMAL/KP_ENTER), plus
/// the UI-navigation set (UI is global per-binding; we still scope it
/// to slot 0 only at v1 since menus are single-user).
///
/// The KeyMap returned is forward-compatible with M6.5's
/// `settings.dat` (memcpy'd to disk; new fields would bump the
/// settings.dat version).
KeyMap makeDefaultKeyMap() noexcept;

/// M6.0 — sample PlayerInput from a KeyMap. Replaces the hard-coded
/// `kRows` table the v1 reader used internally. Public so the replay
/// recorder + test can share the exact same path.
PlayerInput readKeyboardSlotMapped(GLFWwindow* window,
                                   const KeyMap& keymap,
                                   std::uint8_t slot) noexcept;

/// preStep system that polls the borrowed GLFW window's key state on
/// the sim thread and writes the resulting PlayerInput value to every
/// LocalPlayer-tagged entity. M1 only wires P1 (arrows + RShift +
/// RCtrl + /); P2-P4 land in M3 when local multiplayer ships.
///
/// Engine contract:
///   * Lifetime: borrows the GLFWwindow*; the caller (main.cpp) owns
///     it and is responsible for keeping it alive until engine
///     shutdown.
///   * reads()  = none — InputSystem doesn't read live world state.
///   * writes() = UserData — placeholder so the wave scheduler keeps
///                it in a separate wave from MovementSystem. The
///                actual write target is a user component (PlayerInput);
///                the engine has no scheduling category for user
///                components, so we borrow UserData's bit as the
///                conservative declaration.
///   * Driven from preStep, where commits flush immediately on the
///     sim thread before the first wave.
class InputSystem : public threadmaxx::ISystem {
public:
    InputSystem(GLFWwindow* window, UserComponentIds ids) noexcept;

    const char*              name()   const noexcept override { return "tou2d.input"; }
    threadmaxx::ComponentSet reads()  const noexcept override { return threadmaxx::ComponentSet::none(); }
    threadmaxx::ComponentSet writes() const noexcept override { return threadmaxx::ComponentSet{threadmaxx::Component::UserData}; }

    void preStep(threadmaxx::SystemContext& ctx) override;
    void update (threadmaxx::SystemContext& /*ctx*/) override {}

    /// M4.2 — install the round-end shared latch. When set, preStep
    /// stops writing PlayerInput so neither thrust nor fire commands
    /// reach Movement/WeaponFire. Bot system holds the same flag.
    void setRoundEndedFlag(std::shared_ptr<std::atomic<bool>> f) noexcept {
        roundEnded_ = std::move(f);
    }

    /// M5.4 — when set, GLFW polling is bypassed and per-human inputs
    /// are pulled from the player instead. main.cpp is responsible for
    /// calling `replayPlayer->advance()` once per tick (BEFORE
    /// engine.step()) so the player's `inputs(slot)` reflects the
    /// current tick. The borrowed pointer must outlive the engine.
    void setReplayPlayer(ReplayPlayer* p) noexcept { replay_ = p; }

    /// N4 (2026-06-18) — install a custom KeyMap (typically loaded from
    /// settings.dat). When set, `preStep` polls GLFW against this map
    /// rather than the static `defaultKeyMap()`. Default-constructed
    /// (no override installed) → use `defaultKeyMap()` as before.
    /// Replay mode bypasses both paths.
    void setKeyMap(const KeyMap& km) noexcept {
        keymap_           = km;
        keymapInstalled_  = true;
    }

private:
    GLFWwindow*                          window_      = nullptr;
    UserComponentIds                     ids_;
    std::shared_ptr<std::atomic<bool>>   roundEnded_;
    ReplayPlayer*                        replay_      = nullptr;
    KeyMap                               keymap_{};       // N4 — used when keymapInstalled_
    bool                                 keymapInstalled_ = false;
};

} // namespace tou2d
