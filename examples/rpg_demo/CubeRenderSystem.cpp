#include "CubeRenderSystem.hpp"

#include "ParallelDispatch.hpp"

#include <threadmaxx/Components.hpp>
#include <threadmaxx/UserComponent.hpp>
#include <threadmaxx/World.hpp>
#include <threadmaxx/render/RenderFrameBuilder.hpp>
#include <threadmaxx/render/Visibility.hpp>

#if RPG_DEMO_HAS_SIMD
#  include <threadmaxx_simd/aabb_ops.hpp>
#  include <threadmaxx_simd/vec3_ops.hpp>
#endif

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <vector>

namespace rpg {

namespace {

struct CubeSlice {
    const threadmaxx::internal::ArchetypeChunk* chunk;
    std::span<const CubeRender>                 cubeSpan;
    std::span<const AnimState>                  animSpan;   // empty if no AnimState
    bool                                        hasAnim;
    bool                                        hasVel;
    std::uint32_t                               beginFlat;  // first flat index for this slice
    std::uint32_t                               endFlat;    // one-past-last flat index
};

} // namespace

void CubeRenderSystem::update(threadmaxx::SystemContext& ctx) {
    // 2026-05-20 (rev 4) — flat-row parallelFor with cross-chunk slices.
    //
    // Rev 3 used per-chunk-outer / row-parallel-inner, but tiny chunks
    // (player, sword, terrain — each a 1-row archetype) produced
    // `parallelFor(1, grain=0)` which submits a single sub-job. With
    // multi-worker pools, the sub-job is round-robin'd to one specific
    // worker; if that worker is the same one running CubeRender, no
    // other worker gets notified and the wave latch deadlocks.
    //
    // This rev does ONE big `parallelFor(totalRows, grain=0)` over the
    // flat row count across every matching chunk, with a slice
    // lookup table mapping flat index → (chunk, local row). Always
    // produces ~4*workerCount sub-jobs, plenty to fill the pool.
    //
    // A global atomic `outCursor` hands out output slots so sub-jobs
    // can emit in any order (preserves no per-chunk order, but the
    // BRF cull + render don't care about order).

    const auto& w = ctx.world();
    const auto cubeBit  = ids_->cubeRender.componentBit();
    const auto animBit  = ids_->animState.componentBit();
    const auto animId   = ids_->animState;
    const auto chunkCount = w.archetypeChunkCount();

    const bool useDistanceCull =
        worldState_ != nullptr && worldState_->stressMode;
    // 2026-05-22 audit refactor — in first-person mode the player's
    // own body shouldn't render (you'd see the inside of a cube
    // filling the screen). Read PlayerState from the world once
    // per tick and capture the player handle + flag into the
    // worker lambda.
    threadmaxx::EntityHandle playerH{};
    bool                     hidePlayerBody = false;
    if (worldState_) {
        playerH = worldState_->player;
        if (playerH.valid() && w.alive(playerH)) {
            const PlayerState* ps = threadmaxx::user::tryGet<PlayerState>(
                w, ids_->playerState, playerH);
            if (ps) hidePlayerBody = ps->firstPerson != 0u;
        }
    }
    // 2026-05-20 (rev 2) — radius tightened from 18m → 12m. The
    // 100k-NPC stress workload puts ~28 entities/m² in a 60×60 m
    // world; an 18m radius left ~10k visible instances which the
    // Vulkan renderer's single-threaded `submitFrame` rendered at
    // ~134 ms / tick (single biggest item in the per-tick budget).
    // 12m radius cuts visible count to ~4-5k → render drops to
    // ~50-60 ms / tick. The player's third-person camera FOV at
    // distance 12m still covers a screen-worth of game space, so
    // the visual effect is identical.
    constexpr float kStressDrawRadius   = 12.0f;
    constexpr float kStressDrawRadiusSq = kStressDrawRadius * kStressDrawRadius;
    float pX = 0.0f, pZ = 0.0f;
    if (useDistanceCull) {
        const auto p = worldState_->player;
        if (p.valid() && w.alive(p)) {
            const auto& pt = w.get<threadmaxx::Transform>(p);
            pX = pt.position.x;
            pZ = pt.position.z;
        }
    }

    const float simTime = static_cast<float>(ctx.tick()) *
                          static_cast<float>(ctx.dt());

    std::vector<CubeSlice> slices;
    slices.reserve(8);
    std::uint32_t total = 0;
    const auto cubeId = ids_->cubeRender;
    for (std::size_t c = 0; c < chunkCount; ++c) {
        const auto& chunk = w.archetypeChunk(c);
        if (!chunk.mask.has(cubeBit)) continue;
        if (chunk.mask.has(threadmaxx::Component::DisabledTag)) continue;
        const auto rows = chunk.entities.size();
        if (rows == 0) continue;
        CubeSlice s;
        s.chunk    = &chunk;
        s.cubeSpan = threadmaxx::user::chunkSpan<CubeRender>(chunk, cubeId);
        if (s.cubeSpan.empty()) continue;
        s.hasAnim = chunk.mask.has(animBit);
        s.hasVel  = chunk.mask.has(threadmaxx::Component::Velocity);
        if (s.hasAnim) {
            s.animSpan = threadmaxx::user::chunkSpan<AnimState>(chunk, animId);
        }
        s.beginFlat = total;
        total += static_cast<std::uint32_t>(rows);
        s.endFlat = total;
        slices.push_back(s);
    }
    if (total == 0) return;

    items_.resize(total);
    bounds_.resize(total);
    centers_.resize(total);
    radii_.resize(total);
    auto* itemsOut   = items_.data();
    auto* boundsOut  = bounds_.data();
    auto* centersOut = centers_.data();
    auto* radiiOut   = radii_.data();

    std::atomic<std::uint32_t> outCursor{0};
    const auto* slicesPtr = slices.data();
    const std::uint32_t sliceCount =
        static_cast<std::uint32_t>(slices.size());

    dispatchOrInline(ctx, total,
        [slicesPtr, sliceCount,
         itemsOut, boundsOut, centersOut, radiiOut,
         &outCursor, useDistanceCull, pX, pZ, simTime,
         playerH, hidePlayerBody]
        (threadmaxx::Range r, threadmaxx::CommandBuffer&) {
            // Find starting slice for r.begin. Linear scan — slice
            // count is tiny (≤ ~8 typically); cheaper than binary
            // search for that size. We carry the cursor across the
            // sub-job so slice transitions are O(slices) total.
            std::uint32_t si = 0;
            while (si + 1 < sliceCount && r.begin >= slicesPtr[si].endFlat) {
                ++si;
            }

            for (std::uint32_t flat = r.begin; flat < r.end; ++flat) {
                while (si + 1 < sliceCount && flat >= slicesPtr[si].endFlat) {
                    ++si;
                }
                const auto& slice = slicesPtr[si];
                const std::uint32_t row = flat - slice.beginFlat;
                const auto& cr = slice.cubeSpan[row];
                const auto& tr = slice.chunk->transforms[row];

                // 2026-05-22 audit refactor — first-person body-hide.
                if (hidePlayerBody && playerH.valid() &&
                    slice.chunk->entities[row] == playerH) {
                    continue;
                }

                if (useDistanceCull) {
                    const float dx = tr.position.x - pX;
                    const float dz = tr.position.z - pZ;
                    const float maxScale =
                        std::max({std::abs(tr.scale.x),
                                  std::abs(tr.scale.z)});
                    if (maxScale < 8.0f &&
                        dx * dx + dz * dz > kStressDrawRadiusSq) {
                        continue;
                    }
                }

                threadmaxx::DrawItem di = {};
                di.entity    = slice.chunk->entities[row];
                di.transform = tr;
                di.transform.scale = {
                    tr.scale.x * cr.scale,
                    tr.scale.y * cr.scale,
                    tr.scale.z * cr.scale,
                };
                // 2026-05-22 audit fix — draw-time bob is now an
                // ADDITIVE adjustment on top of the live transform,
                // and it only runs in stress mode (where
                // AnimationSystem is a no-op). The pre-fix code
                // OVERWROTE `di.transform.position.y` with the
                // spawn-time `a.baseY`, which froze every animated
                // cube at its spawn Y — including the player. As the
                // terrain-attached `tr.position.y` walked across the
                // bumpy heightmap, the rendered cube stayed pinned
                // at spawn Y, producing the "player floats up while
                // the ground drops away" appearance the user
                // reported. In non-stress mode AnimationSystem has
                // already written `tr.position.y = heightAt + bob`,
                // so the rendered cube faithfully tracks the ground.
                // `useDistanceCull` is already gated on stress mode
                // (see line ~64) so we reuse it as the stress flag.
                if (slice.hasAnim && useDistanceCull) {
                    const auto& a = slice.animSpan[row];
                    float speed = 0.0f;
                    if (slice.hasVel) {
                        const auto& v = slice.chunk->velocities[row].linear;
                        speed = std::sqrt(v.x * v.x + v.z * v.z);
                    }
                    constexpr float kRefSpeed = 4.0f;
                    const float ratio = std::min(speed / kRefSpeed, 1.0f);
                    if (ratio > 0.0f) {
                        const float bob = std::sin(simTime * a.frequency +
                                                   a.phase) *
                                          a.amplitude * ratio;
                        di.transform.position.y = tr.position.y + bob;
                    }
                }
                di.meshId     = cr.meshId;
                di.materialId = 0;
                di.materialOverride.params = {
                    cr.color[0], cr.color[1], cr.color[2], cr.color[3],
                };
                di.cameraMask = ~0u;

                const std::uint32_t slot =
                    outCursor.fetch_add(1, std::memory_order_relaxed);
                itemsOut[slot] = di;

                const float hx = 0.5f * std::abs(di.transform.scale.x);
                const float hy = 0.5f * std::abs(di.transform.scale.y);
                const float hz = 0.5f * std::abs(di.transform.scale.z);
                threadmaxx::BoundingVolume bv;
                bv.min = {di.transform.position.x - hx,
                          di.transform.position.y - hy,
                          di.transform.position.z - hz};
                bv.max = {di.transform.position.x + hx,
                          di.transform.position.y + hy,
                          di.transform.position.z + hz};
                boundsOut[slot] = bv;
                centersOut[slot] = di.transform.position;
                radiiOut[slot]   = std::sqrt(hx * hx + hy * hy + hz * hz);
            }
        });

    const std::uint32_t emit = outCursor.load(std::memory_order_relaxed);
    items_.resize(emit);
    bounds_.resize(emit);
    centers_.resize(emit);
    radii_.resize(emit);
}

void CubeRenderSystem::buildRenderFrame(threadmaxx::RenderFrameBuilder& b) {
    if (items_.empty()) return;
    const std::span<const threadmaxx::Camera> cams =
        worldState_ ? std::span<const threadmaxx::Camera>(worldState_->activeCameras)
                    : std::span<const threadmaxx::Camera>{};

#if RPG_DEMO_HAS_SIMD
    // 2026-05-20 — SIMD sphere broad-phase via `simd::frustum_cull`.
    // Genuinely AVX2-vectorized (8 spheres per iteration, gather +
    // 6-plane survival reduction + movemask). For a 10k-DrawItem
    // stress workload across 2–3 cameras this drops the cull pass
    // from ~2 ms (engine's per-DrawItem AABB sweep) to ~0.2 ms —
    // the single biggest BRF win at scale.
    //
    // The sphere bounds the AABB conservatively, so a sphere-miss
    // implies an AABB-miss. For sphere-hits we KEEP the AABB refine
    // via `cullByFrustum` so the final mask is identical to the
    // legacy path bit-for-bit.
    if (!cams.empty()) {
        const std::size_t n = items_.size();
        std::vector<std::uint8_t> sphereVisible(n * cams.size(), 0);
        for (std::size_t ci = 0; ci < cams.size(); ++ci) {
            threadmaxx::Frustum f = threadmaxx::extractFrustum(cams[ci]);
            threadmaxx::simd::frustum_cull(
                std::span<const threadmaxx::Vec3>(centers_),
                std::span<const float>(radii_),
                f,
                std::span<std::uint8_t>(
                    sphereVisible.data() + ci * n, n));
        }
        for (std::size_t i = 0; i < n; ++i) {
            std::uint32_t mask = 0;
            for (std::size_t ci = 0; ci < cams.size(); ++ci) {
                if (sphereVisible[ci * n + i]) mask |= (1u << ci);
            }
            items_[i].cameraMask = mask;
        }
        std::size_t alive = 0;
        for (std::size_t i = 0; i < n; ++i) {
            if (items_[i].cameraMask != 0) {
                if (alive != i) {
                    std::swap(items_[i],  items_[alive]);
                    std::swap(bounds_[i], bounds_[alive]);
                }
                ++alive;
            }
        }
        if (alive > 0) {
            threadmaxx::cullByFrustum(
                std::span<threadmaxx::DrawItem>(items_.data(), alive),
                std::span<const threadmaxx::BoundingVolume>(bounds_.data(), alive),
                cams);
        }
    }
#else
    // ---- Non-SIMD reference path. Single pass through the per-
    //      item per-camera AABB cull.
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
