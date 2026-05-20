#include "MovementSystem.hpp"

#include "ParallelDispatch.hpp"

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Components.hpp>
#include <threadmaxx/System.hpp>
#include <threadmaxx/World.hpp>
#include <threadmaxx/internal/Archetype.hpp>

#if RPG_DEMO_HAS_SIMD
#  include <threadmaxx_simd/vec3_ops.hpp>
#endif

#include <cstdint>
#include <vector>

namespace rpg {

namespace {

struct MoveSlice {
    const threadmaxx::internal::ArchetypeChunk* chunk;
    std::uint32_t beginFlat;
    std::uint32_t endFlat;
};

} // namespace

void MovementSystem::update(threadmaxx::SystemContext& ctx) {
    // 2026-05-20 (rev 4) — flat-row parallelFor with cross-chunk slices.
    //
    // Same deadlock fix as CubeRenderSystem rev 4. Per-chunk
    // parallelFor would have called parallelFor(1, grain=0) for tiny
    // chunks like the player's archetype — that submits a single
    // sub-job that's round-robin'd to one specific worker; if that
    // worker is the same one running MovementSystem, no other worker
    // is notified and the wave latch deadlocks.
    //
    // Flat-row dispatch always produces ~4*workerCount sub-jobs so
    // round-robin reliably fills the pool.
    //
    // The SIMD integration (simd::scale + simd::add over packed Vec3
    // staging buffers) is preserved — the integration itself is
    // cross-chunk too. Each chunk's staging is appended to a global
    // packed buffer; one big simd kernel call processes all rows.

    const auto& w = ctx.world();
    const auto chunkCount = w.archetypeChunkCount();
    if (chunkCount == 0) return;
    const float dt = static_cast<float>(ctx.dt());

    // Build slice list and stage velocities + positions into packed
    // buffers. Skip rows whose linear velocity is zero — saves the
    // SIMD work AND the per-row setTransform write.
    std::vector<MoveSlice> slices;
    slices.reserve(8);
    std::vector<threadmaxx::Vec3> linear;
    std::vector<threadmaxx::Vec3> positions;
    std::vector<std::uint32_t>    liveRows;        // local row in chunk
    std::vector<threadmaxx::EntityHandle> liveEntities;

    for (std::size_t c = 0; c < chunkCount; ++c) {
        const auto& chunk = w.archetypeChunk(c);
        if (!chunk.mask.has(threadmaxx::Component::Transform)) continue;
        if (!chunk.mask.has(threadmaxx::Component::Velocity))  continue;
        if (chunk.mask.has(threadmaxx::Component::DisabledTag)) continue;
        const auto n = chunk.entities.size();
        if (n == 0) continue;

        const std::uint32_t sliceBegin =
            static_cast<std::uint32_t>(linear.size());
        for (std::size_t row = 0; row < n; ++row) {
            const auto& v = chunk.velocities[row].linear;
            if (v.x == 0.0f && v.y == 0.0f && v.z == 0.0f) continue;
            linear.push_back(v);
            positions.push_back(chunk.transforms[row].position);
            liveRows.push_back(static_cast<std::uint32_t>(row));
            liveEntities.push_back(chunk.entities[row]);
        }
        const std::uint32_t sliceEnd =
            static_cast<std::uint32_t>(linear.size());
        if (sliceEnd == sliceBegin) continue;   // no live rows in this chunk

        MoveSlice s;
        s.chunk     = &chunk;
        s.beginFlat = sliceBegin;
        s.endFlat   = sliceEnd;
        slices.push_back(s);
    }
    if (linear.empty()) return;

    std::vector<threadmaxx::Vec3> delta(linear.size());
#if RPG_DEMO_HAS_SIMD
    // SIMD-batched integration over the FULL packed Vec3 buffer
    // (across all chunks at once). `simd::scale` and `simd::add` are
    // 8-wide AVX2 kernels.
    threadmaxx::simd::scale(
        std::span<const threadmaxx::Vec3>(linear),
        dt,
        std::span<threadmaxx::Vec3>(delta));
    threadmaxx::simd::add(
        std::span<const threadmaxx::Vec3>(positions),
        std::span<const threadmaxx::Vec3>(delta),
        std::span<threadmaxx::Vec3>(positions));
#else
    // ---- Non-SIMD reference path. Equivalent to the simd:: pair
    //      above; iterate scalar.
    for (std::size_t i = 0; i < linear.size(); ++i) {
        positions[i].x += linear[i].x * dt;
        positions[i].y += linear[i].y * dt;
        positions[i].z += linear[i].z * dt;
    }
#endif

    // Row-parallel emit over the flat LIVE count.
    const std::uint32_t live = static_cast<std::uint32_t>(linear.size());
    const auto* slicesPtr = slices.data();
    const std::uint32_t sliceCount =
        static_cast<std::uint32_t>(slices.size());
    const auto* positionsData = positions.data();
    const auto* liveRowsData  = liveRows.data();
    const auto* liveEntitiesData = liveEntities.data();

    dispatchOrInline(ctx, live,
        [slicesPtr, sliceCount, positionsData, liveRowsData, liveEntitiesData]
        (threadmaxx::Range r, threadmaxx::CommandBuffer& cb) {
            std::uint32_t si = 0;
            while (si + 1 < sliceCount && r.begin >= slicesPtr[si].endFlat) {
                ++si;
            }
            for (std::uint32_t i = r.begin; i < r.end; ++i) {
                while (si + 1 < sliceCount && i >= slicesPtr[si].endFlat) {
                    ++si;
                }
                const auto& slice = slicesPtr[si];
                threadmaxx::Transform out =
                    slice.chunk->transforms[liveRowsData[i]];
                out.position = positionsData[i];
                cb.setTransform(liveEntitiesData[i], out);
            }
        });
}

} // namespace rpg
