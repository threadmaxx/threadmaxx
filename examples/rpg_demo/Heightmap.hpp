#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace rpg {

/// §3.11 batch D8 — procedural 2D heightmap (header-only).
///
/// Generates a `resolution × resolution` height grid via 4-octave fBm
/// value noise at construction time, then answers `heightAt` /
/// `slopeAt` queries.
///
/// **2026-05-22 (round 9)** — pivoted to voxel-style terrain.
/// `heightAt` is now a step function that quantizes the underlying
/// noise field to integer multiples of `kBlockUnit` (1.0 m). Each
/// heightmap cell becomes a single voxel column whose top sits at the
/// quantized height; adjacent cells differing by N block units form
/// an N-tall vertical wall. Game-side traversal code (`MovementSystem`,
/// `TerrainAttachSystem`, `NPCBrainSystem`) is responsible for the
/// 1-block step-up cap — `Heightmap` itself is purely a query API.
///
/// World coords are centered: queries in `[-worldExtent/2,
/// +worldExtent/2]` along each of X and Z map onto the grid; queries
/// outside that range clamp to the boundary (never out-of-bounds-read).
///
/// Deterministic: same `(resolution, worldExtent, seed)` triple
/// produces a byte-identical height field. Used by D8's terrain spawn
/// loop, the per-tick `TerrainAttachSystem`, `NPCBrainSystem`'s
/// slope-reject path (now redundant with the step-up cap; kept as a
/// secondary filter), and `bench/terrain_query_bench`.
class Heightmap {
public:
    Heightmap(std::uint32_t resolution,
              float worldExtent,
              std::uint32_t seed = 0xD8000001u)
        : res_(resolution),
          extent_(worldExtent),
          cellSize_(worldExtent / static_cast<float>(resolution)),
          invCellSize_(static_cast<float>(resolution) / worldExtent),
          minH_(0.0f),
          maxH_(0.0f),
          heights_(static_cast<std::size_t>(resolution) * resolution, 0.0f)
    {
        // fBm field rescaled to a height range that gives noticeable
        // hills and produces gradients steep enough to actually trip
        // the gameplay-side slope-reject threshold
        // (`kSlopeRejectThreshold` in DemoTypes.hpp). At 12m scale and
        // ~6 noise cycles across the world the steepest cells span
        // >2m of vertical change per 2m of horizontal step. Verified
        // by `test_terrain_lookup`.
        constexpr float kHeightScale = 12.0f;
        constexpr float kNoiseFreq   = 6.0f;
        float lo = +1.0e30f;
        float hi = -1.0e30f;
        for (std::uint32_t z = 0; z < resolution; ++z) {
            for (std::uint32_t x = 0; x < resolution; ++x) {
                const float fx = static_cast<float>(x) / static_cast<float>(resolution) * kNoiseFreq;
                const float fz = static_cast<float>(z) / static_cast<float>(resolution) * kNoiseFreq;
                const float h  = fbm_(fx, fz, seed) * kHeightScale;
                heights_[static_cast<std::size_t>(z) * resolution + x] = h;
                lo = std::min(lo, h);
                hi = std::max(hi, h);
            }
        }
        minH_ = lo;
        maxH_ = hi;
    }

    std::uint32_t resolution()  const noexcept { return res_; }
    float         worldExtent() const noexcept { return extent_; }
    float         minHeight()   const noexcept { return minH_; }
    float         maxHeight()   const noexcept { return maxH_; }
    float         cellSize()    const noexcept { return cellSize_; }

    /// Quantized world-space height at `(x, z)`. Out-of-range queries
    /// clamp to the boundary.
    ///
    /// **2026-05-22 (round 9)** — pivoted to a Minecraft-style step
    /// function. Each query falls inside exactly one heightmap cell;
    /// the returned height is that cell's sample rounded down to the
    /// nearest `kBlockUnit` (default 1.0 m), giving the world a
    /// discrete grid look. No interpolation between cells — adjacent
    /// cells differing by ≥ 2 block units form a sheer wall that
    /// game-side movement code is expected to reject as a too-tall
    /// step (see `TerrainAttachSystem`'s 1-block step-up rule).
    ///
    /// This makes `heightAt(cellOrigin) == quantize(sampleCell(...))`
    /// AND `heightAt(cellMidpoint) == quantize(sampleCell(...))` —
    /// every point inside a cell reads the same height. Bilinear /
    /// smooth interpolation is gone; that was the previous "ramp"
    /// look and doesn't fit voxel terrain.
    float heightAt(float x, float z) const noexcept {
        const float half = extent_ * 0.5f;
        const float gx = (x + half) * invCellSize_;
        const float gz = (z + half) * invCellSize_;
        // Clamp index to last valid cell. No ix+1 lookup needed for
        // the step form, so the headroom is just one cell.
        const float maxIdx = static_cast<float>(res_) - 1.0f;
        const float clampedX = std::clamp(gx, 0.0f, maxIdx);
        const float clampedZ = std::clamp(gz, 0.0f, maxIdx);
        const std::uint32_t ix = static_cast<std::uint32_t>(clampedX);
        const std::uint32_t iz = static_cast<std::uint32_t>(clampedZ);
        const float raw = heights_[static_cast<std::size_t>(iz) * res_ + ix];
        return quantize_(raw);
    }

    /// World-space block unit. Cells whose sampled height differs by
    /// less than `blockUnit` snap to the same quantized value;
    /// vertical traversal in steps of `blockUnit` is treated as one
    /// "block" by gameplay code (player + NPCs can step up exactly
    /// one block per tick; ≥ 2 is a wall).
    float blockUnit() const noexcept { return kBlockUnit; }

    /// Gradient magnitude (Δheight / Δworld) at `(x, z)` via central
    /// differences across one cell. Dimensionless: 0 = flat, ~1 = 45°.
    /// Used by `NPCBrainSystem` to reject steep Wander targets.
    float slopeAt(float x, float z) const noexcept {
        const float eps = cellSize_;
        const float dx = (heightAt(x + eps, z) - heightAt(x - eps, z)) / (2.0f * eps);
        const float dz = (heightAt(x, z + eps) - heightAt(x, z - eps)) / (2.0f * eps);
        return std::sqrt(dx * dx + dz * dz);
    }

    /// Direct grid sample (no interpolation). Bounds-checked via
    /// clamp. Useful for testing the underlying noise field.
    float sampleCell(std::uint32_t ix, std::uint32_t iz) const noexcept {
        const std::uint32_t cx = std::min(ix, res_ - 1);
        const std::uint32_t cz = std::min(iz, res_ - 1);
        return heights_[static_cast<std::size_t>(cz) * res_ + cx];
    }

private:
    static std::uint32_t hash32_(std::uint32_t x, std::uint32_t y,
                                  std::uint32_t seed) noexcept {
        std::uint32_t h = seed;
        h ^= x + 0x9E3779B9u + (h << 6) + (h >> 2);
        h ^= y + 0x9E3779B9u + (h << 6) + (h >> 2);
        h ^= h >> 16;
        h *= 0x7feb352du;
        h ^= h >> 15;
        h *= 0x846ca68bu;
        h ^= h >> 16;
        return h;
    }
    static float hashFloat_(std::uint32_t x, std::uint32_t y,
                             std::uint32_t seed) noexcept {
        return static_cast<float>(hash32_(x, y, seed)) * (1.0f / 4294967295.0f);
    }
    static float fade_(float t) noexcept {
        return t * t * (3.0f - 2.0f * t);
    }
    // Voxel quantization (round 9, 2026-05-22). Rounds toward
    // -infinity so e.g. raw=5.7 → 5, raw=-0.3 → -1. Block boundaries
    // sit at integer multiples of kBlockUnit.
    static constexpr float kBlockUnit = 1.0f;
    static float quantize_(float v) noexcept {
        return std::floor(v / kBlockUnit) * kBlockUnit;
    }
    static float valueNoise_(float x, float y, std::uint32_t seed) noexcept {
        const float fxFloor = std::floor(x);
        const float fyFloor = std::floor(y);
        const std::uint32_t xi = static_cast<std::uint32_t>(static_cast<std::int32_t>(fxFloor));
        const std::uint32_t yi = static_cast<std::uint32_t>(static_cast<std::int32_t>(fyFloor));
        const float xf = x - fxFloor;
        const float yf = y - fyFloor;
        const float v00 = hashFloat_(xi,     yi,     seed);
        const float v10 = hashFloat_(xi + 1, yi,     seed);
        const float v01 = hashFloat_(xi,     yi + 1, seed);
        const float v11 = hashFloat_(xi + 1, yi + 1, seed);
        const float u = fade_(xf);
        const float v = fade_(yf);
        return (1.0f - u) * (1.0f - v) * v00
             +        u  * (1.0f - v) * v10
             + (1.0f - u) *        v  * v01
             +        u  *        v  * v11;
    }
    static float fbm_(float x, float y, std::uint32_t seed) noexcept {
        float total = 0.0f;
        float amp = 0.5f;
        float freq = 1.0f;
        for (int i = 0; i < 4; ++i) {
            total += amp * valueNoise_(x * freq, y * freq,
                seed + static_cast<std::uint32_t>(i) * 0x9E3779B9u);
            freq *= 2.0f;
            amp  *= 0.5f;
        }
        return total;
    }

    std::uint32_t      res_;
    float              extent_;
    float              cellSize_;
    float              invCellSize_;
    float              minH_;
    float              maxH_;
    std::vector<float> heights_;
};

} // namespace rpg
