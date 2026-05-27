#include "BulletShipCollisionSystem.hpp"

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

    ctx.single([&](threadmaxx::Range /*r*/, threadmaxx::CommandBuffer& cb) {
        const auto& view = ctx.worldView();

        // ---- Pass 1: gather live ships (≤ 4) ----------------------------
        std::array<ShipSlot, 16> ships{};
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
        std::array<std::uint16_t, 16> dmgBySlot{};
        std::array<std::uint8_t,  16> firstShooterBySlot{};
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
                    cb.destroy(entities[row]);
                    break;   // one bullet hits at most one ship per tick
                }
            }
        }

        // ---- Pass 3: apply damage; record killing blow per victim -------
        std::array<std::uint8_t, 16> killerByVictim{};
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
                const float newHp    = victim.currentHp - static_cast<float>(dmg);
                victim.currentHp     = newHp < 0.0f ? 0.0f : newHp;
                threadmaxx::addUserComponent(cb, idsShip, entities[row], victim);

                if (wasAlive && victim.currentHp <= 0.0f) {
                    const std::uint8_t shooter = firstShooterBySlot[slot];
                    if (shooter != kNoShooter && shooter != slot) {
                        killerByVictim[slot] = shooter;
                    }
                }
            }
        }

        // ---- Pass 4: credit kills to shooters ---------------------------
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
                if (addKills == 0) continue;
                Ship shooter = shipSpan[row];
                const std::uint32_t k =
                    static_cast<std::uint32_t>(shooter.kills) + addKills;
                shooter.kills = k > 0xFFFFu ? std::uint16_t{0xFFFFu}
                                            : static_cast<std::uint16_t>(k);
                threadmaxx::addUserComponent(cb, idsShip, entities[row], shooter);

                if (!roundEnded_ && shooter.kills >= kFragLimit) {
                    roundEnded_ = true;
                    RoundEnded ev{};
                    ev.winnerSlot  = shooterSlot;
                    ev.winnerKills = shooter.kills;
                    engine_->events<RoundEnded>().emit(ev);
                    char msg[96];
                    std::snprintf(msg, sizeof msg,
                        "[tou2d] round ended: slot %u reached %u kills",
                        static_cast<unsigned>(shooterSlot),
                        static_cast<unsigned>(shooter.kills));
                    engine_->logger().log(threadmaxx::LogLevel::Warn, msg);
                }
            }
        }
    });
}

} // namespace tou2d
