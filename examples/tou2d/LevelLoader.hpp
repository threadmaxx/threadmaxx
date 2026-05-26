#pragma once

#include "DemoTypes.hpp"

#include <filesystem>
#include <string>

namespace tou2d {

/// Result of loading an imported level. Carries the imported grid
/// extents so the caller can adjust the ship spawn point / camera
/// bounds. `loaded=false` on any failure path.
struct LoadedLevelInfo {
    bool         loaded     = false;
    std::string  name;
    std::int32_t cellsX     = 0;
    std::int32_t cellsY     = 0;
    std::int32_t solidCount = 0;  // # tiles classified Solid (Air skipped)
};

/// Load an imported level directory (produced by `tou2d_import_lev`).
/// `attribute.tga` MUST be a 24-bit uncompressed TGA. Tiles are
/// classified by the dominant non-black colour of their source-pixel
/// block; black-majority blocks are Air and contribute nothing.
///
/// M3.3 — populates `grid` directly instead of spawning per-tile
/// entities. Caller must own the grid; loader resets it before fill.
LoadedLevelInfo loadImportedLevel(TerrainGrid& grid,
                                  const std::filesystem::path& levelDir);

} // namespace tou2d
