#include "LevelLoader.hpp"

#include "DemoTypes.hpp"

// stb_image: tou2d_core's implementation TU. Marked non-static so the
// symbols emit as extern in this object file; main.cpp's copy is
// STB_IMAGE_STATIC and therefore file-scope, so no link clash. See the
// note in main.cpp's stb_image include block.
#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

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
    std::vector<std::uint8_t> bgr;
};

/// Read entire file into a byte buffer. Returns false on any I/O error.
bool readFile(const fs::path& p, std::vector<std::uint8_t>& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    const auto n = f.tellg();
    if (n <= 0) return false;
    out.resize(static_cast<std::size_t>(n));
    f.seekg(0, std::ios::beg);
    f.read(reinterpret_cast<char*>(out.data()),
           static_cast<std::streamsize>(n));
    return static_cast<bool>(f);
}

/// Decode `visual.jpg` into a binary attribute mask: dark pixels become
/// Air, bright pixels become Solid. The threshold tracks the same
/// "black-majority" heuristic the TGA path uses (`classifyTile`) so a
/// JPEG-fallback level looks like one painted with the simplest 2-tone
/// editor palette. Returns a same-shape `TgaImage` filled with two
/// distinct colors (black or white) — downstream classification by
/// dominant-non-black works on it without changes.
///
/// Threshold is luminance-based (BT.601 weights). A 64-step low-pass
/// keeps the JPEG's anti-aliased ground edges from speckling Air pixels
/// inside Solid tiles.
bool deriveAttributeFromJpeg(const fs::path& path, TgaImage& out) {
    std::vector<std::uint8_t> raw;
    if (!readFile(path, raw)) return false;
    int w = 0, h = 0, channels = 0;
    std::uint8_t* rgba = stbi_load_from_memory(
        raw.data(), static_cast<int>(raw.size()),
        &w, &h, &channels, 3);
    if (!rgba || w <= 0 || h <= 0) {
        if (rgba) stbi_image_free(rgba);
        return false;
    }
    out.width   = w;
    out.height  = h;
    out.topDown = true;  // stbi returns rows top-down
    out.bgr.assign(static_cast<std::size_t>(w) * h * 3, 0);
    // BT.601 luminance: Y = 0.299*R + 0.587*G + 0.114*B. Threshold
    // chosen empirically against the 4 shipped TOU levels' visual JPGs:
    // ground is mostly bright-mid (Y > 80), sky/Air is near-black
    // (Y < 40). 64 sits in the gap. The synthesis is intentionally
    // coarse — users wanting fidelity should run the importer's
    // sibling-TGA path or hand-paint an attribute.tga.
    constexpr std::uint32_t kLumaThreshold = 64;
    for (std::int32_t y = 0; y < h; ++y) {
        for (std::int32_t x = 0; x < w; ++x) {
            const std::size_t srcI =
                (static_cast<std::size_t>(y) * w + x) * 3;
            const std::uint32_t r = rgba[srcI];
            const std::uint32_t g = rgba[srcI + 1];
            const std::uint32_t b = rgba[srcI + 2];
            const std::uint32_t luma = (299u * r + 587u * g + 114u * b) / 1000u;
            const bool solid = luma >= kLumaThreshold;
            // BGR + solid → white triple; Air → black triple (already
            // zeroed). classifyTile uses black-vs-non-black so any non-
            // zero color is treated as Solid; pick white for clarity.
            if (solid) {
                const std::size_t dstI =
                    (static_cast<std::size_t>(y) * w + x) * 3;
                out.bgr[dstI    ] = 0xFFu;
                out.bgr[dstI + 1] = 0xFFu;
                out.bgr[dstI + 2] = 0xFFu;
            }
        }
    }
    stbi_image_free(rgba);
    return true;
}

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
    out.topDown = (imgDesc & 0x20) != 0;
    out.bgr.resize(static_cast<std::size_t>(w) * h * 3);
    f.read(reinterpret_cast<char*>(out.bgr.data()),
           static_cast<std::streamsize>(out.bgr.size()));
    return static_cast<bool>(f);
}

Attribute classifyTile(const TgaImage& img,
                       std::int32_t tileX, std::int32_t tileY) noexcept {
    const std::int32_t px0 = tileX * kImportedPxPerTile;
    const std::int32_t py0 = tileY * kImportedPxPerTile;
    const std::int32_t pxEnd = std::min(px0 + kImportedPxPerTile, img.width);
    const std::int32_t pyEnd = std::min(py0 + kImportedPxPerTile, img.height);

    std::uint32_t blackCount    = 0;
    std::uint32_t nonBlackCount = 0;
    for (std::int32_t py = py0; py < pyEnd; ++py) {
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
    if (blackCount > nonBlackCount) return Attribute::Air;
    return Attribute::Solid;
}

} // namespace

LoadedLevelInfo loadImportedLevel(TerrainGrid& grid,
                                  const std::filesystem::path& levelDir) {
    LoadedLevelInfo info{};
    info.name = levelDir.filename().string();

    const fs::path tgaPath = levelDir / "attribute.tga";
    TgaImage img;
    bool usedJpegFallback = false;
    if (!loadTga24Uncompressed(tgaPath, img)) {
        // No / unsupported attribute.tga — try the visual.jpg JPEG
        // fallback so drop-in level dirs that ship only the visual
        // layer remain loadable. Coarse luminance-based Air/Solid
        // split; users who want full attribute fidelity should
        // hand-paint an attribute.tga or run the importer's sibling-TGA
        // path. The fallback fires for desert/minibase/woods on a
        // vanilla TOU install (only jungle ships a sibling makelev TGA).
        const fs::path jpegPath = levelDir / "visual.jpg";
        if (!deriveAttributeFromJpeg(jpegPath, img)) {
            std::fprintf(stderr,
                "[loader] %s: no attribute.tga and no visual.jpg fallback usable\n",
                levelDir.string().c_str());
            return info;
        }
        usedJpegFallback = true;
    }

    const std::int32_t cellsX = (img.width  + kImportedPxPerTile - 1) / kImportedPxPerTile;
    const std::int32_t cellsY = (img.height + kImportedPxPerTile - 1) / kImportedPxPerTile;
    info.cellsX = cellsX;
    info.cellsY = cellsY;

    grid.reset(cellsX, cellsY);
    const std::int32_t halfX = cellsX / 2;
    const std::int32_t halfY = cellsY / 2;

    for (std::int32_t cy = 0; cy < cellsY; ++cy) {
        for (std::int32_t cx = 0; cx < cellsX; ++cx) {
            const Attribute attr = classifyTile(img, cx, cy);
            if (attr == Attribute::Air) continue;

            const std::int32_t worldCellX = cx - halfX;
            const std::int32_t worldCellY = halfY - cy;
            // M4.6 — was 192 (≈ 24 dumbfire shots / 38 spread pellets
            // after the M4.5 damage nerf — felt like punching through
            // armoured concrete). 24 lands a tile at ≈ 3 dumbfire
            // shots / ≈ 5 spread pellets, in the same order of
            // magnitude as ship combat. Bedrock (255) is still set
            // separately by the synthetic-arena path and never used
            // by this loader, so cosmetic perimeter walls remain
            // unbreakable.
            grid.setSolid(worldCellX, worldCellY, /*hp=*/24, attr);
            ++info.solidCount;
        }
    }

    info.loaded = true;
    std::printf("[loader] %s: %dx%d tiles (%dx%d source px); %d solid%s\n",
                info.name.c_str(), cellsX, cellsY,
                img.width, img.height, info.solidCount,
                usedJpegFallback ? " (JPEG-derived fallback)" : "");
    return info;
}

} // namespace tou2d
