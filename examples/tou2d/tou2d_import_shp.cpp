// tou2d_import_shp — Tier 2 importer (.SHP → asset directory).
//
// Reads an original Tunnels-of-the-Underworld ship file (.SHP) and
// unpacks the parts we currently understand:
//
//   <outdir>/<stem>/
//     config.txt        — human-readable display name + stat blob hex
//                          dump. Defaults the manual stat values
//                          (Strength / Thrusters / Turning) when the
//                          file name matches a known ship; the user is
//                          expected to hand-edit if the imported file
//                          doesn't match the stock table.
//     header.bin        — raw bytes [0, payloadStart). Preserved so a
//                          future batch reversing the stats blob can
//                          decode without a second pass over the
//                          original .SHP.
//     body.bin          — raw bytes [payloadStart, end). Presumed to
//                          hold the palettized sprite frames; the
//                          format is opaque pending RE work.
//
// What we DO decode:
//   * Leading 0x00 padding / version byte.
//   * NUL-terminated display name at offset 1.
//   * 3-byte stat triplet immediately after the name terminator (the
//     bytes correlate weakly with the manual's Strength / Thrusters /
//     Turning table; cross-checked against all 9 stock SHP files but
//     not bit-exact yet — see TOU_PLAN.md § 3.0).
//
// What we DEFER:
//   * Sprite frame decoding (palette indices + per-frame dimensions).
//     The opaque body is preserved as body.bin so a follow-up batch
//     can reverse it without re-reading the .SHP.
//   * The constant `01 02 01 05 04` byte run observed mid-header across
//     all 9 files — likely a section-marker; meaning unknown.
//
// Usage:
//   tou2d_import_shp <input.SHP> <outdir>
//
// Standalone — no engine deps; no Vulkan. Safe to build without GLFW
// or the renderer.

#include "ShpHeader.hpp"
#include "PalCol.hpp"
#include "TgaWriter.hpp"

#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

/// One row of the manual's stock-ship table. Defaults written into
/// config.txt when the input file's stem matches a known ship; the
/// user can override post-import. Matches TOU_PLAN.md § 3.0.
struct ManualEntry {
    const char* fileStem;
    const char* manualName;
    float       strength;
    float       thrusters;
    float       turning;
};
constexpr ManualEntry kManualTable[] = {
    {"PERH", "Basic ship",   3.0f, 3.0f,  3.0f},
    {"BATM", "Batman",       2.5f, 4.0f,  3.5f},
    {"PERU", "B2 Stealth",   2.0f, 5.0f,  4.0f},
    {"SPED", "Micro Speedie",1.5f, 7.0f,  3.0f},
    {"XWIN", "X Wing",       2.5f, 4.5f,  4.0f},
    {"TIEF", "Tie Fighter",  2.5f, 5.0f,  4.5f},
    {"BEE2", "Bee",          1.0f, 10.0f, 6.5f},
    {"FLYY", "Fly",          4.0f, 3.0f,  2.0f},
    {"DEST", "Destroyer",    6.0f, 1.5f,  1.0f},
};

const ManualEntry* lookupManual(const std::string& stem) {
    std::string upper;
    upper.reserve(stem.size());
    for (char c : stem) {
        upper.push_back(static_cast<char>(
            std::toupper(static_cast<unsigned char>(c))));
    }
    for (const auto& e : kManualTable) {
        if (upper == e.fileStem) return &e;
    }
    return nullptr;
}

bool readFile(const fs::path& p, std::vector<std::uint8_t>& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    const auto n = f.tellg();
    if (n <= 0) return false;
    out.resize(static_cast<std::size_t>(n));
    f.seekg(0, std::ios::beg);
    f.read(reinterpret_cast<char*>(out.data()), n);
    return static_cast<bool>(f);
}

bool writeFile(const fs::path& p, std::span<const std::uint8_t> bytes) {
    std::ofstream f(p, std::ios::binary);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(f);
}

int usage() {
    std::fprintf(stderr,
        "usage: tou2d_import_shp <input.SHP> <outdir> [--palette <file>] [--width N]\n"
        "  Decodes the display name + stat triplet from a TOU ship file\n"
        "  and writes config.txt + header.bin + body.bin into\n"
        "  <outdir>/<stem>/. With --palette, also emits palette.tga (a\n"
        "  16x16 swatch of the loaded VGA palette) and body_strip.tga\n"
        "  (a best-effort visualization of body.bin interpreted as a raw\n"
        "  indexed-pixel strip at the given --width, default 24).\n"
        "  Sprite framing inside body.bin is still opaque — see\n"
        "  TOU_PLAN.md M4.7 for the open RE questions.\n");
    return 2;
}

/// Best-effort visualization: interpret `body` as raw 8-bit palette
/// indices laid out as a single strip of `width` pixels per row. NO
/// frame boundary detection — successive rows are just successive
/// width-byte chunks of body. The result is a tall thin TGA the user
/// can scroll through; sprite frames (if width matches) appear as
/// visible blobs in the strip. Width is a hypothesis; the user can
/// re-run with different --width values to probe.
bool writeBodyStripTga(const fs::path&                       outPath,
                       std::span<const std::uint8_t>         body,
                       const tou2d::shp::Palette&            pal,
                       std::uint16_t                         width) {
    if (width == 0) return false;
    const std::size_t rows = (body.size() + width - 1) / width;
    if (rows == 0)              return false;
    if (rows > 65535)           return false;
    const std::uint16_t height = static_cast<std::uint16_t>(rows);

    std::vector<tou2d::shp::Rgb> pixels;
    pixels.resize(static_cast<std::size_t>(width) * height);
    for (std::size_t y = 0; y < rows; ++y) {
        for (std::size_t x = 0; x < width; ++x) {
            const std::size_t i = y * width + x;
            const std::size_t src = y * width + x;
            std::uint8_t idx = 0;
            if (src < body.size()) idx = body[src];
            const auto& e = pal.entries[idx];
            pixels[i] = { e.r, e.g, e.b };
        }
    }
    return tou2d::shp::writeTga24(outPath.string(), width, height,
                                  std::span<const tou2d::shp::Rgb>(pixels));
}

bool writePaletteSwatchTga(const fs::path&            outPath,
                           const tou2d::shp::Palette& pal) {
    // 16x16 swatch, one entry per cell, scaled 8x for human inspection:
    // 128x128 final image — readable in any viewer without zoom.
    constexpr std::uint16_t cell  = 8;
    constexpr std::uint16_t dim   = 16 * cell;
    std::vector<tou2d::shp::Rgb> pixels(static_cast<std::size_t>(dim) * dim);
    for (std::uint16_t y = 0; y < dim; ++y) {
        for (std::uint16_t x = 0; x < dim; ++x) {
            const std::uint16_t cx = static_cast<std::uint16_t>(x / cell);
            const std::uint16_t cy = static_cast<std::uint16_t>(y / cell);
            const std::size_t   pi = static_cast<std::size_t>(cy) * 16 + cx;
            const auto&         e  = pal.entries[pi];
            pixels[static_cast<std::size_t>(y) * dim + x] = { e.r, e.g, e.b };
        }
    }
    return tou2d::shp::writeTga24(outPath.string(), dim, dim,
                                  std::span<const tou2d::shp::Rgb>(pixels));
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) return usage();
    const fs::path inputPath = argv[1];
    const fs::path outRoot   = argv[2];

    // ---- Optional flags --------------------------------------------------
    // --palette <path>  load 768-byte VGA palette, emit palette.tga +
    //                   body_strip.tga; without it those files are skipped.
    // --width   N       width-per-row hypothesis for the body strip
    //                   visualization (default 24 — matches the apparent
    //                   24x24-ish frame dim of the PERH/DEST sprites).
    fs::path        palettePath;
    std::uint16_t   stripWidth = 24;
    for (int i = 3; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--palette" && i + 1 < argc) {
            palettePath = argv[++i];
        } else if (a == "--width" && i + 1 < argc) {
            const int v = std::atoi(argv[++i]);
            if (v <= 0 || v > 65535) {
                std::fprintf(stderr, "[import_shp] --width must be in [1, 65535]\n");
                return 2;
            }
            stripWidth = static_cast<std::uint16_t>(v);
        } else {
            std::fprintf(stderr, "[import_shp] unrecognized flag: %s\n", a.c_str());
            return usage();
        }
    }

    std::vector<std::uint8_t> data;
    if (!readFile(inputPath, data)) {
        std::fprintf(stderr, "[import_shp] failed to read %s\n",
                     inputPath.string().c_str());
        return 1;
    }

    tou2d::shp::ParsedHeader hdr;
    if (!tou2d::shp::parseHeader(data, hdr)) {
        std::fprintf(stderr,
            "[import_shp] %s is not a recognized TOU ship file\n",
            inputPath.string().c_str());
        return 1;
    }

    const std::string stem = inputPath.stem().string();
    const fs::path outDir  = outRoot / stem;
    std::error_code ec;
    fs::create_directories(outDir, ec);
    if (ec) {
        std::fprintf(stderr, "[import_shp] cannot create %s: %s\n",
                     outDir.string().c_str(), ec.message().c_str());
        return 1;
    }

    std::span<const std::uint8_t> all = data;
    std::span<const std::uint8_t> header = all.subspan(0, hdr.payloadStart);
    std::span<const std::uint8_t> body   =
        all.subspan(hdr.payloadStart, all.size() - hdr.payloadStart);

    if (!writeFile(outDir / "header.bin", header) ||
        !writeFile(outDir / "body.bin",   body)) {
        std::fprintf(stderr, "[import_shp] failed to write extracted blobs\n");
        return 1;
    }

    const ManualEntry* manual = lookupManual(stem);

    {
        std::ofstream cfg(outDir / "config.txt");
        if (!cfg) {
            std::fprintf(stderr, "[import_shp] failed to open config.txt\n");
            return 1;
        }
        cfg << "# tou2d Tier-2 import of " << inputPath.filename().string() << '\n';
        cfg << "name = "            << hdr.displayName << '\n';
        cfg << "header_bytes = "    << header.size()   << '\n';
        cfg << "body_bytes = "      << body.size()     << '\n';
        cfg << "# Raw stat triplet from offset name+1 (encoding unresolved):\n";
        cfg << "stat_bytes_hex = "
            << std::hex
            << static_cast<int>(hdr.statTriplet[0]) << ' '
            << static_cast<int>(hdr.statTriplet[1]) << ' '
            << static_cast<int>(hdr.statTriplet[2])
            << std::dec << '\n';
        if (manual) {
            cfg << "# Manual fall-back stats (from TOU_PLAN.md § 3.0):\n";
            cfg << "manual_name = "      << manual->manualName << '\n';
            cfg << "manual_strength = "  << manual->strength   << '\n';
            cfg << "manual_thrusters = " << manual->thrusters  << '\n';
            cfg << "manual_turning = "   << manual->turning    << '\n';
        } else {
            cfg << "# Unknown ship file stem; no manual fall-back.\n";
            cfg << "# Override stats manually by editing this file.\n";
        }
    }

    // ---- Optional palette + body visualization --------------------------
    bool wrotePalette = false;
    bool wroteStrip   = false;
    if (!palettePath.empty()) {
        std::vector<std::uint8_t> palRaw;
        if (!readFile(palettePath, palRaw)) {
            std::fprintf(stderr, "[import_shp] failed to read palette %s\n",
                         palettePath.string().c_str());
            return 1;
        }
        tou2d::shp::Palette pal;
        if (!tou2d::shp::parsePalette(palRaw, pal)) {
            std::fprintf(stderr,
                "[import_shp] palette %s is not a 768-byte VGA palette\n",
                palettePath.string().c_str());
            return 1;
        }
        wrotePalette = writePaletteSwatchTga(outDir / "palette.tga", pal);
        wroteStrip   = writeBodyStripTga(outDir / "body_strip.tga",
                                         body, pal, stripWidth);
        if (!wrotePalette || !wroteStrip) {
            std::fprintf(stderr,
                "[import_shp] failed to write palette.tga / body_strip.tga\n");
            return 1;
        }
    }

    std::printf("[import_shp] %s -> %s\n", inputPath.string().c_str(),
                outDir.string().c_str());
    std::printf("  name:        %s\n", hdr.displayName.c_str());
    std::printf("  stats (hex): %02x %02x %02x\n",
                hdr.statTriplet[0], hdr.statTriplet[1], hdr.statTriplet[2]);
    std::printf("  header.bin:  %zu bytes\n", header.size());
    std::printf("  body.bin:    %zu bytes (sprite section — opaque)\n",
                body.size());
    if (wrotePalette) {
        std::printf("  palette.tga: 128x128 swatch (from %s)\n",
                    palettePath.string().c_str());
        std::printf("  body_strip:  %ux%zu speculative-decode strip\n",
                    static_cast<unsigned>(stripWidth),
                    (body.size() + stripWidth - 1) / stripWidth);
    }
    if (!manual) {
        std::printf("  manual:      no stock-ship match for '%s'\n",
                    stem.c_str());
    }
    return 0;
}
