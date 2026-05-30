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

// 2026-05-30 — terrain repulsion gradient + orbital dogfight tunables.
//
// REPULSION: in addition to the hard forward-ray override above, every
// engage / wander tick samples 8 directions around the bot for solid
// terrain. Each direction contributes an away-from-wall vector whose
// magnitude grows quadratically as the wall gets closer. The sum
// becomes a steering bias on the desired flight heading — the bot
// drifts toward open space without needing a hard panic-turn signal.
// This makes bots visibly "thread the needle" through corridors
// instead of grinding against walls.
//
// ORBIT: at close range (< kOrbitRange) the desired flight heading
// rotates tangential to the target line, so the bot circles instead
// of slamming nose-first into the target. Combined with the
// terrain-repulsion bias and an aim-vs-heading split (fire decision
// uses the raw target angle, turn decision uses the bias-mixed
// heading) the bot stays in motion while shooting — fixes the
// "two bots stationary on top of each other firing into nothing"
// stalemate.
constexpr float kRepelLookahead     = 100.0f;  // world units
constexpr int   kRepelDirCount      = 8;       // sample directions around the bot
constexpr int   kRepelSamplesPerRay = 4;       // sample count along each ray
constexpr float kRepelWeight        = 1.4f;    // contribution to desired-heading
constexpr float kOrbitRange         = 100.0f;  // close-engage threshold
constexpr float kOrbitMaxBlend      = 0.85f;   // 0..1 — tangential share at r=0
constexpr float kOverlapRange       = 6.0f;    // below this, ships are basically on top
constexpr float kTwoPi              = 6.28318530718f;

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
    /// M7.4 — captured from `LocalPlayer.factionId` in pass 1 so the
    /// engage-target loop can skip same-faction candidates without a
    /// second user-component lookup. Defaults to the sentinel so
    /// chunks that lack LocalPlayer (defensive — should not happen
    /// with the current world layout) compare unequal to everyone.
    std::uint8_t             factionId = kFactionAuto;
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

/// 2026-05-30 — sample `kRepelDirCount` rays around the bot, accumulate
/// inverse-distance away-from-wall vectors, and write the sum into
/// `(outX, outY)`. The magnitude is unbounded but typically ≤ 2 in
/// open corridors; callers scale by `kRepelWeight` when mixing into
/// the desired heading vector. Uses a coarser per-ray sample count
/// (kRepelSamplesPerRay = 4) than castTerrainRay because the gradient
/// uses 8 rays and we don't want preStep to blow up.
inline void sampleTerrainRepulsion(const TerrainGrid& grid,
                                   float ox, float oy,
                                   float& outX, float& outY) noexcept {
    outX = 0.0f;
    outY = 0.0f;
    if (grid.cellsX <= 0 || grid.cellsY <= 0) return;
    for (int i = 0; i < kRepelDirCount; ++i) {
        const float angle = static_cast<float>(i) *
                            (kTwoPi / static_cast<float>(kRepelDirCount));
        const float sa = std::sin(angle);
        const float ca = std::cos(angle);
        const float fx = -sa;   // direction this ray points
        const float fy =  ca;
        float hitT = kRepelLookahead;
        for (int s = 1; s <= kRepelSamplesPerRay; ++s) {
            const float t = (static_cast<float>(s) /
                             static_cast<float>(kRepelSamplesPerRay)) *
                            kRepelLookahead;
            const float wx = ox + fx * t;
            const float wy = oy + fy * t;
            const std::int32_t cx = static_cast<std::int32_t>(
                std::floor(wx / kTileWorldUnits + 0.5f));
            const std::int32_t cy = static_cast<std::int32_t>(
                std::floor(wy / kTileWorldUnits + 0.5f));
            if (!grid.inBounds(cx, cy)) { hitT = t; break; }
            if (grid.attrAt(cx, cy) == Attribute::Solid) { hitT = t; break; }
        }
        if (hitT >= kRepelLookahead) continue;
        // Wall in direction (fx, fy) at distance hitT; we want to push
        // AWAY from it (i.e. -(fx,fy)) with strength that grows as
        // hitT shrinks. Squared falloff makes the close walls dominate
        // — nearby tile is much stronger than a wall at the lookahead
        // edge.
        const float closeness = 1.0f - hitT / kRepelLookahead;
        const float w = closeness * closeness;
        outX -= fx * w;
        outY -= fy * w;
    }
}

} // namespace

bool findNearestRepairTile(const TerrainGrid& grid,
                           float ox, float oy,
                           float maxRadiusWU,
                           float& outX, float& outY) noexcept {
    if (grid.cellsX <= 0 || grid.cellsY <= 0) return false;
    if (!(maxRadiusWU > 0.0f))               return false;

    // Convert the radius to a cell extent and bound the scan square to
    // the grid. `floor(.../tile + 0.5)` matches the grid's world→cell
    // mapping used elsewhere (castTerrainRay, BulletTerrainSystem).
    const float oCellXf = ox / kTileWorldUnits;
    const float oCellYf = oy / kTileWorldUnits;
    const std::int32_t rCells =
        static_cast<std::int32_t>(std::ceil(maxRadiusWU / kTileWorldUnits));
    const std::int32_t cxLo = static_cast<std::int32_t>(std::floor(oCellXf + 0.5f)) - rCells;
    const std::int32_t cxHi = static_cast<std::int32_t>(std::floor(oCellXf + 0.5f)) + rCells;
    const std::int32_t cyLo = static_cast<std::int32_t>(std::floor(oCellYf + 0.5f)) - rCells;
    const std::int32_t cyHi = static_cast<std::int32_t>(std::floor(oCellYf + 0.5f)) + rCells;

    bool  found    = false;
    float bestD2   = maxRadiusWU * maxRadiusWU;
    float bestX    = 0.0f;
    float bestY    = 0.0f;
    for (std::int32_t cy = cyLo; cy <= cyHi; ++cy) {
        for (std::int32_t cx = cxLo; cx <= cxHi; ++cx) {
            if (!grid.inBounds(cx, cy)) continue;
            if (grid.attrAt(cx, cy) != Attribute::RepairBase) continue;
            const float wx = static_cast<float>(cx) * kTileWorldUnits;
            const float wy = static_cast<float>(cy) * kTileWorldUnits;
            const float dx = wx - ox;
            const float dy = wy - oy;
            const float d2 = dx * dx + dy * dy;
            if (d2 <= bestD2) {
                bestD2 = d2;
                bestX  = wx;
                bestY  = wy;
                found  = true;
            }
        }
    }
    if (found) {
        outX = bestX;
        outY = bestY;
    }
    return found;
}

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
            // M7.4 — capture per-row factionId so the engage-target
            // search can reject same-faction candidates.
            const auto  lpSpan     = threadmaxx::user::chunkSpan<LocalPlayer>(chunk, idsLp);
            for (std::size_t row = 0, n = entities.size(); row < n; ++row) {
                if (liveCount >= live.size()) break;
                live[liveCount].handle    = entities[row];
                live[liveCount].x         = transforms[row].position.x;
                live[liveCount].y         = transforms[row].position.y;
                live[liveCount].vx        = velocities[row].linear.x;
                live[liveCount].vy        = velocities[row].linear.y;
                live[liveCount].factionId = lpSpan[row].factionId;
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
                const std::uint8_t slot         = lpSpan[row].slot;
                const std::uint8_t selfFaction  = lpSpan[row].factionId;

                // Nearest other live ship. M7.4 — same-faction ships
                // are skipped here so a bot never engages an ally.
                // Bots in the only-allies-in-range case fall through
                // to the wander branch instead of orbiting a friend.
                float            bestD2 = 1e30f;
                const ShipPos*   tgt    = nullptr;
                for (std::size_t i = 0; i < liveCount; ++i) {
                    if (live[i].handle.index == entities[row].index) continue;
                    if (live[i].factionId == selfFaction)            continue;
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
                // M7.1 — guarded retreat. The hysteresis latch above only
                // signals INTENT to retreat; we only ACT on it if a Repair
                // tile sits within `kBotRepairSearchRadiusWU`. Without a
                // reachable heal source the bot flips back into engage —
                // running away just to die in open space is worse than
                // standing and fighting.
                const bool wantRetreat = slot < retreating_.size() && retreating_[slot];
                float retreatTargetX = 0.0f;
                float retreatTargetY = 0.0f;
                bool  haveRepair     = false;
                if (wantRetreat && grid_) {
                    haveRepair = findNearestRepairTile(*grid_,
                                                       selfT.position.x,
                                                       selfT.position.y,
                                                       kBotRepairSearchRadiusWU,
                                                       retreatTargetX,
                                                       retreatTargetY);
                }
                const bool retreat = wantRetreat && haveRepair;

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

                // 2026-05-30 — terrain-repulsion gradient used by BOTH
                // engage and wander branches. Cheap (~8 short rays);
                // computed once per bot per tick.
                float repelX = 0.0f, repelY = 0.0f;
                if (grid_) {
                    sampleTerrainRepulsion(*grid_, selfT.position.x,
                                           selfT.position.y, repelX, repelY);
                }

                const float curAngleZ = orientationAngleZ(selfT.orientation);

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

                    // Aim angle drives the FIRE decision — uses lead +
                    // wobble but ignores orbit/repulsion. Keeps the bot
                    // shooting at the actual target while flying any
                    // direction.
                    float aimAngle = std::atan2(-dxL, dyL);
                    if (slot < aimWobblePhase_.size()) {
                        ++aimWobblePhase_[slot];
                        const float phaseRad = static_cast<float>(aimWobblePhase_[slot]) *
                                               kAimWobbleFreqPerTick;
                        aimAngle += std::sin(phaseRad) * kAimWobbleAmp;
                    }
                    const float aimDelta = wrapPi(aimAngle - curAngleZ);

                    // 2026-05-30 — build the desired FLIGHT heading
                    // (separate from aim). Steps: (1) pursuit unit
                    // vector toward target (or away if retreating); (2)
                    // tangential orbit mix at close range so the bot
                    // doesn't slam onto target; (3) terrain repulsion
                    // gradient so it threads through the canvas.
                    float desX, desY;
                    if (range < kOverlapRange) {
                        // Two ships practically on top of each other —
                        // pursuit unit vector is unstable, force a
                        // slot-deterministic kick so the bots separate.
                        const float kickA =
                            static_cast<float>(slot) * 1.2566370614f;  // 72° apart
                        desX = -std::sin(kickA);
                        desY =  std::cos(kickA);
                    } else {
                        desX = dx0 / range;
                        desY = dy0 / range;
                    }
                    if (retreat) {
                        // M7.1 — steer TOWARD the repair tile instead of
                        // away-from-enemy. The hysteresis above only let
                        // us in here when a tile exists inside the search
                        // radius, so this vector is always finite.
                        const float rdx = retreatTargetX - selfT.position.x;
                        const float rdy = retreatTargetY - selfT.position.y;
                        const float rd  = std::sqrt(rdx * rdx + rdy * rdy);
                        if (rd > 1e-3f) {
                            desX = rdx / rd;
                            desY = rdy / rd;
                        }
                    } else {
                        // Orbit bias: roll a ± sign on first close-range
                        // entry, latch it until we leave close range.
                        // Tangential component grows linearly as range
                        // shrinks below kOrbitRange.
                        if (range < kOrbitRange) {
                            if (slot < orbitSign_.size() && orbitSign_[slot] == 0) {
                                orbitSign_[slot] =
                                    (xorshift32(rngBySlot_[slot]) & 1u)
                                        ? std::int8_t{+1}
                                        : std::int8_t{-1};
                            }
                            const float sign = (slot < orbitSign_.size())
                                ? static_cast<float>(orbitSign_[slot])
                                : 1.0f;
                            const float closeness =
                                std::clamp(1.0f - range / kOrbitRange, 0.0f, 1.0f);
                            const float blend = closeness * kOrbitMaxBlend;
                            // perp = rotate(des, +90°) for sign=+1
                            const float perpX = -desY * sign;
                            const float perpY =  desX * sign;
                            desX = desX * (1.0f - blend) + perpX * blend;
                            desY = desY * (1.0f - blend) + perpY * blend;
                        } else if (slot < orbitSign_.size()) {
                            orbitSign_[slot] = 0;  // re-roll on next close approach
                        }
                    }
                    // Mix in terrain repulsion — the gradient pushes the
                    // bot toward open space so it visibly steers clear
                    // of walls between engagement moves.
                    desX += repelX * kRepelWeight;
                    desY += repelY * kRepelWeight;

                    const float desLen = std::sqrt(desX * desX + desY * desY);
                    const float headingAngle = (desLen > 1e-4f)
                        ? std::atan2(-desX, desY)
                        : aimAngle;  // collapsed vector — fall back to aim
                    const float headDelta = wrapPi(headingAngle - curAngleZ);

                    if (headDelta >  kAngEpsilon) in.turnLeft  = 1;
                    if (headDelta < -kAngEpsilon) in.turnRight = 1;

                    // Thrust whenever roughly facing the desired heading
                    // — no close-range cutoff. The orbit/repulsion biases
                    // already steer the bot away from "stall on top of
                    // target" so always-thrust produces continuous
                    // dogfight motion.
                    if (std::fabs(headDelta) < kFacingThrust) in.thrust = 1;

                    // Fire decision uses the aim angle (lead + wobble),
                    // not the orbit/repulsion-biased heading. Bot keeps
                    // shooting at target through the orbital motion.
                    if (!retreat && std::fabs(aimDelta) < kFacingFire &&
                        range < kFireRange) {
                        in.fireBasic = 1;
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
                            wanderAngle_[slot]     = a * kTwoPi;
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

                    // 2026-05-30 — wander heading is built from the
                    // rolled angle PLUS the terrain-repulsion gradient
                    // so a wandering bot doesn't grind into a wall
                    // along a randomly-picked vector.
                    const float wsa = std::sin(wanderAngle_[slot]);
                    const float wca = std::cos(wanderAngle_[slot]);
                    float desX = -wsa + repelX * kRepelWeight;
                    float desY =  wca + repelY * kRepelWeight;
                    const float desLen = std::sqrt(desX * desX + desY * desY);
                    const float headingAngle = (desLen > 1e-4f)
                        ? std::atan2(-desX, desY)
                        : wanderAngle_[slot];
                    const float angDelta = wrapPi(headingAngle - curAngleZ);
                    if (angDelta >  kAngEpsilon) in.turnLeft  = 1;
                    if (angDelta < -kAngEpsilon) in.turnRight = 1;
                    if (std::fabs(angDelta) < kWanderFacingThrust) in.thrust = 1;
                }

                // ---- M7.1 CHAOS FIRE -------------------------------
                // Low-probability basic shot dropped on top of any
                // branch that left `in.fireBasic == 0`. Suppressed when
                // already firing aimed (engage hit the fire-arc test),
                // when a special is firing (avoid burning weapon-swap
                // timing), and when we're seeking a repair tile (the
                // bot's busy trying not to die — don't waste cycles).
                if (in.fireBasic   == 0 &&
                    in.fireSpecial == 0 &&
                    !retreat              &&
                    slot < rngBySlot_.size()) {
                    const float r = static_cast<float>(
                        xorshift32(rngBySlot_[slot]) >> 8) /
                        static_cast<float>(1u << 24);
                    if (r < kBotChaosFireChancePerTick) {
                        in.fireBasic = 1;
                    }
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
