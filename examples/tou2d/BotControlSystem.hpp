#pragma once

#include "DemoTypes.hpp"

#include <threadmaxx/System.hpp>

namespace tou2d {

/// preStep system that overrides PlayerInput for ships with
/// `LocalPlayer::isBot != 0`. Registered AFTER InputSystem so the bot
/// writes win for those slots; human slots are untouched.
///
/// AI is intentionally minimal — TOU's gameplay loop is bullets vs
/// terrain vs each other; a smart bot would dwarf the rest of the
/// demo's code. This bot:
///   * Finds the nearest other live ship (any slot).
///   * Computes the angle to it; sets `turnLeft` / `turnRight` to
///     close the angular gap at the movement system's turn rate.
///   * Sets `thrust = 1` if vaguely facing target (within ±45°) AND
///     range > engagement distance (else lets gravity / inertia work).
///   * Sets `fireBasic = 1` if within ±10° AND within firing range.
///   * Uses `fireSpecial` ~10% of the time when in firing arc to spice
///     things up.
///
/// reads()  = Transform (other ships' positions + own orientation).
/// writes() = UserData (PlayerInput is a user component; same
///            conservative declaration InputSystem uses).
class BotControlSystem : public threadmaxx::ISystem {
public:
    explicit BotControlSystem(UserComponentIds ids) noexcept;

    const char*              name()   const noexcept override { return "tou2d.botControl"; }
    threadmaxx::ComponentSet reads()  const noexcept override {
        return threadmaxx::ComponentSet{threadmaxx::Component::Transform};
    }
    threadmaxx::ComponentSet writes() const noexcept override {
        return threadmaxx::ComponentSet{threadmaxx::Component::UserData};
    }

    void preStep(threadmaxx::SystemContext& ctx) override;
    void update (threadmaxx::SystemContext& /*ctx*/) override {}

private:
    UserComponentIds ids_;
    /// Cheap deterministic RNG state — incremented per (slot, tick)
    /// so bots don't all fire-special on the same tick.
    std::uint32_t    rngState_ = 0x9E3779B9u;
};

} // namespace tou2d
