#include "SaveLoadSystem.hpp"

#include "Input.hpp"

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Components.hpp>
#include <threadmaxx/Serialization.hpp>
#include <threadmaxx/World.hpp>

#include <cstdio>
#include <fstream>

namespace rpg {

SaveLoadSystem::SaveLoadSystem(WorldState* worldState,
                               UserComponentIds* ids,
                               std::filesystem::path savePath)
    : worldState_(worldState), ids_(ids), savePath_(std::move(savePath)) {}

void SaveLoadSystem::preStep(threadmaxx::SystemContext& ctx) {
    const std::uint32_t edges = input().edges.load(std::memory_order_acquire);
    if (edges & kEdgeSaveQuick) {
        input().edges.fetch_and(~kEdgeSaveQuick, std::memory_order_acq_rel);
        save_(ctx);
    }
    if (edges & kEdgeLoadQuick) {
        input().edges.fetch_and(~kEdgeLoadQuick, std::memory_order_acq_rel);
        load_(ctx);
    }
}

void SaveLoadSystem::save_(threadmaxx::SystemContext& ctx) {
    const threadmaxx::WorldSnapshot snap = ctx.world().snapshot();
    std::ofstream f(savePath_, std::ios::binary | std::ios::trunc);
    if (!f) {
        std::fprintf(stderr, "[save] cannot open %s for writing\n",
                     savePath_.string().c_str());
        return;
    }
    threadmaxx::serialize(f, snap);
    std::printf("[save] wrote %zu entities (built-in components only) to %s\n",
                snap.size(), savePath_.string().c_str());
}

void SaveLoadSystem::load_(threadmaxx::SystemContext& ctx) {
    std::ifstream f(savePath_, std::ios::binary);
    if (!f) {
        std::fprintf(stderr, "[load] no save at %s\n", savePath_.string().c_str());
        return;
    }
    threadmaxx::WorldSnapshot snap;
    if (!threadmaxx::deserialize(f, snap)) {
        std::fprintf(stderr, "[load] %s is not a valid save\n",
                     savePath_.string().c_str());
        return;
    }

    // The §3.1 batch 6b docs call user-component serialization out as a
    // game-side responsibility. Loading would tear down every entity
    // and respawn from the built-in snapshot, but the resulting world
    // would have no CubeRender / NpcState / PlayerState / Pickup
    // attached — nothing would render or move. Demonstrating the
    // round-trip without breaking the live demo: count what we read
    // back and print a diagnostic summary by faction.
    (void)ctx;
    std::uint32_t byFaction[4] = {0, 0, 0, 0};
    for (std::size_t i = 0; i < snap.size(); ++i) {
        const auto f_id = snap.factions[i].id;
        if (f_id < 4) ++byFaction[f_id];
    }
    std::printf("[load] read %zu entities from %s — "
                "player=%u friendly=%u hostile=%u neutral=%u "
                "(live world unchanged; UserComponent persistence is game-side)\n",
                snap.size(), savePath_.string().c_str(),
                byFaction[0], byFaction[1], byFaction[2], byFaction[3]);
}

} // namespace rpg
