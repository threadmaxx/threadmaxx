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

// M4.5 — wander. Bots without a visible target or with the nearest
// target outside `kWanderRange` switch to wander mode: roll a random
// world-space heading and chase it for `[kWanderTicksMin,
// kWanderTicksMax]` ticks. Stops them sitting motionless when alone.
constexpr float kWanderRange     = 360.0f;  // engage if target closer; else wander
constexpr std::uint16_t kWanderTicksMin = 60;   // 1.0 s @ 60 Hz
constexpr std::uint16_t kWanderTicksMax = 180;  // 3.0 s
constexpr float kWanderFacingThrust = 1.05f;    // ~60° — looser than engage
                                                 // facing so the bot keeps
                                                 // momentum through turns

// M4.5 — aim wobble: in engage mode, the bot's perceived target angle
// is perturbed by `sin(phase) * kAimWobbleAmp`. 2026-05-29 — bumped
// from 0.10 to 0.25 rad and slowed from 0.08 to 0.05 rad/tick so the
// bot visibly weaves left-right while shooting instead of stalling on
// a perfect bearing.
constexpr float kAimWobbleAmp        = 0.25f;   // ~14°
constexpr float kAimWobbleFreqPerTick = 0.05f;  // rad / tick; period ≈ 125 ticks (~2.1 s)

// 2026-05-28 — terrain avoidance tunables.
//
// The bot casts a single forward ray of `kAvoidLookahead` world units
// and a pair of left/right diagonal "feelers" at ±kAvoidFeelerAngle.
// Whichever (forward, left, right) the cheapest hit lies on picks the
// turn-away sign; the choice is then latched for `kAvoidCommitTicks`
// ticks so the bot commits to one steering direction instead of
// stuttering as the ray geometry crosses cell boundaries.
//
// 2026-05-29 — bumped lookahead 60→90 wu so the bot sees walls earlier
// and has more turn-budget before getting wedged; latch 18→30 ticks so
// commits hold across longer obstacles; added per-tick feeler re-check
// so a wrong-side commit can flip when it discovers the latched side
// is worse than the other.
constexpr float kAvoidLookahead       = 90.0f;   // world units (~3 ships)
constexpr float kAvoidFeelerAngle     = 0.55f;   // ~32°
constexpr int   kAvoidSamples         = 6;       // sample count along each ray
constexpr std::uint16_t kAvoidCommitTicks = 30;  // 500 ms

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

/// Cast a ray of length `kAvoidLookahead` along `angle` from `(ox, oy)`
/// and return the world-distance to the first solid cell, or +∞ when
/// the ray clears `kAvoidSamples` points without finding terrain. The
/// sampler also reports a "hit" when the ray exits the grid (treats
/// off-grid as a wall) so bots don't fly off into the void.
inline float castTerrainRay(const TerrainGrid& grid,
                            float ox, float oy,
                            float angle,
                            float maxDist) noexcept {
    if (grid.cellsX <= 0 || grid.cellsY <= 0) return maxDist;
    const float sa = std::sin(angle);
    const float ca = std::cos(angle);
    // Ship-forward direction matches WeaponFireSystem's bullet forward
    // (-sin θ, cos θ) so the ray casts in the same direction the ship
    // would fire / thrust.
    const float fx = -sa;
    const float fy =  ca;
    for (int i = 1; i <= kAvoidSamples; ++i) {
        const float t = (static_cast<float>(i) / static_cast<float>(kAvoidSamples)) * maxDist;
        const float wx = ox + fx * t;
        const float wy = oy + fy * t;
        const std::int32_t cx = static_cast<std::int32_t>(
            std::floor(wx / kTileWorldUnits + 0.5f));
        const std::int32_t cy = static_cast<std::int32_t>(
            std::floor(wy / kTileWorldUnits + 0.5f));
        if (!grid.inBounds(cx, cy)) return t;   // off-grid = wall
        if (grid.attrAt(cx, cy) == Attribute::Solid) return t;
    }
    return maxDist;
}

} // namespace

BotControlSystem::BotControlSystem(UserComponentIds ids) noexcept : ids_(ids) {
    // M5.1 — seed every per-slot xorshift32 stream deterministically.
    // Matches the M4.1 four-slot scheme (golden-ratio constant XOR'd
    // with a per-slot dither), generalized so the up-to-63 bot slots
    // each get an independent stream without aliasing the 4 humans.
    for (std::size_t s = 0; s < rngBySlot_.size(); ++s) {
        rngBySlot_[s] =
            0x9E3779B9u ^ (0x85EBCA6Bu *
                static_cast<std::uint32_t>(s + 1) + 1u);
    }
}

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

                // M4.5 — wander/engage gate. Engage only when a target
                // exists AND it's inside `kWanderRange`. The further-out
                // case (or no-target case) drops into the wander branch
                // so the bot keeps moving instead of sitting idle.
                const bool hasEngageTarget = tgt != nullptr;
                float       engageRange    = 0.0f;
                if (hasEngageTarget) {
                    const float dx0 = tgt->x - selfT.position.x;
                    const float dy0 = tgt->y - selfT.position.y;
                    engageRange     = std::sqrt(dx0 * dx0 + dy0 * dy0);
                }
                const bool engage = hasEngageTarget && engageRange < kWanderRange;

                if (engage) {
                    // ---- ENGAGE ----------------------------------------
                    const float dx0   = tgt->x - selfT.position.x;
                    const float dy0   = tgt->y - selfT.position.y;
                    const float range = engageRange;

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

                    // M4.5 — aim wobble. Sine wave gives a smooth left/
                    // right oscillation; xorshift chaos adds per-tick
                    // randomness so two bots aren't in phase. Combined
                    // amplitude is ~9° peak — enough to spoil precision
                    // aim, small enough to still land hits in the ±10°
                    // fire arc.
                    if (slot < aimWobblePhase_.size()) {
                        ++aimWobblePhase_[slot];
                        const float phaseRad = static_cast<float>(aimWobblePhase_[slot]) *
                                               kAimWobbleFreqPerTick;
                        tgtAngle += std::sin(phaseRad) * kAimWobbleAmp;
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
                        // 2026-05-29 — keep thrust on whenever we're in
                        // engage mode and facing-ish. The old gate
                        // killed thrust below kThrustHoldDist so a
                        // close-range bot would stall and just shoot,
                        // which combined with the small aim wobble
                        // looked frozen. The wobble-induced angDelta
                        // now drives the bot to weave around the target.
                        const float minThrustDist = kThrustHoldDist * 0.5f;
                        if (std::fabs(angDelta) < kFacingThrust && range > minThrustDist) {
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
                } else if (slot < wanderTicksLeft_.size()) {
                    // ---- WANDER ----------------------------------------
                    // Roll a new heading + duration whenever the current
                    // one expires. Direction is uniform in [0, 2π);
                    // duration in [kWanderTicksMin, kWanderTicksMax].
                    if (wanderTicksLeft_[slot] == 0) {
                        if (slot < rngBySlot_.size()) {
                            const float a = static_cast<float>(
                                xorshift32(rngBySlot_[slot]) >> 8) /
                                static_cast<float>(1u << 24);
                            wanderAngle_[slot]     = a * 6.28318530718f;
                            const std::uint32_t r2 = xorshift32(rngBySlot_[slot]);
                            const std::uint16_t span =
                                static_cast<std::uint16_t>(kWanderTicksMax - kWanderTicksMin);
                            wanderTicksLeft_[slot] = static_cast<std::uint16_t>(
                                kWanderTicksMin + (r2 % (span + 1u)));
                        } else {
                            wanderTicksLeft_[slot] = kWanderTicksMin;
                        }
                    }
                    wanderTicksLeft_[slot] = static_cast<std::uint16_t>(wanderTicksLeft_[slot] - 1);

                    const float curAngle = orientationAngleZ(selfT.orientation);
                    const float angDelta = wrapPi(wanderAngle_[slot] - curAngle);
                    if (angDelta >  kAngEpsilon) in.turnLeft  = 1;
                    if (angDelta < -kAngEpsilon) in.turnRight = 1;
                    if (std::fabs(angDelta) < kWanderFacingThrust) in.thrust = 1;
                }

                // ---- TERRAIN AVOIDANCE (overrides above) -----------
                // Forward + ±feeler ray casts. If the central ray hits
                // close terrain, override turn direction to the side
                // whose feeler has more room. Latched for a few hundred
                // ms so the bot commits to one steering side instead of
                // ping-ponging across the same wall edge.
                //
                // 2026-05-29 — three refinements over the 2026-05-28
                // landing: (a) re-check feelers every tick while the
                // latch is active, flipping sign if the un-latched side
                // has noticeably more room; this rescues bots that
                // commit into a corner pocket. (b) only suppress thrust
                // when the forward ray is REALLY close (< 0.25 of
                // lookahead) AND both feelers are blocked; otherwise
                // keep thrust on so the turning rotates the thrust
                // vector and the bot slides along the wall. (c) raised
                // lookahead 60→90 wu so this whole branch fires earlier.
                if (grid_ && slot < avoidSign_.size()) {
                    if (avoidCommit_[slot] > 0) {
                        avoidCommit_[slot] = static_cast<std::uint16_t>(avoidCommit_[slot] - 1);
                    } else {
                        avoidSign_[slot] = 0;
                    }
                    const float curA = orientationAngleZ(selfT.orientation);
                    const float dC   = castTerrainRay(*grid_, selfT.position.x, selfT.position.y,
                                                       curA, kAvoidLookahead);
                    if (dC < kAvoidLookahead * 0.85f) {
                        const float dL = castTerrainRay(*grid_, selfT.position.x, selfT.position.y,
                                                        curA + kAvoidFeelerAngle, kAvoidLookahead);
                        const float dR = castTerrainRay(*grid_, selfT.position.x, selfT.position.y,
                                                        curA - kAvoidFeelerAngle, kAvoidLookahead);
                        if (avoidSign_[slot] == 0) {
                            avoidSign_[slot]   = (dL >= dR) ? std::int8_t{+1} : std::int8_t{-1};
                            avoidCommit_[slot] = kAvoidCommitTicks;
                        } else {
                            // Re-evaluate the commit: if the side we
                            // latched is clearly worse than the other
                            // (≥ 1.5× the room), flip and refresh the
                            // commit so we don't keep turning into a
                            // tightening pocket.
                            const float latched   = avoidSign_[slot] > 0 ? dL : dR;
                            const float opposite  = avoidSign_[slot] > 0 ? dR : dL;
                            if (opposite > latched * 1.5f) {
                                avoidSign_[slot]   = static_cast<std::int8_t>(-avoidSign_[slot]);
                                avoidCommit_[slot] = kAvoidCommitTicks;
                            }
                        }
                        // turnLeft = nose rotates CCW (positive Z). Sign
                        // convention chosen so +1 turns toward the side
                        // with more open space.
                        in.turnLeft  = avoidSign_[slot] > 0 ? 1u : 0u;
                        in.turnRight = avoidSign_[slot] < 0 ? 1u : 0u;
                        // Only kill thrust when we're really nose-up to
                        // a wall AND both feelers are blocked too — the
                        // textbook wedged-in-corner case. Anywhere else,
                        // keep thrust so the turn produces forward
                        // motion along the obstacle.
                        const bool fullyBoxed =
                            dC < kAvoidLookahead * 0.25f &&
                            dL < kAvoidLookahead * 0.40f &&
                            dR < kAvoidLookahead * 0.40f;
                        if (fullyBoxed) {
                            in.thrust = 0;
                        }
                    }
                }

                threadmaxx::addUserComponent(cb, idsPi, entities[row], in);
            }
        }
    });
}

} // namespace tou2d
