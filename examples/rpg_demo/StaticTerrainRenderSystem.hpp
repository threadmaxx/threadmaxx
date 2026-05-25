#pragma once

#include <threadmaxx/System.hpp>
#include <threadmaxx/render/DrawItem.hpp>
#include <threadmaxx/render/Visibility.hpp>

#include "DemoTypes.hpp"

#include <cstdint>
#include <vector>

namespace threadmaxx { class Engine; }

namespace rpg {

/// §3.11 batch D13 — terrain split out of CubeRenderSystem.
///
/// Owns rendering for every `StaticTag + CubeRender` entity in the
/// world (i.e. the ~92k voxel terrain blocks). At first tick scans
/// the world once, precomputes `(DrawItem, BoundingVolume, center,
/// radius)` per block, and buckets the precomputed entries by their
/// `TerrainChunk` UC (16×16-cell groups → 36 buckets at the demo's
/// 96-cell terrain).
///
/// Per tick: bucket-level XZ distance cull against the player using
/// the precomputed bucket bounding sphere. Surviving buckets memcpy
/// their slice into the per-tick scratch arrays. `buildRenderFrame`
/// runs the same SIMD sphere broad-phase against active cameras and
/// emits DrawItems.
///
/// Invalidation: subscribes to `BlockBroken` / `BlockPlaced` events;
/// the affected `TerrainChunk` bucket is marked dirty and re-scanned
/// next tick. The rest of the cache stays warm.
class StaticTerrainRenderSystem : public threadmaxx::ISystem {
public:
    StaticTerrainRenderSystem(threadmaxx::Engine* engine,
                              UserComponentIds*    ids,
                              const WorldState*    worldState)
        : engine_(engine), ids_(ids), worldState_(worldState) {}

    const char* name() const noexcept override { return "static-terrain-render"; }
    threadmaxx::ComponentSet reads() const noexcept override {
        return threadmaxx::ComponentSet{threadmaxx::Component::Transform};
    }
    threadmaxx::ComponentSet writes() const noexcept override {
        return threadmaxx::ComponentSet::none();
    }
    std::uint32_t preferredWorkerCap() const noexcept override { return 0u; }

    void update(threadmaxx::SystemContext& ctx) override;
    void buildRenderFrame(threadmaxx::RenderFrameBuilder& b) override;

private:
    /// One bucket per `TerrainChunk(chunkX, chunkZ)`. Bucket index =
    /// `chunkZ * chunksPerSide + chunkX`. Precomputed at first tick,
    /// rebuilt per-bucket on `BlockBroken` / `BlockPlaced`.
    struct Bucket {
        std::vector<threadmaxx::DrawItem>       items;
        std::vector<threadmaxx::BoundingVolume> bounds;
        std::vector<threadmaxx::Vec3>           centers;
        std::vector<float>                      radii;
        // XZ bounding circle of all blocks in this bucket — used for
        // the per-tick chunk-level distance cull. `circleRadius`
        // includes a half-block slack so we don't pop visible blocks
        // at the bucket edge.
        float circleCenterX = 0.0f;
        float circleCenterZ = 0.0f;
        float circleRadius  = 0.0f;
    };

    void buildCache(threadmaxx::SystemContext& ctx);
    void rebuildBucket(threadmaxx::SystemContext& ctx, std::size_t bucketIdx);
    void drainInvalidations();
    static std::size_t bucketIndexFor(std::uint32_t chunkX,
                                      std::uint32_t chunkZ,
                                      std::uint32_t chunksPerSide) noexcept {
        return static_cast<std::size_t>(chunkZ) * chunksPerSide + chunkX;
    }

    threadmaxx::Engine*       engine_      = nullptr;
    UserComponentIds*         ids_         = nullptr;
    const WorldState*         worldState_  = nullptr;

    bool                      cacheBuilt_   = false;
    std::uint32_t             chunksPerSide_ = 0;
    std::vector<Bucket>       buckets_;
    std::vector<bool>         bucketDirty_;

    // Per-tick scratch — survivors of the bucket-distance cull,
    // re-populated each tick by memcpy from the surviving buckets.
    std::vector<threadmaxx::DrawItem>       items_;
    std::vector<threadmaxx::BoundingVolume> bounds_;
    std::vector<threadmaxx::Vec3>           centers_;
    std::vector<float>                      radii_;
    std::vector<std::uint8_t>               sphereVisible_;
};

} // namespace rpg
