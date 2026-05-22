// §3.11.8 batch D8 — Heightmap correctness + slope-reject gameplay test.
//
// 2026-05-22 (round 9, voxel pivot) — `heightAt` is now a STEP
// function that quantizes the raw fBm field to integer multiples of
// `blockUnit` (1.0 m). Every query inside a cell returns the same
// height — there's no interpolation. The tests below verify:
//
// 1. `heightAt(anyPointInsideCell) == floor(sampleCell(ix,iz))`.
// 2. Adjacent-cell heights differ in integer block-unit steps —
//    so a 1-block ledge is allowed traversal, ≥ 2-block is a wall.
// 3. Out-of-range queries clamp to the boundary.
// 4. Slope-reject still finds cells above the threshold (it now
//    reports gradients in quantized units, but the noise field's
//    range is enough to produce some > 0.35).

#include "Check.hpp"
#include "Heightmap.hpp"
#include "DemoTypes.hpp"

#include <cmath>
#include <cstdio>
#include <cstdint>

namespace {

float relErr(float a, float b) noexcept {
    const float den = std::max(1.0f, std::fabs(a) + std::fabs(b));
    return std::fabs(a - b) / den;
}

} // namespace

int main() {
    using rpg::Heightmap;

    // Same configuration the demo uses at boot.
    Heightmap hmap(rpg::kHeightmapResolution,
                   rpg::kTerrainExtent,
                   rpg::kHeightmapSeed);

    // ---- 1. Direct grid samples → quantized heightAt at cell origin --------
    // Cell `(ix, iz)` covers `[-extent/2 + ix*cs, -extent/2 + (ix+1)*cs]`.
    // The step-function heightAt returns `floor(sampleCell(ix,iz) /
    // blockUnit) * blockUnit` everywhere inside the cell.
    const float half     = hmap.worldExtent() * 0.5f;
    const float cellSize = hmap.cellSize();
    const float bu       = hmap.blockUnit();
    auto quantize = [bu](float v) { return std::floor(v / bu) * bu; };
    int sampleCount = 0;
    for (std::uint32_t ix : {std::uint32_t(0), std::uint32_t(13),
                              std::uint32_t(rpg::kHeightmapResolution / 2),
                              std::uint32_t(rpg::kHeightmapResolution - 2)}) {
        for (std::uint32_t iz : {std::uint32_t(0), std::uint32_t(7),
                                  std::uint32_t(rpg::kHeightmapResolution / 3),
                                  std::uint32_t(rpg::kHeightmapResolution - 2)}) {
            // Sample near the cell center to dodge floating-point
            // boundary ambiguity (a query exactly at the cell origin
            // lands in cell (ix-1, iz-1) for negative coords thanks
            // to `floor`-style indexing).
            const float worldX = -half + (static_cast<float>(ix) + 0.5f) * cellSize;
            const float worldZ = -half + (static_cast<float>(iz) + 0.5f) * cellSize;
            const float fromQuery  = hmap.heightAt(worldX, worldZ);
            const float fromSample = quantize(hmap.sampleCell(ix, iz));
            CHECK(relErr(fromQuery, fromSample) < 1e-4f);
            ++sampleCount;
        }
    }
    CHECK(sampleCount == 16);

    // ---- 2. heightAt is constant inside a cell -----------------------------
    // Voxel terrain: every point inside the same cell reports the same
    // quantized height. Sample 4 sub-cell positions in a representative
    // cell and verify they all match.
    {
        const std::uint32_t ix = 40, iz = 70;
        const float ox = -half + (static_cast<float>(ix) + 0.5f) * cellSize;
        const float oz = -half + (static_cast<float>(iz) + 0.5f) * cellSize;
        const float h0 = hmap.heightAt(ox,                       oz);
        const float h1 = hmap.heightAt(ox + 0.25f * cellSize,    oz);
        const float h2 = hmap.heightAt(ox - 0.25f * cellSize,    oz + 0.25f * cellSize);
        const float h3 = hmap.heightAt(ox,                       oz - 0.25f * cellSize);
        CHECK(h0 == h1);
        CHECK(h0 == h2);
        CHECK(h0 == h3);
        // Height is on the quantization grid.
        CHECK(std::fabs(h0 - quantize(h0)) < 1e-6f);
    }

    // ---- 3. Out-of-bounds clamps to boundary -------------------------------
    {
        // Sample slightly inside the boundary so the clamped query
        // lands in the same cell as the OOB query.
        const float hLeftEdge  = hmap.heightAt(-half + cellSize * 0.5f, 0.0f);
        const float hWayLeft   = hmap.heightAt(-half * 4.0f, 0.0f);
        CHECK(hLeftEdge == hWayLeft);

        const float hRightEdge = hmap.heightAt(+half - cellSize * 0.5f, 0.0f);
        const float hWayRight  = hmap.heightAt(+half * 10.0f, 0.0f);
        CHECK(hRightEdge == hWayRight);
    }

    // ---- 4. Slope rejection -----------------------------------------------
    // Walk the grid and verify the field actually contains cells the
    // brain would reject. The threshold is `kSlopeRejectThreshold` —
    // if every cell tests below it, slope-reject is gameplay-dead and
    // the heightmap parameters need tuning (which is what the
    // constants in `Heightmap.cpp` and `DemoTypes.hpp` are for).
    float maxSlope     = 0.0f;
    std::uint32_t steepCount = 0;
    for (std::uint32_t iz = 4; iz < rpg::kHeightmapResolution - 4; ++iz) {
        for (std::uint32_t ix = 4; ix < rpg::kHeightmapResolution - 4; ++ix) {
            const float worldX = -half + (static_cast<float>(ix) + 0.5f) * cellSize;
            const float worldZ = -half + (static_cast<float>(iz) + 0.5f) * cellSize;
            const float s = hmap.slopeAt(worldX, worldZ);
            if (s > maxSlope) maxSlope = s;
            if (s > rpg::kSlopeRejectThreshold) ++steepCount;
        }
    }
    std::printf("[test_terrain_lookup] maxSlope=%.3f steepCellCount=%u "
                "threshold=%.3f\n",
                maxSlope, steepCount, rpg::kSlopeRejectThreshold);
    CHECK(steepCount > 0);
    CHECK(maxSlope > rpg::kSlopeRejectThreshold);

    // ---- 5. Determinism — same seed → same field --------------------------
    {
        Heightmap b(rpg::kHeightmapResolution,
                    rpg::kTerrainExtent,
                    rpg::kHeightmapSeed);
        CHECK(relErr(b.heightAt(1.234f, 5.678f),
                     hmap.heightAt(1.234f, 5.678f)) < 1e-6f);
        CHECK(b.minHeight() == hmap.minHeight());
        CHECK(b.maxHeight() == hmap.maxHeight());

        Heightmap c(rpg::kHeightmapResolution,
                    rpg::kTerrainExtent,
                    rpg::kHeightmapSeed + 1u);
        // Different seed → different field. Probability of accidental
        // bit-identical fields is essentially zero.
        CHECK(c.heightAt(1.234f, 5.678f) != hmap.heightAt(1.234f, 5.678f));
    }

    std::printf("[test_terrain_lookup] heightmap res=%u extent=%.1f "
                "min=%.2f max=%.2f cellSize=%.3f\n",
                hmap.resolution(), hmap.worldExtent(),
                hmap.minHeight(), hmap.maxHeight(),
                hmap.cellSize());

    EXIT_WITH_RESULT();
}
