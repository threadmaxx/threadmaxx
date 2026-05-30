#pragma once

#include "DemoTypes.hpp"

#include <threadmaxx/System.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <span>

namespace threadmaxx { class Engine; }

namespace tou2d {

/// M7.4 — friendly-fire arc tunables. Bots fire-suppress when ANY
/// same-faction ally lies inside a forward cone of half-angle
/// `kFriendlyFireArcRad` and within `kFriendlyFireRangeWU`. The arc is
/// deliberately a bit wider than BotControlSystem's `kFacingFire`
/// (~10°) so a near-miss bullet gets suppressed conservatively. The
/// range mirrors BotControlSystem's `kFireRange` (220 wu) so we only
/// gate inside the bot's effective engagement bubble. Humans bypass
/// the gate — friendly fire stays as an explicit game rule for manual
/// aim only.
inline constexpr float kFriendlyFireArcRad  = 0.30f;   // ~17°
inline constexpr float kFriendlyFireRangeWU = 220.0f;

/// M7.4 — POD describing one live ally candidate for `botShotHitsAlly`
/// to evaluate. The `selfIdx` field carries the entity index so the
/// helper can skip the firing ship's own row when iterating.
struct AllyPos {
    float         x = 0.0f, y = 0.0f;
    std::uint8_t  factionId = 0xFFu;
    std::uint32_t selfIdx   = 0xFFFFFFFFu;
};

/// M7.4 — friendly-fire suppression test for a bot's shot.
///
/// Returns true when any same-faction ally lies inside the firing
/// cone (forward half-angle `kFriendlyFireArcRad`, range
/// `kFriendlyFireRangeWU`). Returns false otherwise — including when
/// `allies` is empty or the only entries are the firing ship itself,
/// a different faction, or out-of-arc / out-of-range. Pure
/// (no globals); the engine path passes the firing ship's `ox/oy`,
/// `fireAngle` (orientation Z), `selfFaction` (factionId from the
/// LocalPlayer span), and `selfIdx` (entity index from the chunk).
///
/// The check is conservative — it suppresses a shot whose ARC covers
/// an ally even if the ally is laterally offset from the immediate
/// firing line, accepting some false-positive suppressions to make
/// blue-on-blue impossible in normal play.
bool botShotHitsAlly(std::span<const AllyPos> allies,
                     float ox, float oy,
                     float fireAngle,
                     std::uint8_t selfFaction,
                     std::uint32_t selfIdx) noexcept;

/// Reads PlayerInput.fireBasic + ship Transform per local-player ship,
/// spawns Bullet entities pointed along the ship's current orientation.
///
/// M4.2 — per-ship ammo + reload via the `WeaponLoadout` user component
/// (lives on the same entity as the ship). The system reads the
/// loadout, gates fires on `ammo > 0 && reloadIn == 0`, decrements
/// ammo on each fire, sets `reloadIn = kReload*Ticks` the moment the
/// magazine hits zero, and ticks the reload counter back to zero each
/// step. A reload that completes this tick refills ammo to the
/// magazine size — but does NOT fire the same tick (the player has to
/// re-press / re-hold to start the next burst). The previous
/// per-`EntityHandle.index` cooldown maps are gone; the loadout
/// component now carries the rate-limit state.
///
/// reads / writes:
///   * reads  = {Transform, Velocity, UserData}  (Velocity so the bullet
///              inherits the ship's velocity for a clean shoot-while-
///              moving feel; UserData participates as a common bit).
///   * writes = {EntityStructural, UserData}  — spawning bullets +
///              rewriting WeaponLoadout each tick.
class WeaponFireSystem : public threadmaxx::ISystem {
public:
    /// `engine` borrowed for `events<AudioPlay>` emission only (M4.8).
    /// Optional — pass nullptr to suppress audio cues.
    WeaponFireSystem(UserComponentIds ids,
                     threadmaxx::Engine* engine = nullptr) noexcept;

    const char*              name()   const noexcept override { return "tou2d.weaponFire"; }
    threadmaxx::ComponentSet reads()  const noexcept override {
        return threadmaxx::ComponentSet{
            threadmaxx::Component::Transform,
            threadmaxx::Component::Velocity,
            threadmaxx::Component::UserData,
        };
    }
    threadmaxx::ComponentSet writes() const noexcept override {
        return threadmaxx::ComponentSet{
            threadmaxx::Component::EntityStructural,
            threadmaxx::Component::UserData,
        };
    }

    void update(threadmaxx::SystemContext& ctx) override;

    /// M4.2 — round-end shared latch. When set, update early-outs so
    /// no bullets spawn for the duration of the freeze; reload
    /// counters also stop ticking (consistent with the world's idea
    /// of "frozen", and prevents post-round phantom refills).
    void setRoundEndedFlag(std::shared_ptr<std::atomic<bool>> f) noexcept {
        roundEnded_ = std::move(f);
    }

private:
    UserComponentIds                   ids_;
    std::shared_ptr<std::atomic<bool>> roundEnded_;
    threadmaxx::Engine*                engine_ = nullptr;   // borrowed; AudioPlay emit only
};

} // namespace tou2d
