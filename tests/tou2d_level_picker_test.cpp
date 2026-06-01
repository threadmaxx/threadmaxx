// tou2d_level_picker_test — 2026-05-31 imported-level picker contract.
//
// Pins:
//   * LevelEnumerator returns the directory list sorted by name; only
//     dirs containing `attribute.tga` OR `visual.jpg` are listed.
//   * UISystem::setImportedLevels populates the Level scroller's domain
//     and the formatter prints the selected level's name; with an empty
//     list the formatter prints "(no levels)" and cycling is a no-op.
//   * Cycling past the last level lands on the trailing
//     `kImportedLevelNone` ("synthetic") slot; wrapping is symmetric.
//   * The Level row is at MatchSetup row index 5 (between UseGen and
//     GenSeed).
//   * LevelLoader's JPEG-derived fallback fires when attribute.tga is
//     absent but visual.jpg is present — produces a valid grid.

#include "Check.hpp"

#include "../examples/tou2d/DemoTypes.hpp"
#include "../examples/tou2d/LevelEnumerator.hpp"
#include "../examples/tou2d/LevelLoader.hpp"
#include "../examples/tou2d/MatchSetup.hpp"
#include "../examples/tou2d/UISystem.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

// Disk fixture: under `<tmp>/<unique>/levels/<stem>/<file>` write a few
// dirs with various content shapes. The enumerator + loader read these
// at runtime; clean up on shutdown.
struct DiskFixture {
    fs::path root;
    DiskFixture() {
        const auto seed = std::random_device{}();
        std::mt19937 rng(seed);
        root = fs::temp_directory_path() /
               ("tou2d_picker_" + std::to_string(rng()));
        fs::create_directories(root / "levels");
    }
    ~DiskFixture() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
};

void writeFile(const fs::path& p, std::span<const std::uint8_t> bytes) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
}

void writeMinimalTga(const fs::path& p, int w, int h) {
    // 18-byte uncompressed 24-bit TGA header followed by w*h*3 zeros.
    std::vector<std::uint8_t> tga = {
        0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        std::uint8_t(w & 0xFF), std::uint8_t((w >> 8) & 0xFF),
        std::uint8_t(h & 0xFF), std::uint8_t((h >> 8) & 0xFF),
        24, 0x20  // top-down
    };
    tga.insert(tga.end(),
               static_cast<std::size_t>(w) * h * 3, 0xFFu);  // all-Solid
    writeFile(p, {tga.data(), tga.size()});
}

// Tiny embedded JPEG of a 16x16 grey image — used to exercise the
// loader's JPEG-derived fallback path without a separate asset
// dependency. Captured by piping a stb_image-encoded byte array.
// Generated once via `stbi_write_jpg_to_func` on a 16x16 grey buffer at
// quality=80; pinned here as a literal so the test is hermetic.
const std::vector<std::uint8_t>& miniGreyJpeg() {
    // 16×16, grayscale-ish JPEG. We carry the raw bytes inline so the
    // test doesn't depend on any external file. (Generated offline with
    // `stbi_write_jpg`; size ≈ 320 bytes; visually a near-uniform mid-
    // grey that the loader's luminance threshold will classify Solid.)
    static const std::vector<std::uint8_t> bytes = {
        0xFF,0xD8,0xFF,0xE0,0x00,0x10,0x4A,0x46,0x49,0x46,0x00,0x01,
        0x01,0x00,0x00,0x01,0x00,0x01,0x00,0x00,0xFF,0xDB,0x00,0x43,
        0x00,0x10,0x0B,0x0C,0x0E,0x0C,0x0A,0x10,0x0E,0x0D,0x0E,0x12,
        0x11,0x10,0x13,0x18,0x28,0x1A,0x18,0x16,0x16,0x18,0x31,0x23,
        0x25,0x1D,0x28,0x3A,0x33,0x3D,0x3C,0x39,0x33,0x38,0x37,0x40,
        0x48,0x5C,0x4E,0x40,0x44,0x57,0x45,0x37,0x38,0x50,0x6D,0x51,
        0x57,0x5F,0x62,0x67,0x68,0x67,0x3E,0x4D,0x71,0x79,0x70,0x64,
        0x78,0x5C,0x65,0x67,0x63,0xFF,0xC0,0x00,0x0B,0x08,0x00,0x10,
        0x00,0x10,0x01,0x01,0x11,0x00,0xFF,0xC4,0x00,0x1F,0x00,0x00,
        0x01,0x05,0x01,0x01,0x01,0x01,0x01,0x01,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,
        0x09,0x0A,0x0B,0xFF,0xC4,0x00,0xB5,0x10,0x00,0x02,0x01,0x03,
        0x03,0x02,0x04,0x03,0x05,0x05,0x04,0x04,0x00,0x00,0x01,0x7D,
        0x01,0x02,0x03,0x00,0x04,0x11,0x05,0x12,0x21,0x31,0x41,0x06,
        0x13,0x51,0x61,0x07,0x22,0x71,0x14,0x32,0x81,0x91,0xA1,0x08,
        0x23,0x42,0xB1,0xC1,0x15,0x52,0xD1,0xF0,0x24,0x33,0x62,0x72,
        0x82,0x09,0x0A,0x16,0x17,0x18,0x19,0x1A,0x25,0x26,0x27,0x28,
        0x29,0x2A,0x34,0x35,0x36,0x37,0x38,0x39,0x3A,0x43,0x44,0x45,
        0x46,0x47,0x48,0x49,0x4A,0x53,0x54,0x55,0x56,0x57,0x58,0x59,
        0x5A,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6A,0x73,0x74,0x75,
        0x76,0x77,0x78,0x79,0x7A,0x83,0x84,0x85,0x86,0x87,0x88,0x89,
        0x8A,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9A,0xA2,0xA3,
        0xA4,0xA5,0xA6,0xA7,0xA8,0xA9,0xAA,0xB2,0xB3,0xB4,0xB5,0xB6,
        0xB7,0xB8,0xB9,0xBA,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,
        0xCA,0xD2,0xD3,0xD4,0xD5,0xD6,0xD7,0xD8,0xD9,0xDA,0xE1,0xE2,
        0xE3,0xE4,0xE5,0xE6,0xE7,0xE8,0xE9,0xEA,0xF1,0xF2,0xF3,0xF4,
        0xF5,0xF6,0xF7,0xF8,0xF9,0xFA,0xFF,0xDA,0x00,0x08,0x01,0x01,
        0x00,0x00,0x3F,0x00,0xFB,0xD2,0x8A,0x28,0xAF,0xFF,0xD9
    };
    return bytes;
}

} // namespace

int main() {
    using namespace tou2d;

    // ---- LevelEnumerator: empty / non-existent root -----------------
    {
        const auto v = enumerateImportedLevels("/this/path/does/not/exist");
        CHECK(v.empty());
    }

    // ---- LevelEnumerator: only valid dirs are listed, sorted by name
    {
        DiskFixture d;
        // Valid: jungle has attribute.tga.
        writeMinimalTga(d.root / "levels" / "jungle" / "attribute.tga", 4, 4);
        // Valid: desert has visual.jpg (no TGA needed for enumeration).
        const auto& jpg = miniGreyJpeg();
        writeFile(d.root / "levels" / "desert" / "visual.jpg",
                  {jpg.data(), jpg.size()});
        // Invalid: empty dir.
        fs::create_directories(d.root / "levels" / "_empty");
        // Invalid: file at top level (not a dir).
        writeFile(d.root / "levels" / "stray.txt", {});
        // Valid: alphabetical winner — `aaa` should sort first.
        writeMinimalTga(d.root / "levels" / "aaa" / "attribute.tga", 2, 2);

        const auto v = enumerateImportedLevels(d.root);
        CHECK_EQ(v.size(), std::size_t{3});
        CHECK_EQ(v[0].name, std::string{"aaa"});
        CHECK_EQ(v[1].name, std::string{"desert"});
        CHECK_EQ(v[2].name, std::string{"jungle"});
    }

    // ---- UISystem: empty list → no-op cycle + "(no levels)" formatter
    {
        UISystem ui(nullptr, UIScreen::MatchSetup);
        auto& s = ui.matchSetup();
        CHECK_EQ(s.importedLevelIdx, kImportedLevelNone);
        // Move focus to Level row (index 5).
        for (int i = 0; i < 5; ++i) ui.moveFocus(+1);
        CHECK_EQ(ui.focusIndex(), std::int32_t{5});
        ui.cycleFocused(+1);
        CHECK_EQ(s.importedLevelIdx, kImportedLevelNone);
        ui.cycleFocused(-1);
        CHECK_EQ(s.importedLevelIdx, kImportedLevelNone);

        char buf[64];
        ui.formatRow(5, buf, sizeof(buf));
        CHECK(std::strcmp(buf, "Level: (no levels)") == 0);
    }

    // ---- UISystem: populated list → cycle through + sentinel-wrap ---
    {
        UISystem ui(nullptr, UIScreen::MatchSetup);
        const std::string names[] = {"jungle", "desert"};
        ui.setImportedLevels({names, 2});
        auto& s = ui.matchSetup();
        // Move focus to Level row.
        for (int i = 0; i < 5; ++i) ui.moveFocus(+1);
        CHECK_EQ(s.importedLevelIdx, kImportedLevelNone);
        // Cycle +1 from None lands on idx 0 (first level).
        ui.cycleFocused(+1);
        CHECK_EQ(s.importedLevelIdx, std::uint8_t{0});
        // +1 → idx 1.
        ui.cycleFocused(+1);
        CHECK_EQ(s.importedLevelIdx, std::uint8_t{1});
        // +1 → trailing None sentinel.
        ui.cycleFocused(+1);
        CHECK_EQ(s.importedLevelIdx, kImportedLevelNone);
        // -1 from None wraps back to last level.
        ui.cycleFocused(-1);
        CHECK_EQ(s.importedLevelIdx, std::uint8_t{1});

        // Format prints the level name.
        char buf[64];
        ui.formatRow(5, buf, sizeof(buf));
        CHECK(std::strcmp(buf, "Level: desert") == 0);
        // Reset to None and verify "(synthetic)" label.
        s.importedLevelIdx = kImportedLevelNone;
        ui.formatRow(5, buf, sizeof(buf));
        CHECK(std::strcmp(buf, "Level: (synthetic)") == 0);
    }

    // ---- UISystem: setImportedLevels shrinks index out of range -----
    {
        UISystem ui(nullptr, UIScreen::MatchSetup);
        const std::string three[] = {"a", "b", "c"};
        ui.setImportedLevels({three, 3});
        ui.matchSetup().importedLevelIdx = 2;
        // Now drop list to size 1 — idx 2 is out of range; reset to None.
        const std::string one[] = {"a"};
        ui.setImportedLevels({one, 1});
        CHECK_EQ(ui.matchSetup().importedLevelIdx, kImportedLevelNone);
    }

    // ---- LevelLoader: JPEG fallback produces a non-empty grid -------
    {
        DiskFixture d;
        const auto& jpg = miniGreyJpeg();
        writeFile(d.root / "levels" / "fallback" / "visual.jpg",
                  {jpg.data(), jpg.size()});
        TerrainGrid grid;
        const auto info = loadImportedLevel(
            grid, d.root / "levels" / "fallback");
        CHECK(info.loaded);
        CHECK_EQ(info.name, std::string{"fallback"});
        // 16x16 source / 4-pixel tile → 4x4 grid. The grey JPEG decodes
        // to luma >= 64 so every tile is Solid.
        CHECK_EQ(info.cellsX, std::int32_t{4});
        CHECK_EQ(info.cellsY, std::int32_t{4});
        CHECK(info.solidCount > 0);
    }

    // ---- LevelLoader: rejects dir with neither tga nor jpg ----------
    {
        DiskFixture d;
        fs::create_directories(d.root / "levels" / "broken");
        TerrainGrid grid;
        const auto info = loadImportedLevel(
            grid, d.root / "levels" / "broken");
        CHECK(!info.loaded);
    }

    EXIT_WITH_RESULT();
}
