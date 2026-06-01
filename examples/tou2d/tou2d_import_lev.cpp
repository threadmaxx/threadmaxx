// tou2d_import_lev — Tier 1 importer (.lev → asset directory).
//
// Reads an original Tunnels-of-the-Underworld .lev file (v1.4) and
// unpacks it into a directory the runtime can load. Layout produced:
//
//   <outdir>/<stem>/
//     visual.jpg         — JPEG #1 from the .lev (foreground level art)
//     parallax.jpg       — JPEG #2 from the .lev (parallax background)
//     config_block.bin   — raw 0x42..0x3BD config block (940 bytes)
//     section3.bin       — raw section-3 bytes (presumed RLE attribute
//                          map; encoding not yet reversed — see
//                          M2.6 in TOU_PLAN.md)
//     config.txt         — human-readable fields we *did* parse
//     attribute.tga      — optional copy of <input_dir>/../makelev/<stem>.tga
//                          (sibling source if it exists; the loader
//                          prefers this over decoding section3.bin)
//
// Usage:
//   tou2d_import_lev <input.lev> <outdir>
//
// Standalone — no engine deps; no Vulkan. Safe to build without GLFW
// or the renderer.

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb/stb_image.h>

#include "LevConfig.hpp"

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

constexpr const char* kMagic = "TOU level file v1.4";   // followed by \r\n\x1a

struct LevHeader {
    std::uint32_t jpegStart;    // 0x16
    std::uint32_t jpegEnd;      // 0x1A
    std::uint32_t section3Start;// 0x1E
    std::string   author;       // 0x22, 128 bytes ASCII NUL-padded
    std::string   email;        // 0xA2, 128 bytes ASCII NUL-padded
};

// LevConfig + parseLevConfig live in LevConfig.hpp so tests can pin
// the format without linking the importer. See TOU_RE.md
// "section3 — partial decode" for the layout table.

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

bool parseHeader(std::span<const std::uint8_t> data, LevHeader& hdr) {
    if (data.size() < 0x42) return false;
    if (std::memcmp(data.data(), kMagic, 19) != 0) return false;

    // Read three u32 LE.
    auto u32 = [&](std::size_t off) {
        return static_cast<std::uint32_t>(data[off]      ) |
               (static_cast<std::uint32_t>(data[off + 1]) <<  8) |
               (static_cast<std::uint32_t>(data[off + 2]) << 16) |
               (static_cast<std::uint32_t>(data[off + 3]) << 24);
    };
    hdr.jpegStart     = u32(0x16);
    hdr.jpegEnd       = u32(0x1A);
    hdr.section3Start = u32(0x1E);
    if (hdr.jpegStart >= data.size() || hdr.jpegEnd > data.size() ||
        hdr.section3Start > data.size() || hdr.jpegStart >= hdr.jpegEnd ||
        hdr.jpegEnd > hdr.section3Start) {
        return false;
    }

    // Author: 128 bytes at offset 0x22, NUL-padded ASCII (Latin-1
    // accents in the wild — see Jungle.lev's "Hannu Kankaanpää").
    const char* a = reinterpret_cast<const char*>(data.data() + 0x22);
    std::size_t alen = 0;
    while (alen < 128 && a[alen] != '\0') ++alen;
    hdr.author.assign(a, alen);
    // Email: 128 bytes at offset 0xA2.
    const char* e = reinterpret_cast<const char*>(data.data() + 0xA2);
    std::size_t elen = 0;
    while (elen < 128 && e[elen] != '\0') ++elen;
    hdr.email.assign(e, elen);
    return true;
}

bool decodeJpegSofDimensions(std::span<const std::uint8_t> jpeg,
                             std::uint32_t& outW, std::uint32_t& outH) {
    // Scan for an SOF0/SOF1/SOF2/SOF3 marker and return (w, h).
    std::size_t i = 0;
    while (i + 8 < jpeg.size()) {
        if (jpeg[i] != 0xFF) { ++i; continue; }
        const std::uint8_t marker = jpeg[i + 1];
        i += 2;
        if (marker == 0xD8 || marker == 0xD9 ||
            (marker >= 0xD0 && marker <= 0xD7)) {
            continue;  // standalone markers, no payload
        }
        if (i + 1 >= jpeg.size()) return false;
        const std::uint16_t seglen =
            (static_cast<std::uint16_t>(jpeg[i]) << 8) |
             static_cast<std::uint16_t>(jpeg[i + 1]);
        if (marker == 0xC0 || marker == 0xC1 ||
            marker == 0xC2 || marker == 0xC3) {
            if (i + 7 >= jpeg.size()) return false;
            outH = (static_cast<std::uint32_t>(jpeg[i + 3]) << 8) |
                    static_cast<std::uint32_t>(jpeg[i + 4]);
            outW = (static_cast<std::uint32_t>(jpeg[i + 5]) << 8) |
                    static_cast<std::uint32_t>(jpeg[i + 6]);
            return true;
        }
        i += seglen;
    }
    return false;
}

int usage() {
    std::fprintf(stderr,
        "usage: tou2d_import_lev <input.lev> <outdir>\n"
        "  Extracts JPEG#1 (visual), JPEG#2 (parallax), and raw config /\n"
        "  section-3 blobs into <outdir>/<stem>/ . If a sibling\n"
        "  ../makelev/<stem>.tga is found next to <input.lev>, it is\n"
        "  copied in as attribute.tga.\n");
    return 2;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) return usage();
    const fs::path inputPath = argv[1];
    const fs::path outRoot   = argv[2];

    std::vector<std::uint8_t> data;
    if (!readFile(inputPath, data)) {
        std::fprintf(stderr, "[import_lev] failed to read %s\n",
                     inputPath.string().c_str());
        return 1;
    }

    LevHeader hdr{};
    if (!parseHeader(data, hdr)) {
        std::fprintf(stderr, "[import_lev] %s is not a TOU v1.4 level file\n",
                     inputPath.string().c_str());
        return 1;
    }

    const std::string stem = inputPath.stem().string();
    const fs::path outDir  = outRoot / stem;
    std::error_code ec;
    fs::create_directories(outDir, ec);
    if (ec) {
        std::fprintf(stderr, "[import_lev] cannot create %s: %s\n",
                     outDir.string().c_str(), ec.message().c_str());
        return 1;
    }

    std::span<const std::uint8_t> all = data;
    std::span<const std::uint8_t> jpeg1 =
        all.subspan(hdr.jpegStart, hdr.jpegEnd - hdr.jpegStart);
    std::span<const std::uint8_t> jpeg2 =
        all.subspan(hdr.jpegEnd, hdr.section3Start - hdr.jpegEnd);
    std::span<const std::uint8_t> sect3 =
        all.subspan(hdr.section3Start, all.size() - hdr.section3Start);
    std::span<const std::uint8_t> configBlock =
        all.subspan(0x42, 0x3BE - 0x42);

    if (!writeFile(outDir / "visual.jpg",      jpeg1) ||
        !writeFile(outDir / "parallax.jpg",    jpeg2) ||
        !writeFile(outDir / "section3.bin",    sect3) ||
        !writeFile(outDir / "config_block.bin",configBlock)) {
        std::fprintf(stderr, "[import_lev] failed to write extracted blobs\n");
        return 1;
    }

    std::uint32_t v1w = 0, v1h = 0, v2w = 0, v2h = 0;
    decodeJpegSofDimensions(jpeg1, v1w, v1h);
    decodeJpegSofDimensions(jpeg2, v2w, v2h);

    // Parse the per-level /KEY blob (0x122..0x1B8). Layout fully
    // mapped against the 4 shipped originals on 2026-05-31; see
    // TOU_RE.md "/KEY value blob" for the exact field offsets.
    tou2d::LevConfig lvc{};
    const bool gotCfg = tou2d::parseLevConfig(data, lvc);

    // config.txt — decoded /KEY value parameters + import metadata.
    // Loader code consumes section3.bin / attribute.tga directly;
    // this file is human-readable and forms the basis for hand-
    // editable knob overrides.
    {
        std::ofstream cfg(outDir / "config.txt");
        if (!cfg) {
            std::fprintf(stderr, "[import_lev] failed to open config.txt\n");
            return 1;
        }
        cfg << "# tou2d Tier-1 import of " << inputPath.filename().string() << '\n';
        cfg << "name = "       << stem      << '\n';
        cfg << "author = "     << hdr.author<< '\n';
        cfg << "email = "      << hdr.email << '\n';
        cfg << "visual_jpg_size = "    << v1w << 'x' << v1h << '\n';
        cfg << "parallax_jpg_size = "  << v2w << 'x' << v2h << '\n';
        cfg << "section3_bytes = "     << sect3.size() << '\n';
        cfg << "config_block_bytes = " << configBlock.size() << '\n';
        if (gotCfg) {
            cfg << "# --- decoded /KEY value parameters ---\n";
            cfg << "para = "        << (lvc.para        ? "yes" : "no") << '\n';
            cfg << "civil = "       << +lvc.civil       << '\n';
            cfg << "bomb = "        << +lvc.bomb        << '\n';
            cfg << "waterc = "      << +lvc.watercR << ',' << +lvc.watercG
                                    << ',' << +lvc.watercB << '\n';
            cfg << "disablerun = "  << (lvc.disableRun  ? "yes" : "no") << '\n';
            cfg << "gravity = "     << (lvc.gravity    * 10) << '\n';
            cfg << "resistance = "  << (lvc.resistance * 10) << '\n';
            cfg << "colldamage = "  << (lvc.collDamage * 10) << '\n';
            cfg << "bouncing = "    << (lvc.bouncing   * 10) << '\n';
            cfg << "ambient = "     << +lvc.ambient     << '\n';
            cfg << "parallaxat = "  << +lvc.parallaxAt  << '\n';
            cfg << "gglevel = "     << (lvc.ggLevel ? "yes" : "no") << '\n';
            cfg << "ggtheme = "     << lvc.ggTheme      << '\n';
            cfg << "ggshape = "     << (lvc.ggShape ? "yes" : "no") << '\n';
            cfg << "repair = "      << +lvc.repair      << '\n';
            cfg << "stuffd = "      << +lvc.stuffD      << '\n';
            cfg << "signd = "       << +lvc.signD       << '\n';
            cfg << "randomseed = "  << lvc.randomSeed   << '\n';
        }
    }

    // Best-effort attribute.tga: copy from sibling makelev/<stem>.tga.
    // The original TOU install ships these next to each .lev's source
    // tree (`<install>/makelev/Jungle.tga` for jungle.lev). Filenames in
    // makelev are capitalized so we case-fold the stem.
    bool gotTga = false;
    auto tryCopyTga = [&](const fs::path& candidate) {
        if (gotTga) return;
        if (!fs::exists(candidate, ec) || ec) return;
        fs::copy_file(candidate, outDir / "attribute.tga",
                      fs::copy_options::overwrite_existing, ec);
        if (!ec) gotTga = true;
    };
    const fs::path makelev = inputPath.parent_path().parent_path() / "makelev";
    std::string capStem = stem;
    if (!capStem.empty()) {
        capStem[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(capStem[0])));
    }
    tryCopyTga(makelev / (capStem + ".tga"));
    tryCopyTga(makelev / (stem + ".tga"));

    // 2026-05-31 — JPEG-derived fallback. The original TOU install
    // only ships `makelev/Jungle.tga`; the other 3 levels (desert,
    // minibase, woods) have no sibling source TGA. Without a fallback
    // those levels end up missing `attribute.tga` and the LevelLoader's
    // TGA-only path bails before spawning any terrain. The fallback
    // decodes the embedded visual.jpg and writes a binary Air/Solid
    // TGA derived by luminance threshold — coarse but enough for the
    // levels to LOAD; users who want full attribute fidelity should
    // hand-paint a TGA into the output dir.
    //
    // Note: section3.bin IS the original attribute map (RLE-encoded;
    // u32 record_count + N×20-byte entity records + (value, count)
    // pairs at half visual JPG resolution). Decoding it is parked —
    // see TOU_RE.md § 6 for the cracked subset.
    bool deriveAttr = false;
    if (!gotTga && !jpeg1.empty()) {
        int w = 0, h = 0, ch = 0;
        std::uint8_t* rgb = stbi_load_from_memory(
            jpeg1.data(), static_cast<int>(jpeg1.size()),
            &w, &h, &ch, 3);
        if (rgb && w > 0 && h > 0) {
            // Write 24-bit uncompressed TGA: 18-byte header + BGR rows.
            std::vector<std::uint8_t> tga;
            tga.reserve(18 + std::size_t(w) * h * 3);
            tga.insert(tga.end(), {
                0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                static_cast<std::uint8_t>(w & 0xFF),
                static_cast<std::uint8_t>((w >> 8) & 0xFF),
                static_cast<std::uint8_t>(h & 0xFF),
                static_cast<std::uint8_t>((h >> 8) & 0xFF),
                24, 0x20  // bpp=24, top-down
            });
            constexpr std::uint32_t kLumaThreshold = 64;
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    const int i = (y * w + x) * 3;
                    const std::uint32_t r = rgb[i];
                    const std::uint32_t g = rgb[i + 1];
                    const std::uint32_t b = rgb[i + 2];
                    const std::uint32_t luma =
                        (299u * r + 587u * g + 114u * b) / 1000u;
                    const std::uint8_t v = (luma >= kLumaThreshold) ? 0xFFu : 0x00u;
                    tga.push_back(v); tga.push_back(v); tga.push_back(v);
                }
            }
            deriveAttr = writeFile(outDir / "attribute.tga", tga);
        }
        if (rgb) stbi_image_free(rgb);
    }

    std::printf("[import_lev] %s -> %s\n", inputPath.string().c_str(),
                outDir.string().c_str());
    std::printf("  visual:     %ux%u (%zu bytes)\n",
                v1w, v1h, jpeg1.size());
    std::printf("  parallax:   %ux%u (%zu bytes)\n",
                v2w, v2h, jpeg2.size());
    std::printf("  section3:   %zu bytes (RLE attribute map; decoder parked — see TOU_RE.md § 6)\n",
                sect3.size());
    std::printf("  attribute.tga: %s\n",
                gotTga ? "copied from sibling makelev/"
                : deriveAttr ? "derived from visual.jpg (coarse Air/Solid)"
                : "not produced");
    return 0;
}
