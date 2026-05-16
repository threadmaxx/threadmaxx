#include "QuestSystem.hpp"

#include <threadmaxx/Engine.hpp>

namespace rpg {

namespace {

void bumpQuest(WorldState& ws, threadmaxx::Engine& engine,
               QuestId id, std::uint32_t inc) {
    for (auto& q : ws.quests) {
        if (q.id != id) continue;
        if (q.completed) return;
        const std::uint32_t before = q.progress;
        q.progress = std::min(q.progress + inc, q.target);
        const bool justCompleted = !q.completed && q.progress >= q.target;
        if (justCompleted) q.completed = true;
        if (q.progress != before || justCompleted) {
            engine.events<QuestProgressed>().emit(QuestProgressed{
                q.id, q.progress, q.target, q.completed
            });
        }
        return;
    }
}

} // namespace

QuestSystem::QuestSystem(threadmaxx::Engine* engine, WorldState* worldState)
    : engine_(engine), worldState_(worldState) {
    pickupSub_ = engine_->events<PickupCollected>().subscribeScoped(
        [this](const PickupCollected& ev) {
            bumpQuest(*worldState_, *engine_,
                      QuestId::CollectPickups, ev.value);
        });
    deathSub_ = engine_->events<EntityDied>().subscribeScoped(
        [this](const EntityDied& ev) {
            (void)ev;
            bumpQuest(*worldState_, *engine_, QuestId::KillHostiles, 1u);
        });
}

QuestSystem::~QuestSystem() = default;

} // namespace rpg
