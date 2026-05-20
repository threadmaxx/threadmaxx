#include "CubeRenderSystem.hpp"

#include <threadmaxx/Components.hpp>
#include <threadmaxx/UserComponent.hpp>
#include <threadmaxx/World.hpp>
#include <threadmaxx/render/RenderFrameBuilder.hpp>
#include <threadmaxx/render/Visibility.hpp>

#include <algorithm>
#include <cstring>
#include <vector>

namespace rpg {

void CubeRenderSystem::update(threadmaxx::SystemContext& ctx) {
    // 2026-05-20 — chunk-parallel build of the full `(DrawItem,
    // AABB)` arrays. The DrawItem + AABB construction was
    // previously done serially inside `buildRenderFrame` (~3ms at
    // 60k entities); doing it in `update` lets us spread it across
    // worker jobs (`buildRenderFrame` is single-threaded by engine
    // contract). buildRenderFrame then only runs `cullByFrustum`
    // and emits the pre-built items.
    //
    // 2026-05-20 — stress-mode distance cull. The frustum cull
    // alone isn't enough on the stress workload: with a far clip
    // at 200 m the main camera sees essentially every entity in
    // the 60×60 m world, so culling reports `visible=59943` of
    // 59943 items and the renderer ends up uploading + drawing
    // 60k instances each frame (CPU-side renderer work is the
    // dominant cost of the per-tick budget). When `stressMode`
    // is on we drop any cube whose XZ distance to the player
    // exceeds `kStressDrawRadius`; the player can only see the
    // surrounding region at the demo's camera distance anyway.
    const auto& w = ctx.world();
    const auto cubeBit = ids_->cubeRender.componentBit();
    const auto chunkCount = w.archetypeChunkCount();

    const bool useDistanceCull =
        worldState_ != nullptr && worldState_->stressMode;
    // 18m radius keeps the player's immediate vicinity rendered
    // while culling the 30-m-radius pickup cloud. The world is
    // 60×60 m so a 35-m radius covered every pickup (no effective
    // culling).
    constexpr float kStressDrawRadius   = 18.0f;
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

    struct ChunkJob {
        std::uint32_t chunkIdx;
        std::uint32_t outOffset;
        std::uint32_t rowCount;
    };
    std::vector<ChunkJob> jobs;
    jobs.reserve(8);
    std::uint32_t total = 0;
    for (std::size_t c = 0; c < chunkCount; ++c) {
        const auto& chunk = w.archetypeChunk(c);
        if (!chunk.mask.has(cubeBit)) continue;
        if (chunk.mask.has(threadmaxx::Component::DisabledTag)) continue;
        if (chunk.entities.empty()) continue;
        ChunkJob job;
        job.chunkIdx  = static_cast<std::uint32_t>(c);
        job.outOffset = total;
        job.rowCount  = static_cast<std::uint32_t>(chunk.entities.size());
        jobs.push_back(job);
        total += job.rowCount;
    }
    // Pre-size per-job output ranges to the chunk's row count (upper
    // bound). After parallel build, we compact in place by walking
    // jobs in order and copying each job's actual emit count.
    items_.resize(total);
    bounds_.resize(total);
    if (jobs.empty()) return;

    std::vector<std::uint32_t> perJobEmit(jobs.size(), 0);
    const auto cubeId = ids_->cubeRender;
    auto* itemsOut  = items_.data();
    auto* boundsOut = bounds_.data();
    const auto jobCount = static_cast<std::uint32_t>(jobs.size());

    ctx.parallelFor(jobCount, /*grain*/ 1,
        [&w, &jobs, &perJobEmit, itemsOut, boundsOut, cubeId,
         useDistanceCull, pX, pZ]
        (threadmaxx::Range r, threadmaxx::CommandBuffer&) {
            for (std::uint32_t ji = r.begin; ji < r.end; ++ji) {
                const auto& job = jobs[ji];
                const auto& chunk = w.archetypeChunk(job.chunkIdx);
                auto cubeSpan =
                    threadmaxx::user::chunkSpan<CubeRender>(chunk, cubeId);
                std::uint32_t outRow = job.outOffset;
                if (cubeSpan.empty()) {
                    perJobEmit[ji] = 0;
                    continue;
                }
                for (std::uint32_t row = 0; row < job.rowCount; ++row) {
                    const auto& cr = cubeSpan[row];
                    const auto& tr = chunk.transforms[row];

                    if (useDistanceCull) {
                        const float dx = tr.position.x - pX;
                        const float dz = tr.position.z - pZ;
                        const float maxScale =
                            std::max({std::abs(tr.scale.x),
                                      std::abs(tr.scale.z)});
                        if (maxScale < 8.0f &&
                            dx * dx + dz * dz > kStressDrawRadiusSq) {
                            continue;   // skip, don't emit
                        }
                    }

                    threadmaxx::DrawItem di = {};
                    di.entity    = chunk.entities[row];
                    di.transform = tr;
                    di.transform.scale = {
                        tr.scale.x * cr.scale,
                        tr.scale.y * cr.scale,
                        tr.scale.z * cr.scale,
                    };
                    di.meshId     = cr.meshId;
                    di.materialId = 0;
                    di.materialOverride.params = {
                        cr.color[0], cr.color[1], cr.color[2], cr.color[3],
                    };
                    di.cameraMask = ~0u;
                    itemsOut[outRow] = di;

                    const float hx = 0.5f * std::abs(di.transform.scale.x);
                    const float hy = 0.5f * std::abs(di.transform.scale.y);
                    const float hz = 0.5f * std::abs(di.transform.scale.z);
                    threadmaxx::BoundingVolume bv;
                    bv.min = {tr.position.x - hx, tr.position.y - hy,
                              tr.position.z - hz};
                    bv.max = {tr.position.x + hx, tr.position.y + hy,
                              tr.position.z + hz};
                    boundsOut[outRow] = bv;
                    ++outRow;
                }
                perJobEmit[ji] = outRow - job.outOffset;
            }
        });

    // Compact in place. Each job wrote `perJobEmit[ji]` items into
    // its [outOffset, outOffset+rowCount) reserved range; entries
    // beyond that count are unused. We slide later jobs' valid
    // ranges down to fill the gaps, leaving `items_/bounds_` densely
    // packed at the front. Cheaper than locking on a global atomic
    // offset counter inside the worker loop.
    std::uint32_t compactSize = perJobEmit[0];
    for (std::size_t ji = 1; ji < jobs.size(); ++ji) {
        const std::uint32_t srcStart = jobs[ji].outOffset;
        const std::uint32_t count    = perJobEmit[ji];
        if (count == 0) continue;
        if (compactSize == srcStart) {
            compactSize += count;
            continue;
        }
        std::memmove(items_.data()  + compactSize,
                     items_.data()  + srcStart,
                     count * sizeof(threadmaxx::DrawItem));
        std::memmove(bounds_.data() + compactSize,
                     bounds_.data() + srcStart,
                     count * sizeof(threadmaxx::BoundingVolume));
        compactSize += count;
    }
    items_.resize(compactSize);
    bounds_.resize(compactSize);
}

void CubeRenderSystem::buildRenderFrame(threadmaxx::RenderFrameBuilder& b) {
    if (items_.empty()) return;
    if (worldState_ && !worldState_->activeCameras.empty()) {
        threadmaxx::cullByFrustum(
            std::span<threadmaxx::DrawItem>(items_),
            std::span<const threadmaxx::BoundingVolume>(bounds_),
            std::span<const threadmaxx::Camera>(worldState_->activeCameras));
    }
    for (const auto& di : items_) {
        if (di.cameraMask == 0) continue;
        b.addDrawItem(threadmaxx::RenderPass::Opaque, di);
    }
}

} // namespace rpg
