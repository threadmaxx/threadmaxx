#pragma once

// M6.5 — persistent settings POD + on-disk wire format.
//
// `Settings` is the union of every option the Options menu surfaces.
// Six categories (Video / Audio / Controls / Gameplay / Accessibility /
// Benchmark) each carry a fixed-layout POD; KeyMap (the same shape
// `InputSystem` consumes) lives inline so a custom binding round-trips
// through one file alongside everything else.
//
// **Wire format** — host-endian binary, same caveats as `WorldSnapshot`:
//
//   [magic 'T2DS' u32][version u32]
//   [Video         12B]
//   [Audio          4B]
//   [Controls      kMaxHumans * kActionCount * sizeof(uint16_t) = 112B]
//   [Gameplay       8B]
//   [Accessibility  4B]
//   [Benchmark    128B]   // see BenchmarkSettings — fixed inline path
//
// Total payload = 268B; with the 8B header the file is 276B.
//
// Loader contract: missing file / magic mismatch / version mismatch /
// short read → `loadSettings` returns false; the caller's
// pre-populated defaults are left untouched. Atomic write via
// write-to-tmp + rename (POSIX rename is atomic on the same FS).
// Adding a new field bumps `kSettingsVersion`; older files load with
// the new field defaulted (the next save re-emits at the new version
// — see § "Still TBD" in TOU_PLAN.md M6.5 for the version-skew
// follow-up).

#include "DemoTypes.hpp"

#include <array>
#include <cstdint>
#include <filesystem>

namespace tou2d {

inline constexpr std::uint32_t kSettingsMagic   = 0x53443254u; // 'T2DS' LE
inline constexpr std::uint32_t kSettingsVersion = 1u;

/// Video category — resolution, fullscreen, vsync, UI scale. v1
/// "resolution" is treated as a Display-only row in the menu (no live
/// swapchain recreation); a future M6.5b can flip this to an actual
/// applied change at startup or on confirm.
struct VideoSettings {
    std::uint32_t resolutionW = 1280;
    std::uint32_t resolutionH = 720;
    std::uint8_t  fullscreen  = 0;     ///< 0 = windowed, 1 = fullscreen
    std::uint8_t  vsync       = 1;     ///< 0 = off, 1 = on
    std::uint8_t  uiScale     = 100;   ///< percent: 75 / 100 / 125 / 150
    std::uint8_t  _pad        = 0;
};
static_assert(sizeof(VideoSettings) == 12,
              "VideoSettings is part of the settings.dat wire shape — "
              "layout change requires bumping kSettingsVersion.");

/// Audio category — three independent 0..100 sliders. v1 applies
/// `master` via `ma_engine_set_volume`; `music` is a no-op until M5.x
/// ships a music driver; `sfx` will scale per-`AudioPlay` once the
/// AudioSystem grows a per-event scaler (see § "Still TBD" — the wire
/// shape is here so the eventual scaler doesn't need a version bump).
struct AudioSettings {
    std::uint8_t master = 80;
    std::uint8_t music  = 80;
    std::uint8_t sfx    = 80;
    std::uint8_t _pad   = 0;
};
static_assert(sizeof(AudioSettings) == 4,
              "AudioSettings layout pinned by settings.dat.");

/// Gameplay category — defaults that feed into the next `StartMatch`
/// (these are read by the host on apply, not edited live mid-match).
struct GameplaySettings {
    float         damageScale       = 1.0f;
    std::uint16_t respawnDelayTicks = 90;
    std::uint8_t  cameraMode        = 0;   ///< 0=split, 1=follow, 2=fixed
    std::uint8_t  _pad              = 0;
};
static_assert(sizeof(GameplaySettings) == 8,
              "GameplaySettings layout pinned by settings.dat.");

/// Accessibility category — four independent toggles / scales.
struct AccessibilitySettings {
    std::uint8_t hudScale       = 100;  ///< 50..200 in steps of 25
    std::uint8_t bigWarnings    = 0;
    std::uint8_t screenShake    = 1;
    std::uint8_t photosensitive = 0;    ///< caps explosion-flash alpha
};
static_assert(sizeof(AccessibilitySettings) == 4,
              "AccessibilitySettings layout pinned by settings.dat.");

/// Benchmark category — trace-sink + scripted-skip toggles + an inline
/// 124-byte JSONL output path buffer. Fixed-size to keep the POD
/// trivially copyable and the file layout stride-predictable; users
/// who need a longer path can edit the file by hand (the loader
/// truncates silently otherwise). M6.5's plan called for a
/// variable-length tail; deferred for v1 — see § "Still TBD".
struct BenchmarkSettings {
    std::uint8_t          traceSinkOn    = 0;
    std::uint8_t          scriptedSkipOn = 0;
    std::uint8_t          _pad[2]        = {};
    std::array<char, 124> jsonlPath      = {};
};
static_assert(sizeof(BenchmarkSettings) == 128,
              "BenchmarkSettings layout pinned by settings.dat.");

/// The full settings record. Default-constructed `Settings` matches the
/// "no settings.dat present" loader fallback — i.e. CLI-default
/// gameplay shape with audio at 80%, vsync on, etc.
struct Settings {
    VideoSettings         video{};
    AudioSettings         audio{};
    KeyMap                controls{};      ///< 112 B (kMaxHumans*kActionCount*2)
    GameplaySettings      gameplay{};
    AccessibilitySettings accessibility{};
    BenchmarkSettings     benchmark{};
};
static_assert(sizeof(Settings) ==
                  sizeof(VideoSettings) +
                  sizeof(AudioSettings) +
                  sizeof(KeyMap) +
                  sizeof(GameplaySettings) +
                  sizeof(AccessibilitySettings) +
                  sizeof(BenchmarkSettings),
              "Settings struct must have no internal padding — the wire "
              "format relies on a flat memcpy of each member in order.");
static_assert(sizeof(Settings) == 268,
              "Settings total payload size pinned by the M6.5 wire shape.");

/// Canonical settings.dat path. Honours `XDG_CONFIG_HOME` (POSIX);
/// falls back to `$HOME/.config/tou2d/settings.dat`. Returns the empty
/// path if neither env var is set (the caller treats that as "no
/// persistence available" — runs against defaults).
std::filesystem::path defaultSettingsPath();

/// Atomic write: serialise `s` to `<path>.tmp`, then `rename` over
/// `path`. Creates the parent directory if absent. Returns true on
/// success. Does NOT fsync — host crash mid-write may leave `<path>.tmp`
/// behind (the next `loadSettings` ignores it).
bool saveSettings(const std::filesystem::path& path, const Settings& s);

/// Load from `path` into `out`. Returns true iff the file exists, has
/// the right magic + version, and the full 268-byte payload was read.
/// On any failure returns false and `out` is left untouched — the
/// caller pre-populates `out` with `Settings{}` so the defaults stand.
bool loadSettings(const std::filesystem::path& path, Settings& out);

} // namespace tou2d
