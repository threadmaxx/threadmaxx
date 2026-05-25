#include "StaticTerrainRenderSystem.hpp"

#include <threadmaxx/Components.hpp>
#include <threadmaxx/Engine.hpp>
#include <threadmaxx/EventChannel.hpp>
#include <threadmaxx/RenderFrame.hpp>
#include <threadmaxx/UserComponent.hpp>
#include <threadmaxx/World.hpp>
#include <threadmaxx/internal/Archetype.hpp>
#include <threadmaxx/render/RenderFrameBuilder.hpp>
#include <threadmaxx/render/Visibility.hpp>

#if RPG_DEMO_HAS_SIMD
#  include <threadmaxx_simd/aabb_ops.hpp>
#endif

#include <algorithm>
#include <array>
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
            const threadmaxx::Vec3 center = di.transform.position;
            const float radius = std::sqrt(hx * hx + hy * hy + hz * hz);

            b.items.push_back(di);
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
    // D14: update is cache maintenance only — first-tick build plus
    // per-bucket rebuild on BlockBroken/BlockPlaced. All per-tick
    // culling moved to buildRenderFrame() so survivors are walked once.
    if (!cacheBuilt_) {
        buildCache(ctx);
        return;
    }
    drainInvalidations();
    for (std::size_t i = 0; i < bucketDirty_.size(); ++i) {
        if (!bucketDirty_[i]) continue;
        rebuildBucket(ctx, i);
        bucketDirty_[i] = false;
    }
}

void StaticTerrainRenderSystem::buildRenderFrame(threadmaxx::RenderFrameBuilder& b) {
    if (!cacheBuilt_ || !worldState_ || !engine_) return;

    const auto& w = engine_->world();
    const auto playerH = worldState_->player;
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

    const auto cams = std::span<const threadmaxx::Camera>(worldState_->activeCameras);
    if (cams.empty()) return;

    // Precompute frustums once per BRF; cheap (handful of cameras).
    std::array<threadmaxx::Frustum, threadmaxx::kMaxCameras> frustums{};
    const std::size_t camCount = std::min(cams.size(), frustums.size());
    for (std::size_t ci = 0; ci < camCount; ++ci) {
        frustums[ci] = threadmaxx::extractFrustum(cams[ci]);
    }

    for (const auto& bucket : buckets_) {
        if (bucket.items.empty()) continue;

        // Tier 1: bucket-distance cull (XZ).
        const float dx = bucket.circleCenterX - pX;
        const float dz = bucket.circleCenterZ - pZ;
        const float distSq = dx * dx + dz * dz;
        const float farCutoff = drawRadius + bucket.circleRadius;
        if (distSq > farCutoff * farCutoff) continue;

        const std::size_t n = bucket.items.size();
        const float nearCutoff = drawRadius - bucket.circleRadius;
        const bool fullyInside = (nearCutoff > 0.0f &&
                                  distSq < nearCutoff * nearCutoff);

        // SIMD sphere/frustum cull, fold per-camera into a single
        // mask buffer. Scratch grows monotonically with the largest
        // bucket the demo sees.
        if (perBlockMask_.size() < n) perBlockMask_.resize(n);
        if (sphereVisible_.size() < n) sphereVisible_.resize(n);
        std::fill_n(perBlockMask_.begin(), n, static_cast<std::uint32_t>(0));

        for (std::size_t ci = 0; ci < camCount; ++ci) {
            std::fill_n(sphereVisible_.begin(), n,
                        static_cast<std::uint8_t>(0));
#if RPG_DEMO_HAS_SIMD
            threadmaxx::simd::frustum_cull(
                std::span<const threadmaxx::Vec3>(bucket.centers),
                std::span<const float>(bucket.radii),
                frustums[ci],
                std::span<std::uint8_t>(sphereVisible_.data(), n));
#else
            for (std::size_t i = 0; i < n; ++i) {
                const auto& c = bucket.centers[i];
                const float r = bucket.radii[i];
                bool inside = true;
                for (const auto& p : frustums[ci].planes) {
                    const float d = p[0] * c.x + p[1] * c.y +
                                    p[2] * c.z + p[3];
                    if (d < -r) { inside = false; break; }
                }
                sphereVisible_[i] = inside ? 1u : 0u;
            }
#endif
            const std::uint32_t bit = 1u << ci;
            for (std::size_t i = 0; i < n; ++i) {
                if (sphereVisible_[i]) perBlockMask_[i] |= bit;
            }
        }

        // Emit. Fully-inside buckets skip the per-block distance test;
        // straddling buckets re-check XZ distance per block.
        if (fullyInside) {
            for (std::size_t i = 0; i < n; ++i) {
                const std::uint32_t mask = perBlockMask_[i];
                if (mask == 0) continue;
                threadmaxx::DrawItem di = bucket.items[i];
                di.cameraMask = mask;
                b.addDrawItem(threadmaxx::RenderPass::Opaque, di);
            }
        } else {
            for (std::size_t i = 0; i < n; ++i) {
                const std::uint32_t mask = perBlockMask_[i];
                if (mask == 0) continue;
                const auto& c = bucket.centers[i];
                const float bdx = c.x - pX;
                const float bdz = c.z - pZ;
                if (bdx * bdx + bdz * bdz > drawRadiusSq) continue;
                threadmaxx::DrawItem di = bucket.items[i];
                di.cameraMask = mask;
                b.addDrawItem(threadmaxx::RenderPass::Opaque, di);
            }
        }
    }
}

} // namespace rpg
