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
/// §3.11 batch D14 — fused update + BRF; no per-tick SoA scratch.
///
/// Owns rendering for every `StaticTag + CubeRender` entity in the
/// world (i.e. the ~92k voxel terrain blocks). At first tick scans
/// the world once, precomputes `(DrawItem, center, radius)` per
/// block, and buckets the entries by their `TerrainChunk` UC
/// (16×16-cell groups → 36 buckets at the demo's 96-cell terrain).
///
/// `update()` does cache maintenance only — first-tick build plus
/// per-bucket rebuild on `BlockBroken` / `BlockPlaced`. All per-tick
/// culling and emission happens in `buildRenderFrame()`: bucket-level
/// XZ distance cull → SIMD frustum sphere broad-phase against active
/// cameras directly on the bucket's centers/radii → per-block distance
/// re-check on straddling buckets → emit DrawItems. There is no
/// intermediate items_/bounds_/centers_/radii_ scratch — every
/// surviving block is visited exactly once per tick.
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
    /// rebuilt per-bucket on `BlockBroken` / `BlockPlaced`. D14 dropped
    /// the BoundingVolume sidecar — the SIMD frustum cull only needs
    /// centers + radii; the scalar fallback derives an AABB inline.
    struct Bucket {
        std::vector<threadmaxx::DrawItem>       items;
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

    // D14 — per-BRF scratch sized to the largest bucket. `sphereVisible_`
    // is the SIMD frustum cull output (one byte per block per camera
    // pass); `perBlockMask_` accumulates the per-block camera bitset as
    // the per-camera passes fold in. Both grow monotonically; never
    // shrunk to avoid steady-state churn.
    std::vector<std::uint8_t>  sphereVisible_;
    std::vector<std::uint32_t> perBlockMask_;
};

} // namespace rpg
