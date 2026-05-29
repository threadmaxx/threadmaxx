#include "BulletHomingSystem.hpp"

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Query.hpp>
#include <threadmaxx/World.hpp>

#include <cmath>
#include <cstdint>
#include <limits>

namespace tou2d {

namespace {

struct EnemyTarget {
    float x = 0.0f;
    float y = 0.0f;
    std::uint16_t slot = 0;
};

} // namespace

void BulletHomingSystem::update(threadmaxx::SystemContext& ctx) {
    steeredThisStep_ = 0;

    const auto idsBl = ids_.bullet;
    const auto idsLp = ids_.localPlayer;
    if (!idsBl.valid() || !idsLp.valid()) return;

    ctx.single([&](threadmaxx::Range /*r*/, threadmaxx::CommandBuffer& cb) {
        const auto& view = ctx.worldView();

        // ---- Pass 1: collect every live ship's (position, slot) so the
        //              bullet loop is O(bullets * shipCount) on a small
        //              flat vector instead of re-walking ship chunks
        //              per bullet. Dead ships (DisabledTag) are skipped
        //              so Homer doesn't lock onto a corpse mid-respawn.
        std::array<EnemyTarget, kMaxPlayerSlots> targets{};
        std::size_t targetCount = 0;
        for (const auto* chunkPtr : view.chunks()) {
            if (!chunkPtr) continue;
            const auto& chunk = *chunkPtr;
            if (!chunk.mask.has(threadmaxx::Component::Transform)) continue;
            if (!chunk.mask.has(idsLp.componentBit()))             continue;
            if (chunk.mask.has(threadmaxx::Component::DisabledTag)) continue;

            const auto lpSpan   = threadmaxx::user::chunkSpan<LocalPlayer>(chunk, idsLp);
            const auto entities = chunk.entities;
            const auto& positions = chunk.transforms;
            for (std::size_t row = 0, m = entities.size(); row < m; ++row) {
                if (targetCount >= targets.size()) break;
                targets[targetCount].x    = positions[row].position.x;
                targets[targetCount].y    = positions[row].position.y;
                targets[targetCount].slot = lpSpan[row].slot;
                ++targetCount;
            }
        }

        // No ships → nothing to steer toward; bullets fly straight.
        if (targetCount == 0) return;

        // ---- Pass 2: walk bullet chunks; for each Homer bullet, pick
        //              the nearest enemy (slot != ownerSlot) and rotate
        //              the velocity vector toward it by at most
        //              kHomerTurnPerTickRad.
        for (const auto* chunkPtr : view.chunks()) {
            if (!chunkPtr) continue;
            const auto& chunk = *chunkPtr;
            if (!chunk.mask.has(idsBl.componentBit()))             continue;
            if (!chunk.mask.has(threadmaxx::Component::Transform)) continue;
            if (!chunk.mask.has(threadmaxx::Component::Velocity))  continue;

            const auto blSpan = threadmaxx::user::chunkSpan<Bullet>(chunk, idsBl);
            const auto entities    = chunk.entities;
            const auto& positions  = chunk.transforms;
            const auto& velocities = chunk.velocities;
            const std::size_t n = entities.size();

            for (std::size_t row = 0; row < n; ++row) {
                const auto& blt = blSpan[row];
                if (blt.weaponKind != 10) continue;  // Homer only

                const float bx = positions[row].position.x;
                const float by = positions[row].position.y;

                float bestDistSq = std::numeric_limits<float>::infinity();
                std::size_t bestIdx = targetCount;
                for (std::size_t ti = 0; ti < targetCount; ++ti) {
                    if (targets[ti].slot == blt.ownerSlot) continue;  // skip self
                    const float dx = targets[ti].x - bx;
                    const float dy = targets[ti].y - by;
                    const float d2 = dx * dx + dy * dy;
                    if (d2 < bestDistSq) {
                        bestDistSq = d2;
                        bestIdx    = ti;
                    }
                }
                if (bestIdx == targetCount) continue;  // no enemy yet (1-ship sandbox)

                const auto& v  = velocities[row].linear;
                const float vx = v.x;
                const float vy = v.y;
                const float speed = std::sqrt(vx * vx + vy * vy);
                if (speed <= 0.0f) continue;  // mid-respawn stall safeguard

                const float dx = targets[bestIdx].x - bx;
                const float dy = targets[bestIdx].y - by;
                const float distToTgt = std::sqrt(dx * dx + dy * dy);
                if (distToTgt <= 0.0f) continue;

                const float curAngle  = std::atan2(vy, vx);
                const float wantAngle = std::atan2(dy, dx);
                // Shortest signed angular difference in (-pi, pi].
                float delta = wantAngle - curAngle;
                if (delta >  3.14159265358979323846f) delta -= 6.28318530717958647692f;
                if (delta < -3.14159265358979323846f) delta += 6.28318530717958647692f;
                const float step = (delta >  kHomerTurnPerTickRad) ?  kHomerTurnPerTickRad
                                 : (delta < -kHomerTurnPerTickRad) ? -kHomerTurnPerTickRad
                                 :  delta;
                const float newAngle = curAngle + step;
                threadmaxx::Velocity newV = velocities[row];
                newV.linear.x = std::cos(newAngle) * speed;
                newV.linear.y = std::sin(newAngle) * speed;
                cb.setVelocity(entities[row], newV);
                ++steeredThisStep_;
            }
        }
    });
}

} // namespace tou2d
