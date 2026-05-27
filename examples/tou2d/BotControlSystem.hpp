#pragma once

#include "DemoTypes.hpp"

#include <threadmaxx/System.hpp>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

namespace tou2d {

/// preStep system that overrides PlayerInput for ships with
/// `LocalPlayer::isBot != 0`. Registered AFTER InputSystem so the bot
/// writes win for those slots; human slots are untouched.
///
/// M4.1 polish over the M3.4 minimum:
///   * Per-slot decorrelated xorshift32 stream — bots no longer
///     fire-special on the same tick because their seeds differ.
///   * Aim-lead: target position projected by `range / muzzleSpeed`
///     using target's current velocity, so moving prey gets hit.
///   * Retreat: when own hpFrac < 0.30, the bot flips its target
///     heading and thrusts AWAY from the nearest enemy; exits retreat
///     once hpFrac ≥ 0.50 (hysteresis stops oscillation across the
///     boundary).
///   * Spread chance is range-tiered: 25% close, 10% medium, 0% far —
///     conserves the longer cooldown for shots that actually land.
///
/// reads()  = Transform (positions + orientations) + Velocity (aim-lead
///            uses target velocity) + UserData (LocalPlayer + Ship).
/// writes() = UserData (PlayerInput is a user component; same
///            conservative declaration InputSystem uses).
class BotControlSystem : public threadmaxx::ISystem {
public:
    explicit BotControlSystem(UserComponentIds ids) noexcept;

    const char*              name()   const noexcept override { return "tou2d.botControl"; }
    threadmaxx::ComponentSet reads()  const noexcept override {
        return threadmaxx::ComponentSet{
            threadmaxx::Component::Transform,
            threadmaxx::Component::Velocity,
            threadmaxx::Component::UserData,
        };
    }
    threadmaxx::ComponentSet writes() const noexcept override {
        return threadmaxx::ComponentSet{threadmaxx::Component::UserData};
    }

    void preStep(threadmaxx::SystemContext& ctx) override;
    void update (threadmaxx::SystemContext& /*ctx*/) override {}

    /// M4.2 — round-end shared latch. When set, preStep early-outs so
    /// bots stop steering / firing. Same shape as InputSystem's gate.
    void setRoundEndedFlag(std::shared_ptr<std::atomic<bool>> f) noexcept {
        roundEnded_ = std::move(f);
    }

private:
    UserComponentIds ids_;
    std::shared_ptr<std::atomic<bool>> roundEnded_;
    /// Per-slot xorshift32 streams, deterministically seeded from a
    /// golden-ratio constant XOR'd with a per-slot dither. Each bot
    /// pulls from its own stream so two bots looking at the same tick
    /// produce independent rolls.
    std::array<std::uint32_t, 4> rngBySlot_{
        0x9E3779B9u ^ 0x85EBCA6Bu,
        0x9E3779B9u ^ (0x85EBCA6Bu * 2u + 1u),
        0x9E3779B9u ^ (0x85EBCA6Bu * 3u + 1u),
        0x9E3779B9u ^ (0x85EBCA6Bu * 4u + 1u),
    };
    /// Per-slot retreat latch — see header comment. Persists across
    /// ticks; hysteresis edges are 0.30 (enter) / 0.50 (exit).
    std::array<bool, 4> retreating_{};

    /// M4.5 — wander state. When `wanderTicksLeft_[slot] > 0` AND the
    /// bot has no in-range target, it chases `wanderAngle_[slot]`
    /// (world-space heading) and thrusts toward it. On reaching 0 a
    /// new direction + duration is rolled — duration is uniform in
    /// `[60, 180]` ticks (1-3 s) and direction is full 2π uniform.
    /// Pulls from the same `rngBySlot_` stream as the spread roll, so
    /// the schedule is fully deterministic per seed.
    std::array<std::uint16_t, 4> wanderTicksLeft_{};
    std::array<float, 4>         wanderAngle_{};

    /// M4.5 — aim wobble phase. Per-slot tick counter; the engaged-fire
    /// path adds `sin(phase * kAimWobbleFreq) * kAimWobbleAmp` to the
    /// computed lead angle. This makes the aim oscillate left-right
    /// of the target by a few degrees, which in turn causes the ship
    /// to perpetually chase a moving aim point — natural left-right
    /// weave, no special-case "strafe" logic needed.
    std::array<std::uint32_t, 4> aimWobblePhase_{};
};

} // namespace tou2d
