// tou2d_settings_io_test — M6.5 contract pin for the settings.dat
// reader / writer.
//
// Pins:
//   * POD layout — Settings = 268 B; each category's size pinned
//     individually so a future field addition has to bump
//     kSettingsVersion deliberately.
//   * Round-trip — `Settings -> saveSettings -> loadSettings -> Settings`
//     produces byte-identical content (including all KeyMap rows and
//     the inline jsonlPath buffer).
//   * Atomic write — after `saveSettings` the canonical file exists
//     AND the `.tmp` companion has been renamed away.
//   * Missing file → `loadSettings` returns false; `out` is left
//     untouched (caller's defaults stand).
//   * Magic mismatch → `loadSettings` returns false.
//   * Version mismatch → `loadSettings` returns false.
//   * Default constructor matches the loader's "use defaults" baseline
//     (audio 80/80/80, vsync on, etc.) — load-failure path keeps the
//     pre-call state intact, so this is the contract.
//   * `defaultSettingsPath()` honours $XDG_CONFIG_HOME, falls back to
//     $HOME/.config/tou2d, returns empty when neither is set.

#include "Check.hpp"

#include "../examples/tou2d/Settings.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

namespace {

std::filesystem::path uniqueTempDir() {
    namespace fs = std::filesystem;
    static std::mt19937_64 rng{std::random_device{}()};
    for (int attempt = 0; attempt < 32; ++attempt) {
        const auto p = fs::temp_directory_path() /
                       ("tou2d_settings_test_" +
                        std::to_string(rng()));
        std::error_code ec;
        if (fs::create_directories(p, ec)) return p;
    }
    return fs::temp_directory_path() / "tou2d_settings_test_fallback";
}

} // namespace

int main() {
    using tou2d::Settings;
    using tou2d::VideoSettings;
    using tou2d::AudioSettings;
    using tou2d::GameplaySettings;
    using tou2d::AccessibilitySettings;
    using tou2d::BenchmarkSettings;
    using tou2d::KeyMap;
    using tou2d::kSettingsMagic;
    using tou2d::kSettingsVersion;
    using tou2d::loadSettings;
    using tou2d::saveSettings;
    using tou2d::defaultSettingsPath;

    namespace fs = std::filesystem;

    // ---- 1. POD layout pins -----------------------------------------
    CHECK_EQ(sizeof(VideoSettings),         std::size_t{12});
    CHECK_EQ(sizeof(AudioSettings),         std::size_t{4});
    CHECK_EQ(sizeof(GameplaySettings),      std::size_t{8});
    CHECK_EQ(sizeof(AccessibilitySettings), std::size_t{4});
    CHECK_EQ(sizeof(BenchmarkSettings),     std::size_t{128});
    CHECK_EQ(sizeof(KeyMap),                std::size_t{112});
    CHECK_EQ(sizeof(Settings),              std::size_t{268});
    CHECK_EQ(kSettingsMagic, std::uint32_t{0x53443254u});
    CHECK_EQ(kSettingsVersion, std::uint32_t{1u});

    // ---- 2. Default constructor matches the loader-fallback baseline.
    {
        Settings s{};
        CHECK_EQ(s.video.resolutionW, std::uint32_t{1280});
        CHECK_EQ(s.video.resolutionH, std::uint32_t{720});
        CHECK_EQ(s.video.vsync,   std::uint8_t{1});
        CHECK_EQ(s.video.uiScale, std::uint8_t{100});
        CHECK_EQ(s.audio.master,  std::uint8_t{80});
        CHECK_EQ(s.audio.music,   std::uint8_t{80});
        CHECK_EQ(s.audio.sfx,     std::uint8_t{80});
        CHECK_EQ(s.gameplay.respawnDelayTicks, std::uint16_t{90});
        CHECK_EQ(s.accessibility.screenShake,  std::uint8_t{1});
    }

    // ---- 3. Missing file → false; out untouched ---------------------
    {
        const auto tmp = uniqueTempDir();
        const auto missing = tmp / "no_such_file.dat";
        Settings s{};
        s.audio.master = 42;
        const bool loaded = loadSettings(missing, s);
        CHECK(!loaded);
        // `out` was left untouched.
        CHECK_EQ(s.audio.master, std::uint8_t{42});
        fs::remove_all(tmp);
    }

    // ---- 4. Round-trip ----------------------------------------------
    {
        const auto tmp = uniqueTempDir();
        const auto path = tmp / "settings.dat";

        Settings written{};
        written.video.resolutionW = 1920;
        written.video.resolutionH = 1080;
        written.video.fullscreen  = 1;
        written.video.vsync       = 0;
        written.video.uiScale     = 125;
        written.audio.master      = 55;
        written.audio.music       = 60;
        written.audio.sfx         = 75;
        // Customise a couple of KeyMap rows.
        written.controls.binding[0][0] = 0xAA11;
        written.controls.binding[1][1] = 0xBB22;
        written.controls.binding[2][2] = 0xCC33;
        written.controls.binding[3][3] = 0xDD44;
        written.gameplay.damageScale       = 1.5f;
        written.gameplay.respawnDelayTicks = 180;
        written.gameplay.cameraMode        = 2;
        written.accessibility.hudScale       = 125;
        written.accessibility.bigWarnings    = 1;
        written.accessibility.screenShake    = 0;
        written.accessibility.photosensitive = 1;
        written.benchmark.traceSinkOn    = 1;
        written.benchmark.scriptedSkipOn = 1;
        std::strncpy(written.benchmark.jsonlPath.data(),
                     "/tmp/tou2d.jsonl",
                     written.benchmark.jsonlPath.size() - 1);

        CHECK(saveSettings(path, written));
        CHECK(fs::exists(path));
        // .tmp companion was renamed away.
        CHECK(!fs::exists(fs::path(path).concat(".tmp")));
        CHECK_EQ(fs::file_size(path), std::uint64_t{276});

        Settings read{};
        read.audio.master = 99;  // sentinel — must be overwritten
        CHECK(loadSettings(path, read));
        CHECK_EQ(read.video.resolutionW, std::uint32_t{1920});
        CHECK_EQ(read.video.resolutionH, std::uint32_t{1080});
        CHECK_EQ(read.video.fullscreen,  std::uint8_t{1});
        CHECK_EQ(read.video.vsync,       std::uint8_t{0});
        CHECK_EQ(read.video.uiScale,     std::uint8_t{125});
        CHECK_EQ(read.audio.master,      std::uint8_t{55});
        CHECK_EQ(read.audio.music,       std::uint8_t{60});
        CHECK_EQ(read.audio.sfx,         std::uint8_t{75});
        CHECK_EQ(read.controls.binding[0][0], std::uint16_t{0xAA11});
        CHECK_EQ(read.controls.binding[1][1], std::uint16_t{0xBB22});
        CHECK_EQ(read.controls.binding[2][2], std::uint16_t{0xCC33});
        CHECK_EQ(read.controls.binding[3][3], std::uint16_t{0xDD44});
        CHECK(read.gameplay.damageScale == 1.5f);
        CHECK_EQ(read.gameplay.respawnDelayTicks, std::uint16_t{180});
        CHECK_EQ(read.gameplay.cameraMode,        std::uint8_t{2});
        CHECK_EQ(read.accessibility.hudScale,       std::uint8_t{125});
        CHECK_EQ(read.accessibility.bigWarnings,    std::uint8_t{1});
        CHECK_EQ(read.accessibility.screenShake,    std::uint8_t{0});
        CHECK_EQ(read.accessibility.photosensitive, std::uint8_t{1});
        CHECK_EQ(read.benchmark.traceSinkOn,    std::uint8_t{1});
        CHECK_EQ(read.benchmark.scriptedSkipOn, std::uint8_t{1});
        CHECK_EQ(std::string{read.benchmark.jsonlPath.data()},
                 std::string{"/tmp/tou2d.jsonl"});

        fs::remove_all(tmp);
    }

    // ---- 5. Magic mismatch → false ---------------------------------
    {
        const auto tmp = uniqueTempDir();
        const auto path = tmp / "settings.dat";
        {
            std::ofstream f(path, std::ios::binary | std::ios::trunc);
            std::uint32_t badMagic = 0xDEADBEEF;
            std::uint32_t version  = kSettingsVersion;
            f.write(reinterpret_cast<const char*>(&badMagic), sizeof(badMagic));
            f.write(reinterpret_cast<const char*>(&version),  sizeof(version));
            std::array<std::byte, 268> payload{};
            f.write(reinterpret_cast<const char*>(payload.data()),
                    static_cast<std::streamsize>(payload.size()));
        }
        Settings s{};
        s.audio.master = 7;
        const bool loaded = loadSettings(path, s);
        CHECK(!loaded);
        CHECK_EQ(s.audio.master, std::uint8_t{7});  // untouched
        fs::remove_all(tmp);
    }

    // ---- 6. Version mismatch → false -------------------------------
    {
        const auto tmp = uniqueTempDir();
        const auto path = tmp / "settings.dat";
        {
            std::ofstream f(path, std::ios::binary | std::ios::trunc);
            std::uint32_t magic   = kSettingsMagic;
            std::uint32_t badVer  = kSettingsVersion + 1;
            f.write(reinterpret_cast<const char*>(&magic),  sizeof(magic));
            f.write(reinterpret_cast<const char*>(&badVer), sizeof(badVer));
            std::array<std::byte, 268> payload{};
            f.write(reinterpret_cast<const char*>(payload.data()),
                    static_cast<std::streamsize>(payload.size()));
        }
        Settings s{};
        s.audio.master = 7;
        const bool loaded = loadSettings(path, s);
        CHECK(!loaded);
        CHECK_EQ(s.audio.master, std::uint8_t{7});  // untouched
        fs::remove_all(tmp);
    }

    // ---- 7. Truncated payload → false ------------------------------
    {
        const auto tmp = uniqueTempDir();
        const auto path = tmp / "settings.dat";
        {
            std::ofstream f(path, std::ios::binary | std::ios::trunc);
            std::uint32_t magic = kSettingsMagic;
            std::uint32_t ver   = kSettingsVersion;
            f.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
            f.write(reinterpret_cast<const char*>(&ver),   sizeof(ver));
            // Only 16 bytes of payload instead of 268.
            std::array<std::byte, 16> payload{};
            f.write(reinterpret_cast<const char*>(payload.data()),
                    static_cast<std::streamsize>(payload.size()));
        }
        Settings s{};
        const bool loaded = loadSettings(path, s);
        CHECK(!loaded);
        fs::remove_all(tmp);
    }

    // ---- 8. defaultSettingsPath honours XDG_CONFIG_HOME -------------
    {
        // Stash the existing env state.
        const char* origXdg  = std::getenv("XDG_CONFIG_HOME");
        const char* origHome = std::getenv("HOME");
        const std::string saveXdg  = origXdg  ? origXdg  : "";
        const std::string saveHome = origHome ? origHome : "";

        // Both set → XDG wins.
        setenv("XDG_CONFIG_HOME", "/tmp/xdg", 1);
        setenv("HOME",            "/tmp/home", 1);
        CHECK_EQ(defaultSettingsPath().string(),
                 std::string{"/tmp/xdg/tou2d/settings.dat"});

        // Only HOME → HOME/.config fallback.
        unsetenv("XDG_CONFIG_HOME");
        CHECK_EQ(defaultSettingsPath().string(),
                 std::string{"/tmp/home/.config/tou2d/settings.dat"});

        // Neither → empty.
        unsetenv("HOME");
        CHECK(defaultSettingsPath().empty());

        // Restore.
        if (!saveXdg.empty())  setenv("XDG_CONFIG_HOME", saveXdg.c_str(), 1);
        if (!saveHome.empty()) setenv("HOME",            saveHome.c_str(), 1);
    }

    // ---- 9. saveSettings on empty path → false ---------------------
    {
        Settings s{};
        CHECK(!saveSettings({}, s));
    }

    EXIT_WITH_RESULT();
}
