#include "HudSystem.hpp"

#include "Input.hpp"

#include <threadmaxx/Engine.hpp>
#include <threadmaxx/Resource.hpp>
#include <threadmaxx/UserComponent.hpp>
#include <threadmaxx/World.hpp>

#include <cstdio>

namespace rpg {

HudSystem::HudSystem(threadmaxx::Engine* engine,
                     WorldState* worldState,
                     UserComponentIds* ids)
    : engine_(engine), worldState_(worldState), ids_(ids) {
    pickupSub_ = engine_->events<PickupCollected>().subscribeScoped(
        [](const PickupCollected& ev) {
            std::printf("[hud] pickup collected — total=%u\n", ev.totalPickups);
        });
    // §3.11.1 batch D1 — kill tracker. Increment the worldState
    // counter so postStep can surface it in the HUD.
    deathSub_ = engine_->events<EntityDied>().subscribeScoped(
        [worldState](const EntityDied& ev) {
            (void)ev;
            ++worldState->totalKills;
        });
    // §3.11.5 batch D5 — skip telemetry. Track per-cosmetic-system
    // skip counts so the HUD can show "skipped Nx" suffixes.
    skippedSub_ = engine_->events<threadmaxx::SystemSkipped>().subscribeScoped(
        [worldState](const threadmaxx::SystemSkipped& ev) {
            if (ev.systemName == "hud")             ++worldState->totalSkippedHud;
            else if (ev.systemName == "debug-overlay") ++worldState->totalSkippedOverlay;
            else if (ev.systemName == "day-night")     ++worldState->totalSkippedDayNight;
        });
    // §3.11.5 batch D5 — frame-budget watcher counter. The watcher
    // system emits one event per over-budget tick; we just tally.
    budgetSub_ = engine_->events<threadmaxx::BudgetExceeded>().subscribeScoped(
        [worldState](const threadmaxx::BudgetExceeded& ev) {
            (void)ev;
            ++worldState->budgetExceededCount;
        });
    // §3.11.4 batch D4 — quest progress notifier. Single-line log
    // per advance; the periodic postStep summary surfaces the full
    // quest list state.
    questSub_ = engine_->events<QuestProgressed>().subscribeScoped(
        [](const QuestProgressed& ev) {
            const char* name = (ev.id == QuestId::CollectPickups)
                ? "Collect pickups" : "Defeat hostiles";
            std::printf("[quest] %s — %u/%u%s\n",
                        name, ev.progress, ev.target,
                        ev.completed ? "  ✓ COMPLETE" : "");
        });
    // §3.11.7 batch D7 — AssetReloaded subscriber. Logs the
    // reload event so a player pressing F12 sees confirmation.
    // The renderer-side pipeline rebuild on AssetReloaded is
    // deferred to a future `batch 9b` renderer batch.
    reloadSub_ = engine_->events<threadmaxx::AssetReloaded>().subscribeScoped(
        [](const threadmaxx::AssetReloaded& ev) {
            std::printf("[asset] reloaded: %u → %u (type=%s)\n",
                        ev.oldIndex, ev.newIndex, ev.type.name());
        });
}

HudSystem::~HudSystem() = default;

void HudSystem::preStep(threadmaxx::SystemContext&) {
    const std::uint32_t edges = input().edges.load(std::memory_order_acquire);
    if (edges & kEdgeTrace) {
        input().edges.fetch_and(~kEdgeTrace, std::memory_order_acq_rel);
        if (trace_) {
            std::printf("[hud] trace stopped (wrote %zu bytes to last file)\n",
                        trace_->bytesWrittenCurrent());
            engine_->setTraceSink(nullptr);
            trace_.reset();
        } else {
            threadmaxx::FileTraceSink::Config cfg;
            cfg.pathTemplate  = "/tmp/rpg_demo_trace.%N.json";
            cfg.rotationBytes = 16u * 1024u * 1024u;
            trace_ = std::make_unique<threadmaxx::FileTraceSink>(cfg);
            engine_->setTraceSink(trace_.get());
            std::printf("[hud] trace started — writing to %s\n",
                        cfg.pathTemplate.c_str());
        }
    }
    if (edges & kEdgeReloadShader) {
        input().edges.fetch_and(~kEdgeReloadShader, std::memory_order_acq_rel);
        // §3.11.7 batch D7: F12 emits a synthetic `AssetReloaded`
        // event to demonstrate the hot-reload event channel from
        // game-side. The real renderer-side pipeline rebuild on
        // this event is deferred to `batch 9b` per FUTURE_WORK.
        // The subscriber installed in the ctor prints a line.
        engine_->events<threadmaxx::AssetReloaded>().emit(
            threadmaxx::AssetReloaded{
                /*oldIndex*/ 0,
                /*oldGeneration*/ 0,
                /*newIndex*/ 1,
                /*newGeneration*/ 1,
                /*type*/ std::type_index(typeid(void)),
            });
    }
}

void HudSystem::postStep(threadmaxx::SystemContext& ctx) {
    const std::uint64_t tick = ctx.tick();
    if (tick - lastLoggedTick_ < 60u) return;
    lastLoggedTick_ = tick;

    const auto& w = ctx.world();
    const auto player = worldState_->player;
    std::uint32_t pickups = 0;
    if (player.valid() && w.alive(player)) {
        const PlayerState* ps =
            threadmaxx::user::tryGet<PlayerState>(w, ids_->playerState, player);
        if (ps) pickups = ps->pickups;
    }

    // §3.11.5 batch D5 — surface budget alerts + skip counts. In
    // non-stress mode these stay at zero and the suffix is empty.
    char budgetBuf[96] = "";
    if (worldState_->stressMode) {
        std::snprintf(budgetBuf, sizeof(budgetBuf),
            " OVER=%u skips[hud=%u,ovr=%u,dn=%u]",
            worldState_->budgetExceededCount,
            worldState_->totalSkippedHud,
            worldState_->totalSkippedOverlay,
            worldState_->totalSkippedDayNight);
    }

    std::printf("[hud] tick=%llu entities=%zu pickups=%u kills=%u sun=%.2f%s%s\n",
                static_cast<unsigned long long>(tick),
                w.entities().size(),
                pickups,
                worldState_->totalKills,
                worldState_->sunAngle,
                trace_ ? "  [TRACING]" : "",
                budgetBuf);

    // §3.11.4 batch D4 — quest progress one-liner. Counts active vs
    // completed; the per-quest detail goes out via the
    // QuestProgressed event subscriber, fired only on advance.
    if (!worldState_->quests.empty()) {
        std::uint32_t completed = 0;
        for (const auto& q : worldState_->quests)
            if (q.completed) ++completed;
        std::printf("[hud] quests: %u/%zu complete\n",
                    completed, worldState_->quests.size());
    }

    // §3.11.7 batch D7 — asset-loader stats from
    // `aggregateLoaderStats`. Includes the renderer's MeshLoader /
    // TextureLoader / ShaderLoader plus the demo's PreloadLoader.
    const auto loaderStats = engine_->aggregateLoaderStats();
    std::printf("[hud] assets: pending=%llu inFlight=%llu ready=%llu "
                "mem=%.1f MiB\n",
                static_cast<unsigned long long>(loaderStats.pendingLoads),
                static_cast<unsigned long long>(loaderStats.inFlight),
                static_cast<unsigned long long>(loaderStats.ready),
                static_cast<double>(loaderStats.memoryFootprint)
                    / (1024.0 * 1024.0));
}

} // namespace rpg
