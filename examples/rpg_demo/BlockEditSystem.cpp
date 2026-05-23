#include "BlockEditSystem.hpp"

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Components.hpp>
#include <threadmaxx/Engine.hpp>
#include <threadmaxx/EventChannel.hpp>
#include <threadmaxx/UserComponent.hpp>
#include <threadmaxx/World.hpp>
#include <threadmaxx/internal/Archetype.hpp>

#include <algorithm>
#include <utility>

namespace rpg {

namespace {

constexpr float kBlockUnitMeters = 1.0f;  // matches Heightmap::blockUnit()

inline std::size_t cellIndex(std::uint32_t cellX, std::uint32_t cellZ,
                             std::uint32_t cellsPerSide) noexcept {
    return static_cast<std::size_t>(cellZ) * cellsPerSide + cellX;
}

}  // namespace

void BlockEditSystem::ensureColumnIndex(threadmaxx::SystemContext& ctx) {
    if (columnsBuilt_) return;
    const std::uint32_t cells = worldState_->terrainCellsPerSide;
    if (cells == 0u) return;
    columns_.assign(static_cast<std::size_t>(cells) * cells, {});

    // Walk terrain chunks once. Pre-pivot terrain entities all carried
    // `TerrainPatch`; D10 added `BlockData` to every terrain block, so
    // the join `TerrainPatch ∧ BlockData ∧ Transform` IS the demo's
    // voxel-terrain population.
    const auto& w = ctx.world();
    const auto patchBit = ids_->terrainPatch.componentBit();
    const auto blockBit = ids_->blockData.componentBit();
    const auto chunkCount = w.archetypeChunkCount();
    for (std::size_t c = 0; c < chunkCount; ++c) {
        const auto& chunk = w.archetypeChunk(c);
        if (!chunk.mask.has(patchBit)) continue;
        if (!chunk.mask.has(blockBit)) continue;
        const auto patches = threadmaxx::user::chunkSpan<TerrainPatch>(
            chunk, ids_->terrainPatch);
        const auto& transforms = chunk.transforms;
        const auto& ents       = chunk.entities;
        for (std::size_t r = 0; r < ents.size(); ++r) {
            const auto& p = patches[r];
            if (p.cellX >= cells || p.cellZ >= cells) continue;
            columns_[cellIndex(p.cellX, p.cellZ, cells)].push_back(ents[r]);
        }
    }
    // Sort each column bottom→top by stored Y so `back()` is always
    // the surface block.
    for (std::size_t i = 0; i < columns_.size(); ++i) {
        auto& col = columns_[i];
        std::sort(col.begin(), col.end(),
                  [&](threadmaxx::EntityHandle a, threadmaxx::EntityHandle b) {
                      const auto* ta = w.tryGetTransform(a);
                      const auto* tb = w.tryGetTransform(b);
                      const float ya = ta ? ta->position.y : 0.0f;
                      const float yb = tb ? tb->position.y : 0.0f;
                      return ya < yb;
                  });
    }
    columnsBuilt_ = true;
}

void BlockEditSystem::preStep(threadmaxx::SystemContext& ctx) {
    pendingBreaks_.clear();
    pendingPlaces_.clear();
    pendingExpiredDrops_.clear();
    pendingInvCredits_.clear();

    ensureColumnIndex(ctx);

    const std::uint32_t cells = worldState_->terrainCellsPerSide;
    if (cells == 0u) return;
    const auto& w = ctx.world();

    // ---- Drain BlockBroken events. Resolve cell → entity from the
    //      column stack; fill in `kind` from BlockData if the caller
    //      didn't specify one.
    {
        auto evs = engine_->events<BlockBroken>().drainTick();
        for (const auto& ev : evs) {
            if (ev.cellX >= cells || ev.cellZ >= cells) continue;
            auto& col = columns_[cellIndex(ev.cellX, ev.cellZ, cells)];
            threadmaxx::EntityHandle target = ev.blockEntity;
            if (!target.valid() || !w.alive(target)) {
                if (col.empty()) continue;
                target = col.back();
            }
            if (!target.valid() || !w.alive(target)) continue;
            BlockKind kind = ev.kind;
            if (const auto* bd = threadmaxx::user::tryGet<BlockData>(
                    w, ids_->blockData, target)) {
                kind = bd->kind;
            }
            PendingBreak pb{};
            pb.blockEntity = target;
            pb.kind        = kind;
            pb.posX = ev.posX;
            pb.posY = ev.posY;
            pb.posZ = ev.posZ;
            pb.cellX = ev.cellX;
            pb.cellZ = ev.cellZ;
            pendingBreaks_.push_back(pb);
            // Eager pop so a subsequent BlockBroken on the same cell
            // in the same tick (rare but possible) resolves to the
            // next-lower block.
            if (!col.empty() && col.back() == target) col.pop_back();
        }
    }

    // ---- Drain BlockPlaced events. Caller supplies kind explicitly;
    //      we just enqueue the spawn.
    {
        auto evs = engine_->events<BlockPlaced>().drainTick();
        for (const auto& ev : evs) {
            if (ev.cellX >= cells || ev.cellZ >= cells) continue;
            PendingPlace pp{};
            pp.placer = ev.placer;
            pp.kind   = ev.kind;
            pp.posX = ev.posX;
            pp.posY = ev.posY;
            pp.posZ = ev.posZ;
            pp.cellX = ev.cellX;
            pp.cellZ = ev.cellZ;
            pendingPlaces_.push_back(pp);
        }
    }

    // ---- Drain PickupCollected for the isDrop path. We DON'T own
    //      the event channel; we subscribe-and-drain just like
    //      DamageSystem does for DamageDealt. PickupSystem may have
    //      already drained it this tick — but `drainTick` returns the
    //      front-buffer span and is idempotent across observers (it
    //      doesn't pop). For v1 we rely on PickupSystem ordering: it
    //      runs in a different wave and emits AFTER its drain, so
    //      `drainTick` here sees the previous-tick events. That's the
    //      expected pattern — single-tick latency.
    {
        auto evs = engine_->events<PickupCollected>().drainTick();
        for (const auto& ev : evs) {
            if (ev.isDrop == 0u) continue;
            pendingInvCredits_.emplace_back(ev.player, ev.dropKind);
        }
    }

    // ---- Age out unclaimed DroppedItem entities.
    const float simTime = static_cast<float>(ctx.tick()) *
                          static_cast<float>(ctx.dt());
    const auto dropBit = ids_->droppedItem.componentBit();
    const auto chunkCount = w.archetypeChunkCount();
    for (std::size_t c = 0; c < chunkCount; ++c) {
        const auto& chunk = w.archetypeChunk(c);
        if (!chunk.mask.has(dropBit)) continue;
        if (chunk.mask.has(threadmaxx::Component::DisabledTag)) continue;
        const auto drops = threadmaxx::user::chunkSpan<DroppedItem>(
            chunk, ids_->droppedItem);
        const auto& ents = chunk.entities;
        for (std::size_t r = 0; r < ents.size(); ++r) {
            const float age = simTime - drops[r].spawnTimeSeconds;
            if (age > kDroppedItemLifetimeSeconds) {
                pendingExpiredDrops_.push_back(ents[r]);
            }
        }
    }
}

void BlockEditSystem::update(threadmaxx::SystemContext& ctx) {
    if (pendingBreaks_.empty() &&
        pendingPlaces_.empty() &&
        pendingExpiredDrops_.empty() &&
        pendingInvCredits_.empty()) return;

    const std::uint32_t cells = worldState_->terrainCellsPerSide;
    const float simTime = static_cast<float>(ctx.tick()) *
                          static_cast<float>(ctx.dt());

    // Reserve handles BEFORE the lambda — `reserveEntityHandle` is
    // mutex-protected on the engine and meant to be called from the
    // sim thread, not from inside a `single` lambda. Same pattern as
    // ParticleEmitterSystem.
    std::vector<threadmaxx::EntityHandle> dropHandles;
    dropHandles.reserve(pendingBreaks_.size());
    for (std::size_t i = 0; i < pendingBreaks_.size(); ++i) {
        dropHandles.push_back(engine_->reserveEntityHandle());
    }
    std::vector<threadmaxx::EntityHandle> placeHandles;
    placeHandles.reserve(pendingPlaces_.size());
    for (std::size_t i = 0; i < pendingPlaces_.size(); ++i) {
        placeHandles.push_back(engine_->reserveEntityHandle());
    }

    // Compute inventory writes outside the lambda (chunk-migrating
    // PlayerState-style writes need a snapshot, just like
    // PickupSystem). One write per affected player; consume + credit
    // both fold into a single Inventory blob.
    const auto& w = ctx.world();
    struct InvWrite {
        threadmaxx::EntityHandle player;
        Inventory                inv;
    };
    std::vector<InvWrite> invWrites;
    auto findInvWrite = [&](threadmaxx::EntityHandle p) -> InvWrite& {
        for (auto& w2 : invWrites) if (w2.player == p) return w2;
        invWrites.push_back({p, {}});
        if (const auto* live = threadmaxx::user::tryGet<Inventory>(
                w, ids_->inventory, p)) {
            invWrites.back().inv = *live;
        }
        return invWrites.back();
    };
    // Place: decrement first matching slot for that kind.
    for (const auto& pp : pendingPlaces_) {
        if (!pp.placer.valid() || !w.alive(pp.placer)) continue;
        auto& iw = findInvWrite(pp.placer);
        for (auto& s : iw.inv.slots) {
            if (s.count > 0u && s.kind == pp.kind) {
                --s.count;
                break;
            }
        }
    }
    // Credit: add 1 of `kind` to first matching slot, else first empty.
    for (const auto& [player, kind] : pendingInvCredits_) {
        if (!player.valid() || !w.alive(player)) continue;
        auto& iw = findInvWrite(player);
        bool credited = false;
        for (auto& s : iw.inv.slots) {
            if (s.count > 0u && s.kind == kind) {
                ++s.count;
                credited = true;
                break;
            }
        }
        if (!credited) {
            for (auto& s : iw.inv.slots) {
                if (s.count == 0u) {
                    s.kind  = kind;
                    s.count = 1u;
                    credited = true;
                    break;
                }
            }
        }
        // Silently drop on a full inventory — v1 design.
    }

    // Update the column index for both break + place so subsequent
    // ticks see the new stack heights.
    for (std::size_t i = 0; i < pendingPlaces_.size(); ++i) {
        const auto& pp = pendingPlaces_[i];
        columns_[cellIndex(pp.cellX, pp.cellZ, cells)].push_back(placeHandles[i]);
    }

    auto* ids = ids_;

    ctx.single([breaks = pendingBreaks_,
                places = pendingPlaces_,
                expired = pendingExpiredDrops_,
                dropHandles = std::move(dropHandles),
                placeHandles = std::move(placeHandles),
                invWrites = std::move(invWrites),
                ids, simTime]
               (threadmaxx::Range, threadmaxx::CommandBuffer& cb) {
        // ---- Apply breaks: destroy block + spawn DroppedItem. -----
        for (std::size_t i = 0; i < breaks.size(); ++i) {
            const auto& b = breaks[i];
            cb.destroy(b.blockEntity);

            float color[4];
            blockKindColor(b.kind, color);

            threadmaxx::Bundle bd{};
            bd.transform.position = {b.posX, b.posY + 0.15f, b.posZ};
            // Small cube — visually distinct from the static
            // terrain blocks.
            bd.transform.scale    = {0.35f, 0.35f, 0.35f};
            bd.faction.id         = kFactionNeutral;
            // Tight bounding for pickup overlap. Pickup proximity
            // is XZ-only (see PickupSystem) so Y doesn't matter for
            // the actual pickup, but keeping a real AABB lets future
            // physics use it.
            bd.boundingVolume     = threadmaxx::BoundingVolume{
                {b.posX - 0.2f, b.posY,       b.posZ - 0.2f},
                {b.posX + 0.2f, b.posY + 0.5f, b.posZ + 0.2f}};
            bd.initialMask = threadmaxx::ComponentSet{
                threadmaxx::Component::Transform,
                threadmaxx::Component::Faction,
                threadmaxx::Component::BoundingVolume,
            };
            cb.spawnBundle(dropHandles[i], bd);

            CubeRender cr{};
            cr.color[0] = color[0]; cr.color[1] = color[1];
            cr.color[2] = color[2]; cr.color[3] = color[3];
            cr.scale = 1.0f;
            threadmaxx::addUserComponent(cb, ids->cubeRender, dropHandles[i], cr);

            Pickup pk{};
            pk.value = 0u;  // dropped block ≠ score
            threadmaxx::addUserComponent(cb, ids->pickup, dropHandles[i], pk);

            DroppedItem di{};
            di.kind = b.kind;
            di.spawnTimeSeconds = simTime;
            threadmaxx::addUserComponent(cb, ids->droppedItem, dropHandles[i], di);
        }

        // ---- Apply places: spawn block. ---------------------------
        for (std::size_t i = 0; i < places.size(); ++i) {
            const auto& p = places[i];

            float color[4];
            blockKindColor(p.kind, color);

            threadmaxx::Bundle bd{};
            bd.transform.position = {p.posX, p.posY, p.posZ};
            bd.transform.scale    = {kBlockUnitMeters,
                                     kBlockUnitMeters,
                                     kBlockUnitMeters};
            bd.faction.id         = kFactionNeutral;
            bd.initialMask = threadmaxx::ComponentSet{
                threadmaxx::Component::Transform,
                threadmaxx::Component::Faction,
                threadmaxx::Component::StaticTag,
            };
            cb.spawnBundle(placeHandles[i], bd);

            CubeRender cr{};
            cr.color[0] = color[0]; cr.color[1] = color[1];
            cr.color[2] = color[2]; cr.color[3] = color[3];
            cr.scale = 1.0f;
            threadmaxx::addUserComponent(cb, ids->cubeRender, placeHandles[i], cr);
            threadmaxx::addUserComponent(cb, ids->terrainPatch, placeHandles[i],
                TerrainPatch{p.cellX, p.cellZ});
            threadmaxx::addUserComponent(cb, ids->terrainChunk, placeHandles[i],
                TerrainChunk{p.cellX / kTerrainChunkSize,
                             p.cellZ / kTerrainChunkSize});
            threadmaxx::addUserComponent(cb, ids->blockData, placeHandles[i],
                BlockData{p.kind, blockKindHardness(p.kind),
                          p.kind == BlockKind::Water ? 0u : 1u});
        }

        // ---- Age-out: destroy expired DroppedItem entities. -------
        for (auto h : expired) cb.destroy(h);

        // ---- Inventory chunk migrations (one per affected player). -
        for (const auto& iw : invWrites) {
            threadmaxx::removeUserComponent(cb, ids->inventory, iw.player);
            threadmaxx::addUserComponent(cb, ids->inventory, iw.player, iw.inv);
        }
    });
}

}  // namespace rpg
