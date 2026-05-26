#pragma once

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Engine.hpp>
#include <threadmaxx/UserComponent.hpp>

#include <filesystem>
#include <string>

namespace tou2d {

/// Result of loading an imported level directory. Carries the imported
/// grid extents so the caller can adjust the ship spawn point / camera
/// bounds. Empty / zero on failure.
struct LoadedLevelInfo {
    bool         loaded     = false;
    std::string  name;
    std::int32_t cellsX     = 0;  // tile-grid width  (after downsample)
    std::int32_t cellsY     = 0;  // tile-grid height (after downsample)
    std::int32_t solidCount = 0;  // # tiles spawned (Air tiles skipped)
};

/// Number of source attribute-TGA pixels that map to one runtime tile
/// along each axis. Picked so a typical 1000×1091 imported level
/// produces ~31×34 tiles — comfortably visible inside the ~11×7-tile
/// camera viewport without flooding the scene with cubes.
inline constexpr std::int32_t kImportedPxPerTile = 32;

/// Load an imported level directory (produced by `tou2d_import_lev`).
/// The directory must contain `attribute.tga` — a 24-bit uncompressed
/// TGA. Optional companions (`visual.jpg`, `parallax.jpg`,
/// `config.txt`) are ignored for M2.7; M2.8+ wires them in.
///
/// Spawns one `TerrainBlock` entity per non-Air tile. The tile grid is
/// downsampled from the pixel attribute map at `kImportedPxPerTile`,
/// classifying each downsample block by its dominant non-black colour
/// (or Air if more than half the pixels are black).
///
/// Caller-owned `seed` accumulates the spawn commands; the engine
/// commits them at the end of `Engine::initialize`. Returns metadata
/// even on partial failure so the host can keep running with whatever
/// loaded (mirrors the rpg_demo fallback posture).
LoadedLevelInfo loadImportedLevel(threadmaxx::Engine& engine,
                                  threadmaxx::CommandBuffer& seed,
                                  threadmaxx::UserComponentId terrainBlockId,
                                  const std::filesystem::path& levelDir);

} // namespace tou2d
