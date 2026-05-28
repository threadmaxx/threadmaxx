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

    /// 2026-05-28 — borrowed terrain grid; the bot ray-casts a few
    /// cells ahead of its current heading so it can steer around solid
    /// terrain instead of slamming into it. Null means "no terrain
    /// awareness" (legacy behaviour). Must outlive the system.
    void setTerrainGrid(const TerrainGrid* grid) noexcept { grid_ = grid; }

private:
    UserComponentIds ids_;
    std::shared_ptr<std::atomic<bool>> roundEnded_;
    /// Per-slot xorshift32 streams, deterministically seeded from a
    /// golden-ratio constant XOR'd with a per-slot dither. Each bot
    /// pulls from its own stream so two bots looking at the same tick
    /// produce independent rolls.
    ///
    /// M5.1 — sized to `kMaxPlayerSlots` so any of the up-to-63 bots
    /// has its own independent stream. Initialized from a seed function
    /// in the ctor body (constexpr init for 64 entries is a noisy
    /// initializer-list otherwise).
    std::array<std::uint32_t, kMaxPlayerSlots>  rngBySlot_{};
    /// Per-slot retreat latch — see header comment. Persists across
    /// ticks; hysteresis edges are 0.30 (enter) / 0.50 (exit).
    std::array<bool, kMaxPlayerSlots> retreating_{};

    /// M4.5 — wander state. When `wanderTicksLeft_[slot] > 0` AND the
    /// bot has no in-range target, it chases `wanderAngle_[slot]`
    /// (world-space heading) and thrusts toward it. On reaching 0 a
    /// new direction + duration is rolled — duration is uniform in
    /// `[60, 180]` ticks (1-3 s) and direction is full 2π uniform.
    /// Pulls from the same `rngBySlot_` stream as the spread roll, so
    /// the schedule is fully deterministic per seed.
    std::array<std::uint16_t, kMaxPlayerSlots> wanderTicksLeft_{};
    std::array<float, kMaxPlayerSlots>         wanderAngle_{};

    /// M4.5 — aim wobble phase. Per-slot tick counter; the engaged-fire
    /// path adds `sin(phase * kAimWobbleFreq) * kAimWobbleAmp` to the
    /// computed lead angle. This makes the aim oscillate left-right
    /// of the target by a few degrees, which in turn causes the ship
    /// to perpetually chase a moving aim point — natural left-right
    /// weave, no special-case "strafe" logic needed.
    std::array<std::uint32_t, kMaxPlayerSlots> aimWobblePhase_{};

    /// 2026-05-28 — terrain-avoidance state. Once a forward ray hits
    /// solid terrain inside `kAvoidLookahead`, the bot latches a turn
    /// direction (+1 / -1) for `kAvoidCommitTicks` ticks and steers
    /// that way continuously — flipping the sign every tick under
    /// fire produced the earlier "shaking against the wall" look.
    /// `avoidSign_` of 0 means "no obstacle / free to engage".
    std::array<std::int8_t,   kMaxPlayerSlots> avoidSign_{};
    std::array<std::uint16_t, kMaxPlayerSlots> avoidCommit_{};

    const TerrainGrid*                          grid_ = nullptr;
};

} // namespace tou2d
