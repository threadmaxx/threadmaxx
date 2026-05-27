#pragma once

#include "DemoTypes.hpp"

#include <threadmaxx/System.hpp>

#include <atomic>
#include <cstdint>
#include <memory>

struct GLFWwindow;

namespace tou2d {

/// M4.3 — closes the round loop. While `roundEnded_` is true, polls the
/// GLFW window for the human's restart key (P1 fire — RShift — for
/// parity with the original arcade-style "press fire to start") and,
/// when seen + the post-round holdoff has elapsed, resets every ship
/// (HP, position, velocity, kills, ammo, DisabledTag) and flips
/// `roundEnded_` back to false. The next tick's preStep gates see the
/// reset state and the simulation resumes from a clean slate.
///
/// Reset semantics — full round reset for both modes:
///   * Ship.currentHp     = maxHp
///   * Ship.kills         = 0
///   * Ship.respawnIn     = 0  (clears any kPermanentDeathSentinel)
///   * Transform.position = (spawnX, spawnY, 0)
///   * Velocity           = 0
///   * WeaponLoadout      = fresh full magazines, zero reload
///   * DisabledTag        = removed (re-enables LSS losers)
/// `Ship.tilesDestroyed` is intentionally preserved — it's a session-
/// long curiosity counter, not a round-scoped one.
///
/// Registration: runs preStep AFTER Input/Bot (so the input gate has
/// already short-circuited for the tick, no stale PlayerInput races us)
/// but BEFORE everything else (the resets need to land before Movement
/// reads positions).
///
/// reads()  = none — restart never reads world state for its decision.
/// writes() = {Transform, Velocity, EntityStructural, UserData} — the
///            full surface a round reset touches.
class RoundRestartSystem : public threadmaxx::ISystem {
public:
    RoundRestartSystem(GLFWwindow* window, UserComponentIds ids) noexcept;

    const char*              name()   const noexcept override { return "tou2d.roundRestart"; }
    threadmaxx::ComponentSet reads()  const noexcept override { return threadmaxx::ComponentSet::none(); }
    threadmaxx::ComponentSet writes() const noexcept override {
        return threadmaxx::ComponentSet{
            threadmaxx::Component::Transform,
            threadmaxx::Component::Velocity,
            threadmaxx::Component::EntityStructural,
            threadmaxx::Component::UserData,
        };
    }

    void preStep(threadmaxx::SystemContext& ctx) override;
    void update (threadmaxx::SystemContext& /*ctx*/) override {}

    /// Install the shared round-end latch + winner pointers owned by
    /// TouGame. The restart pass clears all three to the "round live"
    /// resting state.
    void setRoundEndedFlag(std::shared_ptr<std::atomic<bool>> f,
                           std::uint8_t*  winnerSlot,
                           std::uint16_t* winnerKills) noexcept {
        roundEnded_  = std::move(f);
        winnerSlot_  = winnerSlot;
        winnerKills_ = winnerKills;
    }

private:
    GLFWwindow*                        window_     = nullptr;
    UserComponentIds                   ids_;
    std::shared_ptr<std::atomic<bool>> roundEnded_;
    std::uint8_t*                      winnerSlot_  = nullptr;
    std::uint16_t*                     winnerKills_ = nullptr;

    /// Decremented every tick while `roundEnded_` is set. The restart
    /// key is ignored until this reaches 0; rearmed (set back to
    /// kRestartHoldoffTicks) every time we observe `roundEnded_` newly
    /// false.
    std::uint16_t                      holdoffTicks_ = 0;

    /// One-tick latch — set true the first tick we observed
    /// `roundEnded_` true. Used so we re-arm the holdoff on a
    /// rising-edge transition (live → ended) rather than every tick
    /// it's ended.
    bool                               wasRoundEnded_ = false;
};

} // namespace tou2d
