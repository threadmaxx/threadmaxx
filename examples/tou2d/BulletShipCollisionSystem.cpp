#include "BulletShipCollisionSystem.hpp"

#include "ParticleSystem.hpp"

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Engine.hpp>
#include <threadmaxx/EventChannel.hpp>
#include <threadmaxx/Logger.hpp>
#include <threadmaxx/Query.hpp>
#include <threadmaxx/World.hpp>

#include <array>
#include <cstdio>

namespace tou2d {

namespace {

/// Hit radius factor — ship.transform.scale.x is the cube half-extent
/// in world units (28 wu in M3.5). The original TOU's ships are pixel
/// sprites about 0.85 × the bounding box; 0.45 × scale ≈ 12.6 wu hit
/// radius matches that and gives a small forgiveness margin around the
/// visible silhouette.
constexpr float kShipHitRadiusFactor = 0.45f;

/// Sentinel for "no shooter recorded yet" in the per-victim arrays.
constexpr std::uint8_t kNoShooter = 0xFFu;

// M5.3 — per-weapon spark color (low 24 bits; alpha derived from ttl
// inside ParticleSystem). M5.6 extended the table with the three new
// specials so each weapon kind reads distinct on hit.
//   * Dumbfire (0) — warm yellow
//   * Spread   (1) — orange
//   * Rapid    (2) — cyan
//   * Sniper   (3) — magenta
//   * Quintet  (4) — lime green
constexpr std::uint32_t kSparkColorDumbfire = 0x00FFCC44u;
constexpr std::uint32_t kSparkColorSpread   = 0x00FF8030u;
constexpr std::uint32_t kSparkColorRapid    = 0x0040D0FFu;
constexpr std::uint32_t kSparkColorSniper   = 0x00FF40C0u;
constexpr std::uint32_t kSparkColorQuintet  = 0x0080FF40u;

inline std::uint32_t sparkColorFor(std::uint8_t weaponKind) noexcept {
    switch (weaponKind) {
        case 1: return kSparkColorSpread;
        case 2: return kSparkColorRapid;
        case 3: return kSparkColorSniper;
        case 4: return kSparkColorQuintet;
        default: return kSparkColorDumbfire;
    }
}

struct ShipSlot {
    float          x       = 0.0f;
    float          y       = 0.0f;
    float          radius2 = 0.0f;   ///< squared hit radius
    std::uint8_t   slot    = 0;
};

} // namespace

BulletShipCollisionSystem::BulletShipCollisionSystem(
    UserComponentIds ids, threadmaxx::Engine* engine) noexcept
    : ids_(ids), engine_(engine) {}

void BulletShipCollisionSystem::update(threadmaxx::SystemContext& ctx) {
    const auto idsBl   = ids_.bullet;
    const auto idsLp   = ids_.localPlayer;
    const auto idsShip = ids_.ship;
    if (!idsBl.valid() || !idsLp.valid() || !idsShip.valid()) return;
    if (!engine_)                                              return;

    // Latch the mode at the top of the tick — we hash a few sites
    // below; cheaper to dereference once.
    const MatchMode mode = matchMode_ ? *matchMode_ : MatchMode::Deathmatch;
    const bool roundAlreadyEnded =
        roundEnded_ && roundEnded_->load(std::memory_order_acquire);

    ctx.single([&](threadmaxx::Range /*r*/, threadmaxx::CommandBuffer& cb) {
        const auto& view = ctx.worldView();

        // ---- Pass 1: gather live ships (capped at kMaxPlayerSlots) ----
        std::array<ShipSlot, kMaxPlayerSlots> ships{};
        std::size_t              nShips = 0;
        for (const auto* chunkPtr : view.chunks()) {
            if (!chunkPtr) continue;
            const auto& chunk = *chunkPtr;
            if (!chunk.mask.has(idsLp.componentBit()))               continue;
            if (!chunk.mask.has(idsShip.componentBit()))             continue;
            if (!chunk.mask.has(threadmaxx::Component::Transform))   continue;
            if (chunk.mask.has(threadmaxx::Component::DisabledTag))  continue;  // dead

            const auto lpSpan   = threadmaxx::user::chunkSpan<LocalPlayer>(chunk, idsLp);
            const auto entities = chunk.entities;
            const auto& positions = chunk.transforms;
            for (std::size_t row = 0, n = entities.size(); row < n; ++row) {
                if (nShips >= ships.size()) break;
                const float r = positions[row].scale.x * kShipHitRadiusFactor;
                ships[nShips] = ShipSlot{
                    positions[row].position.x,
                    positions[row].position.y,
                    r * r,
                    lpSpan[row].slot,
                };
                ++nShips;
            }
        }

        if (nShips == 0) return;

        // ---- Pass 2: bullet hit-test ------------------------------------
        // For each victim slot, accumulate total damage taken this tick
        // and remember the FIRST shooter to land a hit (used for kill
        // credit in pass 4 if HP crosses zero). "First-hit" is a
        // gameplay simplification — in practice a player who follows up
        // a teammate's softening shots gets the cleanup; the alternate
        // ("largest-damage hit wins") needs per-bullet bookkeeping and
        // doesn't change the outcome for 4P local play.
        std::array<std::uint16_t, kMaxPlayerSlots> dmgBySlot{};
        std::array<std::uint8_t,  kMaxPlayerSlots> firstShooterBySlot{};
        for (auto& s : firstShooterBySlot) s = kNoShooter;

        for (const auto* chunkPtr : view.chunks()) {
            if (!chunkPtr) continue;
            const auto& chunk = *chunkPtr;
            if (!chunk.mask.has(idsBl.componentBit()))             continue;
            if (!chunk.mask.has(threadmaxx::Component::Transform)) continue;

            const auto blSpan = threadmaxx::user::chunkSpan<Bullet>(chunk, idsBl);
            const auto entities    = chunk.entities;
            const auto& positions  = chunk.transforms;
            for (std::size_t row = 0, n = entities.size(); row < n; ++row) {
                const float bx = positions[row].position.x;
                const float by = positions[row].position.y;
                const Bullet& blt = blSpan[row];

                for (std::size_t i = 0; i < nShips; ++i) {
                    const auto& sh = ships[i];
                    if (sh.slot == blt.ownerSlot)        continue;  // no FF

                    const float dx = bx - sh.x;
                    const float dy = by - sh.y;
                    if (dx * dx + dy * dy > sh.radius2)  continue;

                    if (sh.slot < dmgBySlot.size()) {
                        const std::uint32_t sum =
                            static_cast<std::uint32_t>(dmgBySlot[sh.slot]) +
                            static_cast<std::uint32_t>(blt.damage);
                        dmgBySlot[sh.slot] =
                            sum > 0xFFFFu ? std::uint16_t{0xFFFFu}
                                          : static_cast<std::uint16_t>(sum);
                        if (firstShooterBySlot[sh.slot] == kNoShooter) {
                            firstShooterBySlot[sh.slot] =
                                static_cast<std::uint8_t>(blt.ownerSlot & 0xFFu);
                        }
                    }
                    // M5.3 — impact spark at the bullet's hit point.
                    // M5.6 — per-kind palette via sparkColorFor.
                    if (particles_) {
                        particles_->emitImpactSpark(
                            bx, by, sparkColorFor(blt.weaponKind));
                    }
                    cb.destroy(entities[row]);
                    break;   // one bullet hits at most one ship per tick
                }
            }
        }

        // ---- Pass 3: apply damage; record killing blow per victim -------
        std::array<std::uint8_t, kMaxPlayerSlots> killerByVictim{};
        for (auto& v : killerByVictim) v = kNoShooter;

        for (const auto* chunkPtr : view.chunks()) {
            if (!chunkPtr) continue;
            const auto& chunk = *chunkPtr;
            if (!chunk.mask.has(idsLp.componentBit()))               continue;
            if (!chunk.mask.has(idsShip.componentBit()))             continue;
            if (chunk.mask.has(threadmaxx::Component::DisabledTag))  continue;

            const auto lpSpan   = threadmaxx::user::chunkSpan<LocalPlayer>(chunk, idsLp);
            const auto shipSpan = threadmaxx::user::chunkSpan<Ship>(chunk, idsShip);
            const auto entities = chunk.entities;
            for (std::size_t row = 0, n = entities.size(); row < n; ++row) {
                const std::uint8_t slot = lpSpan[row].slot;
                if (slot >= dmgBySlot.size())  continue;
                const std::uint16_t dmg = dmgBySlot[slot];
                if (dmg == 0)                  continue;

                Ship victim = shipSpan[row];
                const bool  wasAlive = victim.currentHp > 0.0f;
                // N4 — settings-driven damage scale. Default 1.0
                // reproduces the pre-N4 raw `blt.damage` behaviour.
                const float scaledDmg = static_cast<float>(dmg) * damageScale_;
                const float newHp    = victim.currentHp - scaledDmg;
                victim.currentHp     = newHp < 0.0f ? 0.0f : newHp;
                threadmaxx::addUserComponent(cb, idsShip, entities[row], victim);

                // N6 (2026-06-18) — scoreboard accumulators. Damage
                // taken always credits the victim; damage dealt is
                // credited to the firstShooter for this victim this
                // tick (the same shooter who gets kill credit on a
                // fatal hit — keeps the bookkeeping symmetric with
                // the existing "first shot wins" credit policy).
                const std::uint32_t applied = static_cast<std::uint32_t>(
                    std::min<float>(scaledDmg, 65535.0f));
                if (slot < damageTakenBySlot_.size()) {
                    const std::uint32_t sum =
                        static_cast<std::uint32_t>(damageTakenBySlot_[slot]) + applied;
                    damageTakenBySlot_[slot] =
                        sum > 0xFFFFu ? std::uint16_t{0xFFFFu}
                                      : static_cast<std::uint16_t>(sum);
                }
                const std::uint8_t firstShooter = firstShooterBySlot[slot];
                if (firstShooter != kNoShooter &&
                    firstShooter < damageDealtBySlot_.size()) {
                    const std::uint32_t sum =
                        static_cast<std::uint32_t>(damageDealtBySlot_[firstShooter]) + applied;
                    damageDealtBySlot_[firstShooter] =
                        sum > 0xFFFFu ? std::uint16_t{0xFFFFu}
                                      : static_cast<std::uint16_t>(sum);
                }

                // M4.8 — audio cue. Hit on every damage tick; explode on
                // the transition from alive → 0 HP.
                if (engine_) {
                    engine_->events<AudioPlay>().emit(
                        AudioPlay{audio::kSoundHit, 0, 0});
                    if (wasAlive && victim.currentHp <= 0.0f) {
                        engine_->events<AudioPlay>().emit(
                            AudioPlay{audio::kSoundExplode, 0, 0});
                    }
                }

                if (wasAlive && victim.currentHp <= 0.0f) {
                    // N6 — death increment per victim slot.
                    if (slot < deathsBySlot_.size() &&
                        deathsBySlot_[slot] < 0xFFFFu) {
                        ++deathsBySlot_[slot];
                    }
                    const std::uint8_t shooter = firstShooterBySlot[slot];
                    if (shooter != kNoShooter && shooter != slot) {
                        killerByVictim[slot] = shooter;
                        // M6.8 — broadcast a kill-feed toast. Render-only,
                        // never round-tripped through WorldSnapshot, so
                        // commitHash is unaffected.
                        if (engine_) {
                            UIToast t{};
                            t.slot          = kToastSlotBroadcast;
                            t.severity      = 0;
                            t.durationTicks = 180;  // 3 s @ 60 Hz
                            std::snprintf(
                                t.message.data(), t.message.size(),
                                "P%u fragged P%u",
                                static_cast<unsigned>(shooter),
                                static_cast<unsigned>(slot));
                            engine_->events<UIToast>().emit(t);
                        }
                    }
                }
            }
        }

        // ---- Pass 4: credit kills to shooters + DM win check ------------
        // M4.3 — also rolls up the kill totals so we can decide the LSS
        // mutual-annihilation winner without a second walk. Per-slot
        // post-tick kill totals reflect any +1 we apply this pass; for
        // a slot that didn't score this tick, the chunk's existing
        // value is reused.
        std::array<std::uint16_t, kMaxPlayerSlots> killsBySlotPostTick{};
        bool                         dmRoundEnded = false;
        std::uint8_t                 dmWinnerSlot = 0;
        std::uint16_t                dmWinnerKills = 0;

        for (const auto* chunkPtr : view.chunks()) {
            if (!chunkPtr) continue;
            const auto& chunk = *chunkPtr;
            if (!chunk.mask.has(idsLp.componentBit()))    continue;
            if (!chunk.mask.has(idsShip.componentBit()))  continue;
            // Shooter may be alive OR dead — they still get the credit.

            const auto lpSpan   = threadmaxx::user::chunkSpan<LocalPlayer>(chunk, idsLp);
            const auto shipSpan = threadmaxx::user::chunkSpan<Ship>(chunk, idsShip);
            const auto entities = chunk.entities;
            for (std::size_t row = 0, n = entities.size(); row < n; ++row) {
                const std::uint8_t shooterSlot = lpSpan[row].slot;
                std::uint16_t addKills = 0;
                for (std::size_t v = 0; v < killerByVictim.size(); ++v) {
                    if (killerByVictim[v] == shooterSlot) ++addKills;
                }

                Ship shooter = shipSpan[row];
                if (addKills > 0) {
                    const std::uint32_t k =
                        static_cast<std::uint32_t>(shooter.kills) + addKills;
                    shooter.kills = k > 0xFFFFu ? std::uint16_t{0xFFFFu}
                                                : static_cast<std::uint16_t>(k);
                    threadmaxx::addUserComponent(cb, idsShip, entities[row], shooter);
                }

                if (shooterSlot < killsBySlotPostTick.size()) {
                    killsBySlotPostTick[shooterSlot] = shooter.kills;
                }

                if (mode == MatchMode::Deathmatch && !roundAlreadyEnded &&
                    !dmRoundEnded && shooter.kills >= kFragLimit) {
                    dmRoundEnded   = true;
                    dmWinnerSlot   = shooterSlot;
                    dmWinnerKills  = shooter.kills;
                }
            }
        }

        // ---- Pass 5: LSS post-damage survivor census --------------------
        // Count ships with currentHp > 0 right after this tick's damage.
        // ≤ 1 alive triggers round-end. Winner = surviving slot if
        // exactly one; on mutual annihilation (zero survivors) pick the
        // slot with the most kills (deterministic tiebreak: lowest slot
        // index wins ties).
        bool          lssRoundEnded  = false;
        std::uint8_t  lssWinnerSlot  = 0;
        std::uint16_t lssWinnerKills = 0;
        if (mode == MatchMode::LastShipStanding && !roundAlreadyEnded) {
            std::uint32_t aliveCount = 0;
            std::uint8_t  lastAliveSlot = 0;
            for (const auto* chunkPtr : view.chunks()) {
                if (!chunkPtr) continue;
                const auto& chunk = *chunkPtr;
                if (!chunk.mask.has(idsLp.componentBit()))    continue;
                if (!chunk.mask.has(idsShip.componentBit()))  continue;
                // A ship that was already DisabledTag at the top of the
                // tick is permanently out; skip — its currentHp is
                // whatever it was on death (0). A ship that just hit 0
                // HP this tick is still in the !DisabledTag chunk; its
                // post-damage write above lowered currentHp to 0, so
                // the alive check correctly excludes it.
                if (chunk.mask.has(threadmaxx::Component::DisabledTag)) continue;

                const auto lpSpan   = threadmaxx::user::chunkSpan<LocalPlayer>(chunk, idsLp);
                const auto shipSpan = threadmaxx::user::chunkSpan<Ship>(chunk, idsShip);
                for (std::size_t row = 0, n = lpSpan.size(); row < n; ++row) {
                    const std::uint8_t slot = lpSpan[row].slot;
                    const std::uint16_t dmg = slot < dmgBySlot.size()
                                                  ? dmgBySlot[slot]
                                                  : std::uint16_t{0};
                    // Recompute post-tick HP using the same dmg sum
                    // pass 3 applied (the chunkSpan we hold reads the
                    // PRE-write value).
                    const float preHp = shipSpan[row].currentHp;
                    const float postHp =
                        preHp > 0.0f ? preHp - static_cast<float>(dmg) : preHp;
                    if (postHp > 0.0f) {
                        ++aliveCount;
                        lastAliveSlot = slot;
                    }
                }
            }
            if (aliveCount <= 1) {
                lssRoundEnded = true;
                if (aliveCount == 1) {
                    lssWinnerSlot = lastAliveSlot;
                    lssWinnerKills =
                        lastAliveSlot < killsBySlotPostTick.size()
                            ? killsBySlotPostTick[lastAliveSlot]
                            : std::uint16_t{0};
                } else {
                    // Mutual annihilation — pick the highest kill count
                    // (lowest slot wins ties).
                    std::uint8_t  best     = 0;
                    std::uint16_t bestKill = 0;
                    for (std::size_t i = 0; i < killsBySlotPostTick.size(); ++i) {
                        if (killsBySlotPostTick[i] > bestKill) {
                            bestKill = killsBySlotPostTick[i];
                            best     = static_cast<std::uint8_t>(i);
                        }
                    }
                    lssWinnerSlot  = best;
                    lssWinnerKills = bestKill;
                }
            }
        }

        // ---- Emit round-end (if any) ------------------------------------
        if ((dmRoundEnded || lssRoundEnded) && roundEnded_) {
            const std::uint8_t  winSlot  = dmRoundEnded ? dmWinnerSlot  : lssWinnerSlot;
            const std::uint16_t winKills = dmRoundEnded ? dmWinnerKills : lssWinnerKills;
            roundEnded_->store(true, std::memory_order_release);
            if (winnerSlot_)  *winnerSlot_  = winSlot;
            if (winnerKills_) *winnerKills_ = winKills;

            RoundEnded ev{};
            ev.winnerSlot  = winSlot;
            ev.winnerKills = winKills;
            engine_->events<RoundEnded>().emit(ev);

            const char* modeStr =
                mode == MatchMode::LastShipStanding ? "LSS" : "DM";
            char msg[96];
            std::snprintf(msg, sizeof msg,
                "[tou2d] round ended (%s): slot %u with %u kills",
                modeStr,
                static_cast<unsigned>(winSlot),
                static_cast<unsigned>(winKills));
            engine_->logger().log(threadmaxx::LogLevel::Warn, msg);
        }
    });
}

} // namespace tou2d
