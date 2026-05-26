#include "LevelLoader.hpp"

#include "DemoTypes.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <vector>

namespace tou2d {

namespace {

namespace fs = std::filesystem;

struct TgaImage {
    std::int32_t width  = 0;
    std::int32_t height = 0;
    bool         topDown = false;
    std::vector<std::uint8_t> bgr;   // tightly packed B,G,R triples in TGA order
};

/// Load a 24-bit uncompressed TGA (image type 2). Returns false for any
/// other variant — the makelev pipeline only emits this form (see
/// `TOU/toudoc_makelev.htm`).
bool loadTga24Uncompressed(const fs::path& path, TgaImage& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::array<std::uint8_t, 18> hdr{};
    f.read(reinterpret_cast<char*>(hdr.data()), 18);
    if (!f) return false;

    const std::uint8_t idLen       = hdr[0];
    const std::uint8_t cmapType    = hdr[1];
    const std::uint8_t imageType   = hdr[2];
    const std::uint16_t w          = static_cast<std::uint16_t>(hdr[12] | (hdr[13] << 8));
    const std::uint16_t h          = static_cast<std::uint16_t>(hdr[14] | (hdr[15] << 8));
    const std::uint8_t bpp         = hdr[16];
    const std::uint8_t imgDesc     = hdr[17];

    if (cmapType != 0 || imageType != 2 || bpp != 24 || w == 0 || h == 0) {
        return false;
    }
    if (idLen) f.seekg(idLen, std::ios::cur);

    out.width   = w;
    out.height  = h;
    out.topDown = (imgDesc & 0x20) != 0;   // bit 5 = origin at top-left
    out.bgr.resize(static_cast<std::size_t>(w) * h * 3);
    f.read(reinterpret_cast<char*>(out.bgr.data()),
           static_cast<std::streamsize>(out.bgr.size()));
    return static_cast<bool>(f);
}

/// Walk a (kImportedPxPerTile × kImportedPxPerTile) source-pixel block
/// in the TGA at the given tile coordinate, count black vs non-black
/// pixels, and return the Attribute classification.
///
/// Black (0,0,0) is "Air" per the original level-editor docs (see
/// `TOU/toudoc_makelev.htm`). Any other color is treated as Solid for
/// M2; M3 will partition non-black into Damage / Repair / Spawn /
/// turret-color by mapping (R, G, B) → colors.png palette entries.
Attribute classifyTile(const TgaImage& img,
                       std::int32_t tileX, std::int32_t tileY) noexcept {
    const std::int32_t px0 = tileX * kImportedPxPerTile;
    const std::int32_t py0 = tileY * kImportedPxPerTile;
    const std::int32_t pxEnd = std::min(px0 + kImportedPxPerTile, img.width);
    const std::int32_t pyEnd = std::min(py0 + kImportedPxPerTile, img.height);

    std::uint32_t blackCount    = 0;
    std::uint32_t nonBlackCount = 0;
    for (std::int32_t py = py0; py < pyEnd; ++py) {
        // TGA default origin is bottom-left; flip Y unless topDown.
        const std::int32_t srcRow = img.topDown ? py : (img.height - 1 - py);
        const std::size_t  rowOff = static_cast<std::size_t>(srcRow) * img.width * 3;
        for (std::int32_t px = px0; px < pxEnd; ++px) {
            const std::size_t i = rowOff + static_cast<std::size_t>(px) * 3;
            const std::uint8_t b = img.bgr[i];
            const std::uint8_t g = img.bgr[i + 1];
            const std::uint8_t r = img.bgr[i + 2];
            if (r == 0 && g == 0 && b == 0) ++blackCount;
            else                            ++nonBlackCount;
        }
    }
    // Air if a strict majority of pixels are black; Solid otherwise.
    // 50/50 ties get rounded to Solid so floor/ceiling tiles stay
    // collidable when the colour boundary sits exactly on a tile edge.
    if (blackCount > nonBlackCount) return Attribute::Air;
    return Attribute::Solid;
}

} // namespace

LoadedLevelInfo loadImportedLevel(threadmaxx::Engine& engine,
                                  threadmaxx::CommandBuffer& seed,
                                  threadmaxx::UserComponentId terrainBlockId,
                                  const std::filesystem::path& levelDir) {
    LoadedLevelInfo info{};
    info.name = levelDir.filename().string();

    const fs::path tgaPath = levelDir / "attribute.tga";
    TgaImage img;
    if (!loadTga24Uncompressed(tgaPath, img)) {
        std::fprintf(stderr,
            "[loader] %s: missing or unsupported attribute.tga (need 24-bit uncompressed)\n",
            tgaPath.string().c_str());
        return info;
    }

    const std::int32_t cellsX = (img.width  + kImportedPxPerTile - 1) / kImportedPxPerTile;
    const std::int32_t cellsY = (img.height + kImportedPxPerTile - 1) / kImportedPxPerTile;
    info.cellsX = cellsX;
    info.cellsY = cellsY;

    // Center the imported grid on world origin so the existing camera /
    // ship-spawn defaults still look reasonable.
    const std::int32_t halfX = cellsX / 2;
    const std::int32_t halfY = cellsY / 2;

    for (std::int32_t cy = 0; cy < cellsY; ++cy) {
        for (std::int32_t cx = 0; cx < cellsX; ++cx) {
            const Attribute attr = classifyTile(img, cx, cy);
            if (attr == Attribute::Air) continue;

            const std::int32_t worldCellX = cx - halfX;
            // Flip Y so the TGA's bottom-of-image becomes -Y in world
            // space — matches the level-editor's intuitive "top of
            // screen = ceiling" convention.
            const std::int32_t worldCellY = halfY - cy;

            const auto handle = engine.reserveEntityHandle();
            threadmaxx::Bundle b = {};
            b.transform.position = {
                static_cast<float>(worldCellX) * kTileWorldUnits,
                static_cast<float>(worldCellY) * kTileWorldUnits,
                0.0f,
            };
            b.transform.scale = {kTileWorldUnits * 0.96f,
                                 kTileWorldUnits * 0.96f,
                                 kTileWorldUnits};
            b.renderTag = threadmaxx::RenderTag{0, 1, 0u};
            b.initialMask = threadmaxx::ComponentSet{
                threadmaxx::Component::Transform,
                threadmaxx::Component::RenderTag,
            };
            seed.spawnBundle(handle, b);

            TerrainBlock blk{};
            blk.attr  = attr;
            blk.hp    = 0xFF;
            blk.cellX = static_cast<std::int16_t>(worldCellX);
            blk.cellY = static_cast<std::int16_t>(worldCellY);
            threadmaxx::addUserComponent(seed, terrainBlockId, handle, blk);
            ++info.solidCount;
        }
    }

    info.loaded = true;
    std::printf("[loader] %s: %dx%d tiles (%dx%d source px); %d solid spawned\n",
                info.name.c_str(), cellsX, cellsY,
                img.width, img.height, info.solidCount);
    return info;
}

} // namespace tou2d
