#include "StaticTerrainRenderSystem.hpp"

#include <threadmaxx/Components.hpp>
#include <threadmaxx/Engine.hpp>
#include <threadmaxx/EventChannel.hpp>
#include <threadmaxx/UserComponent.hpp>
#include <threadmaxx/World.hpp>
#include <threadmaxx/internal/Archetype.hpp>
#include <threadmaxx/render/RenderFrameBuilder.hpp>
#include <threadmaxx/render/Visibility.hpp>

#if RPG_DEMO_HAS_SIMD
#  include <threadmaxx_simd/aabb_ops.hpp>
#  include <threadmaxx_simd/vec3_ops.hpp>
#endif

#include <algorithm>
#include <cmath>
#include <cstring>

namespace rpg {

namespace {

// One bucket spans kTerrainChunkSize cells of the heightmap; each cell
// is `kTerrainExtent / cells` metres wide. We use the demo's stable
// constants here rather than the live WorldState since the bucket
// layout is set once at boot and we only need a quick XZ circle bound.
inline float cellSizeMeters(std::uint32_t cells) noexcept {
    return kTerrainExtent / static_cast<float>(cells);
}

} // namespace

void StaticTerrainRenderSystem::buildCache(threadmaxx::SystemContext& ctx) {
    const std::uint32_t cells = worldState_ ? worldState_->terrainCellsPerSide : 0u;
    if (cells == 0u) return;
    chunksPerSide_ = (cells + kTerrainChunkSize - 1u) / kTerrainChunkSize;
    buckets_.assign(static_cast<std::size_t>(chunksPerSide_) * chunksPerSide_,
                    Bucket{});
    bucketDirty_.assign(buckets_.size(), false);

    const auto& w           = ctx.world();
    const auto  cubeBit     = ids_->cubeRender.componentBit();
    const auto  chunkUcBit  = ids_->terrainChunk.componentBit();
    const auto  cubeId      = ids_->cubeRender;
    const auto  chunkUcId   = ids_->terrainChunk;
    const auto  chunkCount  = w.archetypeChunkCount();

    for (std::size_t c = 0; c < chunkCount; ++c) {
        const auto& chunk = w.archetypeChunk(c);
        if (!chunk.mask.has(threadmaxx::Component::StaticTag)) continue;
        if (chunk.mask.has(threadmaxx::Component::DisabledTag)) continue;
        if (!chunk.mask.has(cubeBit)) continue;
        if (!chunk.mask.has(chunkUcBit)) continue;
        const auto crSpan  = threadmaxx::user::chunkSpan<CubeRender>(chunk, cubeId);
        const auto tcSpan  = threadmaxx::user::chunkSpan<TerrainChunk>(chunk, chunkUcId);
        if (crSpan.empty() || tcSpan.empty()) continue;
        const auto& ents = chunk.entities;
        const auto& xfs  = chunk.transforms;
        for (std::size_t r = 0; r < ents.size(); ++r) {
            const auto& cr = crSpan[r];
            const auto& tc = tcSpan[r];
            if (tc.chunkX >= chunksPerSide_ || tc.chunkZ >= chunksPerSide_) continue;
            auto& b = buckets_[bucketIndexFor(tc.chunkX, tc.chunkZ, chunksPerSide_)];

            threadmaxx::DrawItem di{};
            di.entity    = ents[r];
            di.transform = xfs[r];
            di.transform.scale = {
                xfs[r].scale.x * cr.scale,
                xfs[r].scale.y * cr.scale,
                xfs[r].scale.z * cr.scale,
            };
            di.meshId     = cr.meshId;
            di.materialId = 0;
            di.materialOverride.params = {
                cr.color[0], cr.color[1], cr.color[2], cr.color[3],
            };
            di.cameraMask = ~0u;

            const float hx = 0.5f * std::abs(di.transform.scale.x);
            const float hy = 0.5f * std::abs(di.transform.scale.y);
            const float hz = 0.5f * std::abs(di.transform.scale.z);
            threadmaxx::BoundingVolume bv{
                {di.transform.position.x - hx,
                 di.transform.position.y - hy,
                 di.transform.position.z - hz},
                {di.transform.position.x + hx,
                 di.transform.position.y + hy,
                 di.transform.position.z + hz}};
            const threadmaxx::Vec3 center = di.transform.position;
            const float radius = std::sqrt(hx * hx + hy * hy + hz * hz);

            b.items.push_back(di);
            b.bounds.push_back(bv);
            b.centers.push_back(center);
            b.radii.push_back(radius);
        }
    }

    // Precompute the XZ bounding circle for each bucket. Center is
    // the chunk's geometric centre (cell-grid-derived) and radius is
    // the distance from that centre to the farthest block plus a
    // single-block half-extent slack so we don't pop blocks at the
    // chunk edge.
    const float cellM        = cellSizeMeters(cells);
    const float halfExtentM  = kTerrainExtent * 0.5f;
    const float bucketSizeM  = cellM * static_cast<float>(kTerrainChunkSize);
    for (std::uint32_t cz = 0; cz < chunksPerSide_; ++cz) {
        for (std::uint32_t cx = 0; cx < chunksPerSide_; ++cx) {
            auto& b = buckets_[bucketIndexFor(cx, cz, chunksPerSide_)];
            // Chunk geometric centre in world space.
            b.circleCenterX = -halfExtentM +
                              (static_cast<float>(cx) + 0.5f) * bucketSizeM;
            b.circleCenterZ = -halfExtentM +
                              (static_cast<float>(cz) + 0.5f) * bucketSizeM;
            float maxR2 = 0.0f;
            for (const auto& c : b.centers) {
                const float dx = c.x - b.circleCenterX;
                const float dz = c.z - b.circleCenterZ;
                const float r2 = dx * dx + dz * dz;
                if (r2 > maxR2) maxR2 = r2;
            }
            // Slack: half a block in XZ so distance-cull doesn't trim
            // visible blocks at the very edge of the bucket.
            b.circleRadius = std::sqrt(maxR2) + 0.5f * cellM;
        }
    }
    cacheBuilt_ = true;
}

void StaticTerrainRenderSystem::rebuildBucket(threadmaxx::SystemContext& ctx,
                                              std::size_t bucketIdx) {
    if (bucketIdx >= buckets_.size()) return;
    auto& b = buckets_[bucketIdx];
    // Save the bucket's circle bound — its XZ footprint never changes
    // (the chunk's grid position is fixed), just the block contents.
    const float savedCx = b.circleCenterX;
    const float savedCz = b.circleCenterZ;
    b.items.clear();
    b.bounds.clear();
    b.centers.clear();
    b.radii.clear();

    const std::uint32_t targetCx = static_cast<std::uint32_t>(bucketIdx % chunksPerSide_);
    const std::uint32_t targetCz = static_cast<std::uint32_t>(bucketIdx / chunksPerSide_);

    const auto& w           = ctx.world();
    const auto  cubeBit     = ids_->cubeRender.componentBit();
    const auto  chunkUcBit  = ids_->terrainChunk.componentBit();
    const auto  cubeId      = ids_->cubeRender;
    const auto  chunkUcId   = ids_->terrainChunk;
    const auto  chunkCount  = w.archetypeChunkCount();
    for (std::size_t c = 0; c < chunkCount; ++c) {
        const auto& chunk = w.archetypeChunk(c);
        if (!chunk.mask.has(threadmaxx::Component::StaticTag)) continue;
        if (chunk.mask.has(threadmaxx::Component::DisabledTag)) continue;
        if (!chunk.mask.has(cubeBit)) continue;
        if (!chunk.mask.has(chunkUcBit)) continue;
        const auto crSpan = threadmaxx::user::chunkSpan<CubeRender>(chunk, cubeId);
        const auto tcSpan = threadmaxx::user::chunkSpan<TerrainChunk>(chunk, chunkUcId);
        if (crSpan.empty() || tcSpan.empty()) continue;
        const auto& ents = chunk.entities;
        const auto& xfs  = chunk.transforms;
        for (std::size_t r = 0; r < ents.size(); ++r) {
            const auto& tc = tcSpan[r];
            if (tc.chunkX != targetCx || tc.chunkZ != targetCz) continue;
            const auto& cr = crSpan[r];
            threadmaxx::DrawItem di{};
            di.entity    = ents[r];
            di.transform = xfs[r];
            di.transform.scale = {
                xfs[r].scale.x * cr.scale,
                xfs[r].scale.y * cr.scale,
                xfs[r].scale.z * cr.scale,
            };
            di.meshId     = cr.meshId;
            di.materialId = 0;
            di.materialOverride.params = {
                cr.color[0], cr.color[1], cr.color[2], cr.color[3],
            };
            di.cameraMask = ~0u;
            const float hx = 0.5f * std::abs(di.transform.scale.x);
            const float hy = 0.5f * std::abs(di.transform.scale.y);
            const float hz = 0.5f * std::abs(di.transform.scale.z);
            b.items.push_back(di);
            b.bounds.push_back(threadmaxx::BoundingVolume{
                {di.transform.position.x - hx,
                 di.transform.position.y - hy,
                 di.transform.position.z - hz},
                {di.transform.position.x + hx,
                 di.transform.position.y + hy,
                 di.transform.position.z + hz}});
            b.centers.push_back(di.transform.position);
            b.radii.push_back(std::sqrt(hx * hx + hy * hy + hz * hz));
        }
    }

    // Recompute the bucket circle radius (block list may have grown
    // OR shrunk). Center stays put.
    b.circleCenterX = savedCx;
    b.circleCenterZ = savedCz;
    float maxR2 = 0.0f;
    for (const auto& c : b.centers) {
        const float dx = c.x - savedCx;
        const float dz = c.z - savedCz;
        const float r2 = dx * dx + dz * dz;
        if (r2 > maxR2) maxR2 = r2;
    }
    const std::uint32_t cells = worldState_ ? worldState_->terrainCellsPerSide : 0u;
    const float slack = (cells > 0u) ? 0.5f * cellSizeMeters(cells) : 0.5f;
    b.circleRadius = std::sqrt(maxR2) + slack;
}

void StaticTerrainRenderSystem::drainInvalidations() {
    // Both events carry cellX/cellZ — convert to bucket index and
    // mark the bucket dirty. Drained idempotently here so we don't
    // race BlockEditSystem's drain (drainTick returns the front-buffer
    // span without popping).
    {
        auto evs = engine_->events<BlockBroken>().drainTick();
        for (const auto& ev : evs) {
            const std::uint32_t cx = ev.cellX / kTerrainChunkSize;
            const std::uint32_t cz = ev.cellZ / kTerrainChunkSize;
            if (cx >= chunksPerSide_ || cz >= chunksPerSide_) continue;
            bucketDirty_[bucketIndexFor(cx, cz, chunksPerSide_)] = true;
        }
    }
    {
        auto evs = engine_->events<BlockPlaced>().drainTick();
        for (const auto& ev : evs) {
            const std::uint32_t cx = ev.cellX / kTerrainChunkSize;
            const std::uint32_t cz = ev.cellZ / kTerrainChunkSize;
            if (cx >= chunksPerSide_ || cz >= chunksPerSide_) continue;
            bucketDirty_[bucketIndexFor(cx, cz, chunksPerSide_)] = true;
        }
    }
}

void StaticTerrainRenderSystem::update(threadmaxx::SystemContext& ctx) {
    if (!cacheBuilt_) {
        buildCache(ctx);
        if (!cacheBuilt_) return;
    } else {
        drainInvalidations();
        for (std::size_t i = 0; i < bucketDirty_.size(); ++i) {
            if (!bucketDirty_[i]) continue;
            rebuildBucket(ctx, i);
            bucketDirty_[i] = false;
        }
    }

    // Per-tick bucket distance cull. Mirrors CubeRenderSystem's draw
    // radius (36 m normal / 12 m stress). The chunk's XZ bounding
    // circle includes a half-cell slack; we add the bucket radius to
    // the draw radius for the conservative "any block in this chunk
    // might be visible" test.
    if (!worldState_) return;
    const auto playerH = worldState_->player;
    const auto& w = ctx.world();
    float pX = 0.0f, pZ = 0.0f;
    if (playerH.valid() && w.alive(playerH)) {
        const auto& pt = w.get<threadmaxx::Transform>(playerH);
        pX = pt.position.x;
        pZ = pt.position.z;
    }
    constexpr float kStressDrawRadius = 12.0f;
    constexpr float kNormalDrawRadius = 36.0f;
    const float drawRadius = worldState_->stressMode ? kStressDrawRadius
                                                     : kNormalDrawRadius;
    const float drawRadiusSq = drawRadius * drawRadius;

    items_.clear();
    bounds_.clear();
    centers_.clear();
    radii_.clear();
    for (const auto& b : buckets_) {
        if (b.items.empty()) continue;
        const float dx = b.circleCenterX - pX;
        const float dz = b.circleCenterZ - pZ;
        const float distSq = dx * dx + dz * dz;
        // Fully outside: chunk centre + its radius can't reach the
        // draw circle. Cheap skip — discards majority of buckets.
        const float farCutoff = drawRadius + b.circleRadius;
        if (distSq > farCutoff * farCutoff) continue;
        // Fully inside: every block in the chunk is within draw
        // radius. Bulk-append the precomputed slice.
        const float nearCutoff = drawRadius - b.circleRadius;
        if (nearCutoff > 0.0f && distSq < nearCutoff * nearCutoff) {
            items_.insert  (items_.end(),   b.items.begin(),   b.items.end());
            bounds_.insert (bounds_.end(),  b.bounds.begin(),  b.bounds.end());
            centers_.insert(centers_.end(), b.centers.begin(), b.centers.end());
            radii_.insert  (radii_.end(),   b.radii.begin(),   b.radii.end());
            continue;
        }
        // Straddles the boundary — per-block distance cull inside
        // the bucket. Cheap (6 ops/block) and prevents the bulk
        // memcpy of mostly-invisible blocks at the bucket fringe.
        const std::size_t n = b.items.size();
        for (std::size_t i = 0; i < n; ++i) {
            const auto& center = b.centers[i];
            const float bdx = center.x - pX;
            const float bdz = center.z - pZ;
            if (bdx * bdx + bdz * bdz > drawRadiusSq) continue;
            items_  .push_back(b.items  [i]);
            bounds_ .push_back(b.bounds [i]);
            centers_.push_back(b.centers[i]);
            radii_  .push_back(b.radii  [i]);
        }
    }
}

void StaticTerrainRenderSystem::buildRenderFrame(threadmaxx::RenderFrameBuilder& b) {
    if (items_.empty()) return;
    const std::span<const threadmaxx::Camera> cams =
        worldState_ ? std::span<const threadmaxx::Camera>(worldState_->activeCameras)
                    : std::span<const threadmaxx::Camera>{};

#if RPG_DEMO_HAS_SIMD
    if (!cams.empty()) {
        const std::size_t n = items_.size();
        if (sphereVisible_.size() < n * cams.size()) {
            sphereVisible_.assign(n * cams.size(), 0);
        } else {
            std::fill_n(sphereVisible_.begin(), n * cams.size(),
                        static_cast<std::uint8_t>(0));
        }
        for (std::size_t ci = 0; ci < cams.size(); ++ci) {
            threadmaxx::Frustum f = threadmaxx::extractFrustum(cams[ci]);
            threadmaxx::simd::frustum_cull(
                std::span<const threadmaxx::Vec3>(centers_),
                std::span<const float>(radii_),
                f,
                std::span<std::uint8_t>(
                    sphereVisible_.data() + ci * n, n));
        }
        for (std::size_t i = 0; i < n; ++i) {
            std::uint32_t mask = 0;
            for (std::size_t ci = 0; ci < cams.size(); ++ci) {
                if (sphereVisible_[ci * n + i]) mask |= (1u << ci);
            }
            items_[i].cameraMask = mask;
        }
    }
#else
    if (!cams.empty()) {
        threadmaxx::cullByFrustum(
            std::span<threadmaxx::DrawItem>(items_),
            std::span<const threadmaxx::BoundingVolume>(bounds_),
            cams);
    }
#endif

    for (const auto& di : items_) {
        if (di.cameraMask == 0) continue;
        b.addDrawItem(threadmaxx::RenderPass::Opaque, di);
    }
}

} // namespace rpg
