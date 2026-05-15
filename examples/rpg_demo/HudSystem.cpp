#include "HudSystem.hpp"

#include "Input.hpp"

#include <threadmaxx/Engine.hpp>
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

    std::printf("[hud] tick=%llu entities=%zu pickups=%u sun=%.2f%s\n",
                static_cast<unsigned long long>(tick),
                w.entities().size(),
                pickups,
                worldState_->sunAngle,
                trace_ ? "  [TRACING]" : "");
}

} // namespace rpg
