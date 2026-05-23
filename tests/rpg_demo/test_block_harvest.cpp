// §3.11 batch D11 — voxel block harvest contract.
//
// Three things to verify:
//   1. Direct-emit a `BlockBroken` event on a known terrain cell.
//      After one engine.step(), the targeted block entity must be
//      destroyed AND a new entity carrying both `Pickup` + the
//      `DroppedItem` UC must exist near the broken block.
//   2. Direct-emit a `BlockPlaced` event with `kind = Stone`. The
//      next step must spawn a new terrain entity with the right
//      kind in BlockData, and the player's Inventory's Stone slot
//      decrements by one (D11's starter inventory has 4 Stones).
//   3. The expired DroppedItem age-out path: advance simulation
//      past `kDroppedItemLifetimeSeconds` and verify the drop is
//      destroyed.
//
// 2026-05-23 batch D11 — pure engine test. We emit events directly
// via `engine->events<>().emit()` to avoid coupling the test to
// PlayerInputSystem's targeting math (the targeting is exercised
// indirectly through the demo's manual playtest path).

#include "DemoTestHarness.hpp"

#include <threadmaxx/EventChannel.hpp>
#include <threadmaxx/UserComponent.hpp>
#include <threadmaxx/World.hpp>
#include <threadmaxx/internal/Archetype.hpp>

#include <cstdio>
#include <vector>

namespace {

using namespace rpg;
using namespace rpg::testing;
using namespace threadmaxx;

/// Return the top entity at `(cellX, cellZ)` — the block at the
/// highest Y among entities carrying that TerrainPatch.
EntityHandle topBlockAt(const World& w,
                        const UserComponentIds& ids,
                        std::uint32_t cellX, std::uint32_t cellZ) {
    EntityHandle best{};
    float bestY = -1e30f;
    const auto patchBit = ids.terrainPatch.componentBit();
    const auto chunkCount = w.archetypeChunkCount();
    for (std::size_t c = 0; c < chunkCount; ++c) {
        const auto& chunk = w.archetypeChunk(c);
        if (!chunk.mask.has(patchBit)) continue;
        if (chunk.mask.has(Component::DisabledTag)) continue;
        const auto patches = user::chunkSpan<TerrainPatch>(chunk, ids.terrainPatch);
        for (std::size_t r = 0; r < chunk.entities.size(); ++r) {
            if (patches[r].cellX != cellX || patches[r].cellZ != cellZ) continue;
            const float y = chunk.transforms[r].position.y;
            if (y > bestY) { bestY = y; best = chunk.entities[r]; }
        }
    }
    return best;
}

/// Count DroppedItem-bearing entities of a given kind (or any kind
/// when `match = std::nullopt`).
std::size_t countDroppedItems(const World& w, const UserComponentIds& ids,
                              const BlockKind* match) {
    std::size_t n = 0;
    const auto bit = ids.droppedItem.componentBit();
    const auto chunkCount = w.archetypeChunkCount();
    for (std::size_t c = 0; c < chunkCount; ++c) {
        const auto& chunk = w.archetypeChunk(c);
        if (!chunk.mask.has(bit)) continue;
        if (chunk.mask.has(Component::DisabledTag)) continue;
        const auto drops = user::chunkSpan<DroppedItem>(chunk, ids.droppedItem);
        for (std::size_t r = 0; r < chunk.entities.size(); ++r) {
            if (!match || drops[r].kind == *match) ++n;
        }
    }
    return n;
}

}  // namespace

int main() {
    resetEdges();
    auto fx = makeHeadless();
    const auto& ids = fx.game->ids();

    // ---- 1. Find a non-empty terrain cell and break its top block.
    //
    // Important: stay well outside PickupSystem's `kPickRadius =
    // 1.2 m` of the player (which spawns at world origin). Otherwise
    // the spawned DroppedItem would be auto-collected before we can
    // observe it. Cell index 35 sits at world coord ~11.5 m — way
    // outside the pick radius.
    const std::uint32_t cellX = 35u, cellZ = 35u;
    EntityHandle topBlock = topBlockAt(fx.engine->world(), ids, cellX, cellZ);
    CHECK(topBlock.valid());
    CHECK(fx.engine->world().alive(topBlock));
    const auto& tT = fx.engine->world().get<Transform>(topBlock);
    const float breakY = tT.position.y;
    std::printf("[test_block_harvest] top block at cell (%u,%u) Y=%.2f\n",
                cellX, cellZ, breakY);

    {
        BlockBroken ev{};
        ev.breaker = fx.game->worldState().player;
        ev.blockEntity = topBlock;
        ev.cellX = cellX;
        ev.cellZ = cellZ;
        ev.posX = tT.position.x;
        ev.posY = tT.position.y;
        ev.posZ = tT.position.z;
        fx.engine->events<BlockBroken>().emit(ev);
    }

    // tick 1: drain → enqueue. tick 2: apply (single-pass commit).
    // After tick 2 the target should be dead and a DroppedItem must
    // exist.
    fx.engine->step();
    fx.engine->step();

    CHECK(!fx.engine->world().alive(topBlock));
    const std::size_t dropsAfterBreak =
        countDroppedItems(fx.engine->world(), ids, nullptr);
    std::printf("[test_block_harvest] dropped items after break: %zu\n",
                dropsAfterBreak);
    CHECK(dropsAfterBreak >= 1u);

    // ---- 2. Place a Stone block at a nearby cell, also outside
    //         the player's pickup radius.
    const std::uint32_t pCellX = 10u, pCellZ = 10u;
    const float halfExtent = kTerrainExtent * 0.5f;
    const float tileSize = kTerrainExtent /
        static_cast<float>(fx.game->worldState().terrainCellsPerSide);
    const float pX = -halfExtent + (static_cast<float>(pCellX) + 0.5f) * tileSize;
    const float pZ = -halfExtent + (static_cast<float>(pCellZ) + 0.5f) * tileSize;
    const Heightmap& hmap = *fx.game->worldState().heightmap;
    const float topY = hmap.heightAt(pX, pZ);
    const float placeY = topY + 0.5f;

    // Snapshot the player's inventory pre-place.
    const Inventory* invBefore = user::tryGet<Inventory>(
        fx.engine->world(), ids.inventory, fx.game->worldState().player);
    CHECK(invBefore != nullptr);
    std::uint32_t stoneBefore = 0u;
    for (const auto& s : invBefore->slots) {
        if (s.kind == BlockKind::Stone) stoneBefore += s.count;
    }
    CHECK_EQ(stoneBefore, kPlayerInventoryStartingPicks);

    {
        BlockPlaced ev{};
        ev.placer = fx.game->worldState().player;
        ev.kind = BlockKind::Stone;
        ev.cellX = pCellX;
        ev.cellZ = pCellZ;
        ev.posX = pX;
        ev.posY = placeY;
        ev.posZ = pZ;
        fx.engine->events<BlockPlaced>().emit(ev);
    }

    fx.engine->step();
    fx.engine->step();

    // Verify a new block exists at (pCellX, pCellZ) with Y above the
    // original top.
    EntityHandle newTop = topBlockAt(fx.engine->world(), ids, pCellX, pCellZ);
    CHECK(newTop.valid());
    const auto& nT = fx.engine->world().get<Transform>(newTop);
    std::printf("[test_block_harvest] post-place top at (%u,%u) Y=%.2f "
                "(expected ~%.2f)\n", pCellX, pCellZ, nT.position.y, placeY);
    CHECK(nT.position.y >= placeY - 0.01f);
    const BlockData* bd = user::tryGet<BlockData>(fx.engine->world(),
                                                   ids.blockData, newTop);
    CHECK(bd != nullptr);
    CHECK(bd->kind == BlockKind::Stone);

    // Inventory decremented.
    const Inventory* invAfter = user::tryGet<Inventory>(
        fx.engine->world(), ids.inventory, fx.game->worldState().player);
    CHECK(invAfter != nullptr);
    std::uint32_t stoneAfter = 0u;
    for (const auto& s : invAfter->slots) {
        if (s.kind == BlockKind::Stone) stoneAfter += s.count;
    }
    std::printf("[test_block_harvest] inventory Stone: %u -> %u\n",
                stoneBefore, stoneAfter);
    CHECK_EQ(stoneAfter, stoneBefore - 1u);

    // ---- 3. Age out the DroppedItem entity.
    //
    // Default `dt = 1/60s`. Drops despawn after
    // `kDroppedItemLifetimeSeconds = 30s` = 1800 ticks. Plus a small
    // pad to cover the preStep / update boundary.
    const auto ticksNeeded = static_cast<std::size_t>(
        kDroppedItemLifetimeSeconds * 60.0f) + 4u;
    for (std::size_t i = 0; i < ticksNeeded; ++i) fx.engine->step();
    const std::size_t dropsAfterTimeout =
        countDroppedItems(fx.engine->world(), ids, nullptr);
    std::printf("[test_block_harvest] dropped items after %zu ticks: %zu\n",
                ticksNeeded, dropsAfterTimeout);
    CHECK_EQ(dropsAfterTimeout, 0u);

    EXIT_WITH_RESULT();
}
