#include "BotControlSystem.hpp"

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Query.hpp>
#include <threadmaxx/World.hpp>

#include <cmath>

namespace tou2d {

namespace {

constexpr float kFireRange         = 220.0f;   // open fire inside this distance
constexpr float kThrustHoldDist    = 60.0f;    // closer than this → coast (don't ram)
constexpr float kFacingThrust      = 0.78f;    // ~45° in radians, |angDelta| < this → thrust
constexpr float kFacingFire        = 0.17f;    // ~10° → fire
constexpr float kAngEpsilon        = 0.04f;    // ±2.3° dead-zone — stops jitter

// Retreat hysteresis edges — enter < 0.30, exit ≥ 0.50. The gap stops
// a ship hovering near 30% HP from flickering retreat/engage every
// tick (one stray dumbfire bounces hpFrac off the boundary cleanly).
constexpr float kRetreatEnterHp = 0.30f;
constexpr float kRetreatExitHp  = 0.50f;

// Effective range tiers for spread fire chance. Spread has a longer
// cooldown so we want it landing only when it pays off — wasted shots
// at long range eat the cooldown that would have allowed a hit-likely
// dumbfire. Close-in tickets get higher spread chance because the 3
// pellets are likely to all land.
constexpr float kSpreadRangeClose  =  80.0f;
constexpr float kSpreadRangeMid    = 160.0f;
constexpr float kSpreadChanceClose = 0.25f;
constexpr float kSpreadChanceMid   = 0.10f;

// Aim-lead reference speed — match WeaponFireSystem's `kMuzzleSpeed`
// (600 wu/s). Spread shots are slightly slower (520) but the bot's
// fire-arc check (±10°) is wide enough to absorb the small lead error
// from a single shared constant; not worth a per-weapon lead.
constexpr float kBulletReferenceSpeed = 600.0f;

inline float orientationAngleZ(const threadmaxx::Quat& q) noexcept {
    return std::atan2(2.0f * (q.w * q.z + q.x * q.y),
                      1.0f - 2.0f * (q.y * q.y + q.z * q.z));
}

/// Shortest signed angular difference target-current, wrapped into (-π, π].
inline float wrapPi(float a) noexcept {
    constexpr float twoPi = 6.28318530718f;
    constexpr float pi    = 3.14159265359f;
    a = std::fmod(a + pi, twoPi);
    if (a < 0.0f) a += twoPi;
    return a - pi;
}

/// xorshift32 — single-tick PRNG used to dither bot decisions.
inline std::uint32_t xorshift32(std::uint32_t& s) noexcept {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

struct ShipPos {
    threadmaxx::EntityHandle handle{};
    float                    x  = 0, y  = 0;
    float                    vx = 0, vy = 0;   ///< for aim-lead
};

} // namespace

BotControlSystem::BotControlSystem(UserComponentIds ids) noexcept : ids_(ids) {}

void BotControlSystem::preStep(threadmaxx::SystemContext& ctx) {
    const auto idsPi = ids_.playerInput;
    const auto idsLp = ids_.localPlayer;
    if (!idsPi.valid() || !idsLp.valid()) return;

    // M4.2 — round over, bots also stop writing input.
    if (roundEnded_ && roundEnded_->load(std::memory_order_acquire)) {
        return;
    }

    const auto idsShip = ids_.ship;

    ctx.single([&](threadmaxx::Range /*r*/, threadmaxx::CommandBuffer& cb) {
        const auto& view = ctx.worldView();

        // ---- Pass 1: gather position + velocity of every live ship --------
        // "live" = LocalPlayer-tagged and NOT DisabledTag. Cheap pre-scan
        // because ≤4 ships ever exist. Velocity is captured for aim-lead in
        // pass 2; transforms+velocities are parallel chunk vectors so the
        // walk costs one extra span dereference per row.
        std::array<ShipPos, 16> live;
        std::size_t             liveCount = 0;

        for (const auto* chunkPtr : view.chunks()) {
            if (!chunkPtr) continue;
            const auto& chunk = *chunkPtr;
            if (!chunk.mask.has(idsLp.componentBit()))               continue;
            if (!chunk.mask.has(threadmaxx::Component::Transform))   continue;
            if (!chunk.mask.has(threadmaxx::Component::Velocity))    continue;
            if (chunk.mask.has(threadmaxx::Component::DisabledTag))  continue;

            const auto& transforms = chunk.transforms;
            const auto& velocities = chunk.velocities;
            const auto  entities   = chunk.entities;
            for (std::size_t row = 0, n = entities.size(); row < n; ++row) {
                if (liveCount >= live.size()) break;
                live[liveCount].handle = entities[row];
                live[liveCount].x      = transforms[row].position.x;
                live[liveCount].y      = transforms[row].position.y;
                live[liveCount].vx     = velocities[row].linear.x;
                live[liveCount].vy     = velocities[row].linear.y;
                ++liveCount;
            }
        }

        // ---- Pass 2: drive each bot ship ----------------------------------
        // Bot chunks additionally require Ship — the retreat state machine
        // reads hpFrac from it. Bots are guaranteed to have Ship at spawn
        // time (TouGame::spawnShip attaches it for every slot); the extra
        // mask gate is defence-in-depth.
        for (const auto* chunkPtr : view.chunks()) {
            if (!chunkPtr) continue;
            const auto& chunk = *chunkPtr;
            if (!chunk.mask.has(idsLp.componentBit()))               continue;
            if (!chunk.mask.has(idsPi.componentBit()))               continue;
            if (idsShip.valid() && !chunk.mask.has(idsShip.componentBit())) continue;
            if (!chunk.mask.has(threadmaxx::Component::Transform))   continue;
            if (chunk.mask.has(threadmaxx::Component::DisabledTag))  continue;

            const auto lpSpan      = threadmaxx::user::chunkSpan<LocalPlayer>(chunk, idsLp);
            const auto shipSpan    = idsShip.valid()
                ? threadmaxx::user::chunkSpan<Ship>(chunk, idsShip)
                : std::span<const Ship>{};
            const auto& transforms = chunk.transforms;
            const auto  entities   = chunk.entities;
            for (std::size_t row = 0, n = entities.size(); row < n; ++row) {
                if (lpSpan[row].isBot == 0) continue;  // human — InputSystem won this slot

                const auto& selfT = transforms[row];
                const std::uint8_t slot = lpSpan[row].slot;

                // Nearest other live ship.
                float            bestD2 = 1e30f;
                const ShipPos*   tgt    = nullptr;
                for (std::size_t i = 0; i < liveCount; ++i) {
                    if (live[i].handle.index == entities[row].index) continue;
                    const float dx = live[i].x - selfT.position.x;
                    const float dy = live[i].y - selfT.position.y;
                    const float d2 = dx * dx + dy * dy;
                    if (d2 < bestD2) {
                        bestD2 = d2;
                        tgt    = &live[i];
                    }
                }

                // Retreat hysteresis — only meaningful if we can see our own
                // HP. Without Ship data the bot defaults to never retreating.
                if (slot < retreating_.size() && !shipSpan.empty()) {
                    const Ship& sh    = shipSpan[row];
                    const float maxHp = sh.maxHp > 0.0f ? sh.maxHp : 1.0f;
                    const float frac  = sh.currentHp / maxHp;
                    if (retreating_[slot]) {
                        if (frac >= kRetreatExitHp) retreating_[slot] = false;
                    } else {
                        if (frac <  kRetreatEnterHp) retreating_[slot] = true;
                    }
                }
                const bool retreat = slot < retreating_.size() && retreating_[slot];

                PlayerInput in{};  // all zero — drift if no target

                if (tgt) {
                    const float dx0   = tgt->x - selfT.position.x;
                    const float dy0   = tgt->y - selfT.position.y;
                    const float range = std::sqrt(dx0 * dx0 + dy0 * dy0);

                    // Aim-lead: project the target forward by the time the
                    // bullet would need to cross the present range. Uses
                    // target's CURRENT velocity (no iterative refinement —
                    // close enough at the ranges the bot ever engages at).
                    // Skipped in retreat (we don't shoot when retreating).
                    const float flightT = range / kBulletReferenceSpeed;
                    const float leadX   = tgt->x + tgt->vx * flightT;
                    const float leadY   = tgt->y + tgt->vy * flightT;
                    const float dxL     = leadX - selfT.position.x;
                    const float dyL     = leadY - selfT.position.y;

                    // Atan2 here returns the world-space angle to lead pos;
                    // our Z-rotation forward maps to (-sin θ, cos θ), so
                    // the target angle is atan2(-dx, dy). In retreat we
                    // flip 180° — fly away from the nearest enemy.
                    float tgtAngle = std::atan2(-dxL, dyL);
                    if (retreat) {
                        tgtAngle += 3.14159265359f;  // mirror
                    }
                    const float curAngle  = orientationAngleZ(selfT.orientation);
                    const float angDelta  = wrapPi(tgtAngle - curAngle);

                    if (angDelta >  kAngEpsilon) in.turnLeft  = 1;
                    if (angDelta < -kAngEpsilon) in.turnRight = 1;

                    if (retreat) {
                        // Run, no shoot. Thrust as long as we're roughly
                        // facing the escape vector (don't burn fuel backward).
                        if (std::fabs(angDelta) < kFacingThrust) in.thrust = 1;
                    } else {
                        if (std::fabs(angDelta) < kFacingThrust && range > kThrustHoldDist) {
                            in.thrust = 1;
                        }
                        if (std::fabs(angDelta) < kFacingFire && range < kFireRange) {
                            in.fireBasic = 1;

                            // Range-tiered spread chance. Roll once per
                            // in-arc tick from THIS bot's stream — two bots
                            // in the same arc against the same target will
                            // produce different rolls.
                            float chance = 0.0f;
                            if      (range < kSpreadRangeClose) chance = kSpreadChanceClose;
                            else if (range < kSpreadRangeMid)   chance = kSpreadChanceMid;
                            if (chance > 0.0f && slot < rngBySlot_.size()) {
                                const float r = static_cast<float>(
                                    xorshift32(rngBySlot_[slot]) >> 8) /
                                    static_cast<float>(1u << 24);
                                if (r < chance) in.fireSpecial = 1;
                            }
                        }
                    }
                }

                threadmaxx::addUserComponent(cb, idsPi, entities[row], in);
            }
        }
    });
}

} // namespace tou2d
