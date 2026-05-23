#pragma once

// §3.11 batch D11 — applies voxel block break / place events.
//
// `BlockEditSystem` owns the demo's per-column entity index, drains
// `BlockBroken` / `BlockPlaced` from the typed event channel and
// applies the resulting destroy + spawn through a CommandBuffer. It
// also walks the DroppedItem chunk each tick to age out unclaimed
// drops and ingests `PickupCollected{isDrop = 1}` events to credit
// the player's `Inventory`.
//
// Index lifecycle. The first `preStep` after registration scans the
// world for entities with `TerrainPatch` + `BlockData` UCs and
// populates a flat `columns_` vector indexed by `cellZ * cellsPerSide
// + cellX`. Each cell holds an ascending-Y stack of entity handles
// (bottom block at index 0). Subsequent breaks / places mutate the
// stack inline.
//
// Determinism. Break + place are applied via `ctx.single(...)` so
// the chunk migration order is fixed. The event queue is FIFO per
// emitter, which mirrors the rest of the demo (e.g. DamageSystem).

#include <threadmaxx/System.hpp>

#include "DemoTypes.hpp"

#include <vector>

namespace threadmaxx { class Engine; }

namespace rpg {

class BlockEditSystem : public threadmaxx::ISystem {
public:
    BlockEditSystem(threadmaxx::Engine* engine,
                    WorldState* worldState,
                    UserComponentIds* ids)
        : engine_(engine), worldState_(worldState), ids_(ids) {}

    const char* name() const noexcept override { return "block-edit"; }
    /// Writes-all to compose with the player's other UC writers
    /// (PickupSystem, PlayerInputSystem) — Inventory mutation is a
    /// chunk migration on the player.
    threadmaxx::ComponentSet writes() const noexcept override {
        return threadmaxx::ComponentSet::all();
    }
    threadmaxx::ComponentSet reads() const noexcept override {
        return threadmaxx::ComponentSet::all();
    }

    void preStep(threadmaxx::SystemContext& ctx) override;
    void update (threadmaxx::SystemContext& ctx) override;

private:
    void ensureColumnIndex(threadmaxx::SystemContext& ctx);

    threadmaxx::Engine* engine_     = nullptr;
    WorldState*         worldState_ = nullptr;
    UserComponentIds*   ids_        = nullptr;

    bool columnsBuilt_ = false;
    /// Per-cell entity stacks. Flat layout: `columns_[cz * cells + cx]`
    /// is a vector of EntityHandles sorted by Y (ascending).
    std::vector<std::vector<threadmaxx::EntityHandle>> columns_;

    /// Pending operations decoded by preStep, applied by update.
    struct PendingBreak {
        threadmaxx::EntityHandle blockEntity;
        BlockKind                kind;
        float                    posX, posY, posZ;
        std::uint32_t            cellX, cellZ;
    };
    struct PendingPlace {
        threadmaxx::EntityHandle placer;
        BlockKind                kind;
        float                    posX, posY, posZ;
        std::uint32_t            cellX, cellZ;
    };
    std::vector<PendingBreak>                      pendingBreaks_;
    std::vector<PendingPlace>                      pendingPlaces_;
    std::vector<threadmaxx::EntityHandle>          pendingExpiredDrops_;
    /// Inventory credits from drained PickupCollected events.
    std::vector<std::pair<threadmaxx::EntityHandle, BlockKind>> pendingInvCredits_;
};

} // namespace rpg
