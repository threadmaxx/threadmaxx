#include "DamageSystem.hpp"

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Engine.hpp>
#include <threadmaxx/EventChannel.hpp>
#include <threadmaxx/World.hpp>

#include <algorithm>
#include <utility>
#include <vector>

namespace rpg {

void DamageSystem::preStep(threadmaxx::SystemContext& ctx) {
    damageThisTick_.clear();
    // §3.11.1 batch D1 — drain `DamageDealt` from this tick's emitters.
    // `drainTick` returns a span into the front buffer; we accumulate
    // hits per target so multiple hits in one tick compose into one
    // Health write below.
    auto evs = engine_->events<DamageDealt>().drainTick();
    for (const auto& d : evs) {
        auto& p = damageThisTick_[d.target];
        p.attacker = d.attacker;
        p.amount  += d.amount;
        // Last-hit position wins; good enough for the floating
        // damage text overlay.
        p.posX = d.posX; p.posY = d.posY; p.posZ = d.posZ;
    }
    (void)ctx;
}

void DamageSystem::update(threadmaxx::SystemContext& ctx) {
    if (damageThisTick_.empty()) return;
    const auto& w = ctx.world();
    auto& chDied = engine_->events<EntityDied>();

    // Snapshot of pending hits (handles + amounts + preserved max)
    // so we can move into the single() lambda safely. `setHealth`
    // overwrites BOTH `current` AND `max`, so we read the pre-hit
    // max here and forward it to the write below.
    struct Apply {
        threadmaxx::EntityHandle target;
        threadmaxx::EntityHandle attacker;
        float                    newCurrent = 0.0f;
        float                    maxHp      = 0.0f;
        bool                     killBlow   = false;
        float                    posX = 0.0f, posY = 0.0f, posZ = 0.0f;
    };
    std::vector<Apply> applies;
    applies.reserve(damageThisTick_.size());
    for (const auto& [target, hit] : damageThisTick_) {
        if (!w.alive(target)) continue;
        const threadmaxx::Health* hp = w.tryGetHealth(target);
        if (!hp) continue;
        Apply a;
        a.target     = target;
        a.attacker   = hit.attacker;
        a.newCurrent = std::max(0.0f, hp->current - hit.amount);
        a.maxHp      = hp->max;
        a.killBlow   = (hp->current > 0.0f && a.newCurrent <= 0.0f);
        a.posX = hit.posX; a.posY = hit.posY; a.posZ = hit.posZ;
        applies.push_back(a);
    }
    if (applies.empty()) return;

    ctx.single([applies = std::move(applies), &chDied]
               (threadmaxx::Range, threadmaxx::CommandBuffer& cb) {
        for (const auto& a : applies) {
            cb.setHealth(a.target,
                threadmaxx::Health{a.newCurrent, a.maxHp});
        }
        for (const auto& a : applies) {
            if (a.killBlow) {
                EntityDied ev;
                ev.entity = a.target;
                ev.killer = a.attacker;
                ev.posX = a.posX;
                ev.posY = a.posY;
                ev.posZ = a.posZ;
                chDied.emit(ev);
            }
        }
    });
}

} // namespace rpg
