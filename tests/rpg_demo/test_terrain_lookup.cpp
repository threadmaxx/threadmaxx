// §3.11.8 batch D8 — Heightmap correctness + slope-reject gameplay test.
//
// Two halves:
//
// 1. Pure-data check: construct a Heightmap and verify
//    `heightAt(cellCenter)` matches the cell sample exactly,
//    bilinear midpoint between two known cells matches the average,
//    and queries outside the world extent clamp to the boundary
//    rather than reading out-of-bounds.
//
// 2. Slope-reject behavior: find a steep cell in the same heightmap,
//    verify `slopeAt` reports a value above the rejection threshold
//    used by `NPCBrainSystem`. Confirms the integration contract:
//    if the heightmap reports a cell as steep, the brain will refuse
//    it.

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

    // ---- 1. Direct grid samples match heightAt at cell centers --------------
    // Cell `(ix, iz)` covers `[-extent/2 + ix*cs, -extent/2 + (ix+1)*cs]`.
    // Its center is at world `(-extent/2 + (ix + 0.5)*cs, ...)`. heightAt
    // at the cell *origin* should equal sampleCell(ix, iz) exactly
    // (bilinear weight is 1 at the corner).
    const float half     = hmap.worldExtent() * 0.5f;
    const float cellSize = hmap.cellSize();
    int sampleCount = 0;
    for (std::uint32_t ix : {std::uint32_t(0), std::uint32_t(13),
                              std::uint32_t(rpg::kHeightmapResolution / 2),
                              std::uint32_t(rpg::kHeightmapResolution - 2)}) {
        for (std::uint32_t iz : {std::uint32_t(0), std::uint32_t(7),
                                  std::uint32_t(rpg::kHeightmapResolution / 3),
                                  std::uint32_t(rpg::kHeightmapResolution - 2)}) {
            const float worldX = -half + static_cast<float>(ix) * cellSize;
            const float worldZ = -half + static_cast<float>(iz) * cellSize;
            const float fromQuery  = hmap.heightAt(worldX, worldZ);
            const float fromSample = hmap.sampleCell(ix, iz);
            CHECK(relErr(fromQuery, fromSample) < 1e-4f);
            ++sampleCount;
        }
    }
    CHECK(sampleCount == 16);

    // ---- 2. Bilinear midpoint matches corner average -----------------------
    // Pick an interior cell and query its mid-point. The expected
    // value is the average of the 4 corner samples (bilinear with
    // u = v = 0.5).
    {
        const std::uint32_t ix = 40, iz = 70;
        const float worldX = -half + (static_cast<float>(ix) + 0.5f) * cellSize;
        const float worldZ = -half + (static_cast<float>(iz) + 0.5f) * cellSize;
        const float corners[4] = {
            hmap.sampleCell(ix,     iz    ),
            hmap.sampleCell(ix + 1, iz    ),
            hmap.sampleCell(ix,     iz + 1),
            hmap.sampleCell(ix + 1, iz + 1),
        };
        const float expected = 0.25f * (corners[0] + corners[1] +
                                         corners[2] + corners[3]);
        const float got = hmap.heightAt(worldX, worldZ);
        CHECK(relErr(got, expected) < 1e-4f);
    }

    // ---- 3. Out-of-bounds clamps to boundary -------------------------------
    {
        const float hLeftEdge  = hmap.heightAt(-half, 0.0f);
        const float hWayLeft   = hmap.heightAt(-half * 4.0f, 0.0f);
        CHECK(relErr(hLeftEdge, hWayLeft) < 1e-4f);

        const float hRightEdge = hmap.heightAt(+half * 0.999f, 0.0f);
        const float hWayRight  = hmap.heightAt(+half * 10.0f, 0.0f);
        CHECK(relErr(hRightEdge, hWayRight) < 0.01f);
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
