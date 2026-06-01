// tou2d_lev_config_test — 2026-05-31 .lev /KEY value blob contract.
//
// Pins the offset-level layout of the .lev container's per-level
// game-design parameters (water color, gravity, ambient sound, GG
// theme, random seed). Reverse-engineered by cross-referencing each
// shipped TOU level against its makelev/<Stem>.txt sidecar on
// 2026-05-31; see examples/tou2d/TOU_RE.md for the full table.
//
// The test is hermetic: it builds a synthetic .lev byte blob with
// known values at the exact offsets and verifies the parser round-
// trips each field. A second case pins the all-defaults block (zero-
// initialized data ⇒ default-constructed LevConfig).

#include "Check.hpp"

#include "../examples/tou2d/LevConfig.hpp"

#include <cstdint>
#include <cstring>
#include <vector>

namespace {

// Build a synthetic .lev buffer with the .lev /KEY blob filled at the
// canonical offsets. Returns the buffer; callers parse it.
std::vector<std::uint8_t> makeSyntheticLev(
        bool          para,
        std::uint8_t  civil,
        std::uint8_t  bomb,
        std::uint8_t  watercR,
        std::uint8_t  watercG,
        std::uint8_t  watercB,
        bool          disableRun,
        std::uint8_t  gravityX10,
        std::uint8_t  resistanceX10,
        std::uint8_t  collDamageX10,
        std::uint8_t  bouncingX10,
        std::uint8_t  ambient,
        std::uint8_t  parallaxAt,
        bool          ggLevel,
        const char*   ggTheme,
        bool          ggShape,
        std::uint8_t  repair,
        std::uint8_t  stuffD,
        std::uint8_t  signD,
        std::uint16_t randomSeed) {
    std::vector<std::uint8_t> buf(tou2d::kLevConfigMinFileBytes, 0u);
    buf[0x122] = para ? 1u : 0u;
    buf[0x123] = civil;
    buf[0x124] = bomb;
    buf[0x125] = watercR;
    buf[0x126] = watercG;
    buf[0x127] = watercB;
    buf[0x128] = disableRun ? 1u : 0u;
    buf[0x129] = gravityX10;
    buf[0x12A] = resistanceX10;
    buf[0x12B] = collDamageX10;
    buf[0x12C] = bouncingX10;
    buf[0x12D] = ambient;
    buf[0x12E] = parallaxAt;
    buf[0x12F] = ggLevel ? 1u : 0u;
    {
        const std::size_t n = std::strlen(ggTheme);
        const std::size_t k = (n < 16u) ? n : 16u;
        std::memcpy(buf.data() + 0x130, ggTheme, k);
    }
    buf[0x1B0] = ggShape ? 1u : 0u;
    buf[0x1B1] = repair;
    buf[0x1B2] = stuffD;
    buf[0x1B3] = signD;
    buf[0x1B6] = static_cast<std::uint8_t>(randomSeed & 0xFFu);
    buf[0x1B7] = static_cast<std::uint8_t>((randomSeed >> 8) & 0xFFu);
    return buf;
}

void test_jungle_shipped_bytes() {
    // Pins parses for the byte values seen in TOU/levels/jungle.lev.
    // Jungle.txt: PARA=yes, CIVIL=10, BOMB=5, WATERC=0,120,157,
    // DISABLERUN=yes, GRAVITY=100, RESISTANCE=100, COLLDAMAGE=50,
    // BOUNCING=50, AMBIENT=1, PARALLAXAT=2, GGLEVEL=no,
    // GGTHEME="the earth", GGSHAPE=yes, REPAIR=20, STUFFD=20,
    // SIGND=20, RANDOMSEED=54321.
    const auto buf = makeSyntheticLev(
        /*para*/true, /*civil*/10, /*bomb*/5,
        /*waterc*/0, 120, 157,
        /*disableRun*/true,
        /*gravity*/10, /*resistance*/10, /*collDamage*/5, /*bouncing*/5,
        /*ambient*/1, /*parallaxAt*/2, /*ggLevel*/false,
        "the earth",
        /*ggShape*/true, /*repair*/20, /*stuffD*/20, /*signD*/20,
        /*randomSeed*/54321);
    tou2d::LevConfig cfg{};
    CHECK(tou2d::parseLevConfig(std::span<const std::uint8_t>(buf), cfg));
    CHECK(cfg.para);
    CHECK_EQ(int(cfg.civil), 10);
    CHECK_EQ(int(cfg.bomb), 5);
    CHECK_EQ(int(cfg.watercR), 0);
    CHECK_EQ(int(cfg.watercG), 120);
    CHECK_EQ(int(cfg.watercB), 157);
    CHECK(cfg.disableRun);
    // Stored value is /10; ×10 → original percent.
    CHECK_EQ(int(cfg.gravity) * 10, 100);
    CHECK_EQ(int(cfg.resistance) * 10, 100);
    CHECK_EQ(int(cfg.collDamage) * 10, 50);
    CHECK_EQ(int(cfg.bouncing) * 10, 50);
    CHECK_EQ(int(cfg.ambient), 1);
    CHECK_EQ(int(cfg.parallaxAt), 2);
    CHECK(!cfg.ggLevel);
    CHECK_EQ(cfg.ggTheme, std::string("the earth"));
    CHECK(cfg.ggShape);
    CHECK_EQ(int(cfg.repair), 20);
    CHECK_EQ(int(cfg.stuffD), 20);
    CHECK_EQ(int(cfg.signD), 20);
    CHECK_EQ(int(cfg.randomSeed), 54321);
}

void test_woods_shipped_bytes() {
    // Pins parses for byte values seen in TOU/levels/woods.lev:
    // WATERC=50,50,255 (bright blue), AMBIENT=0, PARALLAXAT=4.
    const auto buf = makeSyntheticLev(
        true, 10, 5,
        50, 50, 255,
        false, 10, 10, 10, 10,
        0, 4, false,
        "the earth",
        true, 20, 20, 20,
        54321);
    tou2d::LevConfig cfg{};
    CHECK(tou2d::parseLevConfig(std::span<const std::uint8_t>(buf), cfg));
    CHECK_EQ(int(cfg.watercR), 50);
    CHECK_EQ(int(cfg.watercG), 50);
    CHECK_EQ(int(cfg.watercB), 255);
    CHECK_EQ(int(cfg.ambient), 0);
    CHECK_EQ(int(cfg.parallaxAt), 4);
    CHECK(!cfg.disableRun);
}

void test_buffer_too_short_rejects() {
    std::vector<std::uint8_t> buf(0x122u, 0u);  // pre-config region only
    tou2d::LevConfig cfg{};
    CHECK(!tou2d::parseLevConfig(std::span<const std::uint8_t>(buf), cfg));
    // Default-constructed cfg unchanged on failure.
    CHECK(!cfg.para);
    CHECK_EQ(int(cfg.civil), 10);
}

void test_theme_string_is_null_terminated() {
    // The 16-byte theme field is read until either NUL or 16 bytes.
    // Verify a sub-16-byte name doesn't pick up trailing zeros.
    auto buf = makeSyntheticLev(
        true, 10, 5, 0, 0, 0, false,
        10, 10, 10, 10, 0, 2, false,
        "metro",                          // 5 chars
        true, 20, 20, 20, 54321);
    tou2d::LevConfig cfg{};
    CHECK(tou2d::parseLevConfig(std::span<const std::uint8_t>(buf), cfg));
    CHECK_EQ(cfg.ggTheme.size(), std::size_t(5));
    CHECK_EQ(cfg.ggTheme, std::string("metro"));
}

void test_theme_string_full_16_no_terminator() {
    // Exactly-16-char theme with no trailing NUL: parser caps at 16.
    auto buf = makeSyntheticLev(
        true, 10, 5, 0, 0, 0, false,
        10, 10, 10, 10, 0, 2, false,
        "abcdefghijklmnop",               // 16 chars, no NUL room
        true, 20, 20, 20, 54321);
    tou2d::LevConfig cfg{};
    CHECK(tou2d::parseLevConfig(std::span<const std::uint8_t>(buf), cfg));
    CHECK_EQ(cfg.ggTheme.size(), std::size_t(16));
    CHECK_EQ(cfg.ggTheme, std::string("abcdefghijklmnop"));
}

void test_random_seed_endian() {
    // randomseed is stored u16 little-endian at 0x1B6..0x1B8.
    // Use a value where the two bytes are distinguishable: 0x1234.
    auto buf = makeSyntheticLev(
        true, 10, 5, 0, 0, 0, false,
        10, 10, 10, 10, 0, 2, false,
        "x", true, 20, 20, 20, 0x1234);
    tou2d::LevConfig cfg{};
    CHECK(tou2d::parseLevConfig(std::span<const std::uint8_t>(buf), cfg));
    CHECK_EQ(int(cfg.randomSeed), 0x1234);
    // Bytes in the buffer match LE order.
    CHECK_EQ(int(buf[0x1B6]), 0x34);
    CHECK_EQ(int(buf[0x1B7]), 0x12);
}

void test_offsets_are_canonical() {
    // Triple-check the documented constants match TOU_RE.md.
    CHECK_EQ(tou2d::kLevConfigStart,      std::size_t(0x122));
    CHECK_EQ(tou2d::kLevConfigThemeStart, std::size_t(0x130));
    CHECK_EQ(tou2d::kLevConfigTailStart,  std::size_t(0x1B0));
    CHECK_EQ(tou2d::kLevConfigMinFileBytes, std::size_t(0x1B8));
}

} // namespace

int main() {
    test_offsets_are_canonical();
    test_jungle_shipped_bytes();
    test_woods_shipped_bytes();
    test_buffer_too_short_rejects();
    test_theme_string_is_null_terminated();
    test_theme_string_full_16_no_terminator();
    test_random_seed_endian();
    EXIT_WITH_RESULT();
}
