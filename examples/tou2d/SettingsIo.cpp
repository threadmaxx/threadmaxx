// M6.5 — settings.dat reader / writer.
//
// Wire layout matches the comment at the top of `Settings.hpp`. The
// member order in `Settings` is the on-disk order; we memcpy each
// member into a flat byte buffer rather than `fwrite(&s, sizeof(s))`
// so a future ABI hiccup that adds compiler-inserted padding to the
// outer `Settings` struct fails the static_assert in the header
// rather than silently corrupting the file format.

#include "Settings.hpp"

#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace tou2d {

namespace {

/// Append `n` bytes from `src` into `buf` at `offset`; advance `offset`.
void appendBytes_(std::array<std::byte, 276>& buf,
                  std::size_t&                offset,
                  const void*                 src,
                  std::size_t                 n) noexcept {
    std::memcpy(buf.data() + offset, src, n);
    offset += n;
}

/// Read `n` bytes from `buf[offset]` into `dst`; advance `offset`.
void readBytes_(const std::array<std::byte, 276>& buf,
                std::size_t&                      offset,
                void*                             dst,
                std::size_t                       n) noexcept {
    std::memcpy(dst, buf.data() + offset, n);
    offset += n;
}

constexpr std::size_t kFileSize = 276;
static_assert(kFileSize == 8 /*header*/ + sizeof(Settings),
              "settings.dat file size = 8B header + Settings payload.");

} // namespace

std::filesystem::path defaultSettingsPath() {
    namespace fs = std::filesystem;
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && xdg[0] != '\0') {
        return fs::path(xdg) / "tou2d" / "settings.dat";
    }
    if (const char* home = std::getenv("HOME"); home && home[0] != '\0') {
        return fs::path(home) / ".config" / "tou2d" / "settings.dat";
    }
    return {};
}

bool saveSettings(const std::filesystem::path& path, const Settings& s) {
    if (path.empty()) return false;

    // Ensure parent dir exists; ignore "already exists" / "EEXIST".
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    // create_directories returns false when the dir already existed
    // (not an error); we still proceed even if ec is set, because the
    // subsequent fopen will surface the real failure.

    // Marshal the full 276-byte file into a stack buffer.
    std::array<std::byte, kFileSize> buf{};
    std::size_t off = 0;
    appendBytes_(buf, off, &kSettingsMagic,   sizeof(kSettingsMagic));
    appendBytes_(buf, off, &kSettingsVersion, sizeof(kSettingsVersion));
    appendBytes_(buf, off, &s.video,          sizeof(VideoSettings));
    appendBytes_(buf, off, &s.audio,          sizeof(AudioSettings));
    appendBytes_(buf, off, &s.controls,       sizeof(KeyMap));
    appendBytes_(buf, off, &s.gameplay,       sizeof(GameplaySettings));
    appendBytes_(buf, off, &s.accessibility,  sizeof(AccessibilitySettings));
    appendBytes_(buf, off, &s.benchmark,      sizeof(BenchmarkSettings));
    if (off != kFileSize) return false;

    // Atomic write: <path>.tmp first, then rename.
    std::filesystem::path tmp = path;
    tmp += ".tmp";

    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(reinterpret_cast<const char*>(buf.data()),
                static_cast<std::streamsize>(buf.size()));
        if (!f) return false;
        f.close();
        if (!f.good()) return false;
    }

    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        // Cleanup: drop the tmp so a partial write doesn't linger.
        std::error_code rmEc;
        std::filesystem::remove(tmp, rmEc);
        return false;
    }
    return true;
}

bool loadSettings(const std::filesystem::path& path, Settings& out) {
    if (path.empty()) return false;

    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) return false;
    const auto sz = std::filesystem::file_size(path, ec);
    if (ec || sz != kFileSize) return false;

    std::ifstream f(path, std::ios::binary);
    if (!f) return false;

    std::array<std::byte, kFileSize> buf{};
    f.read(reinterpret_cast<char*>(buf.data()),
           static_cast<std::streamsize>(buf.size()));
    if (!f || static_cast<std::size_t>(f.gcount()) != kFileSize) return false;

    std::size_t off = 0;
    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    readBytes_(buf, off, &magic,   sizeof(magic));
    readBytes_(buf, off, &version, sizeof(version));
    if (magic != kSettingsMagic)     return false;
    if (version != kSettingsVersion) return false;

    Settings tmp{};
    readBytes_(buf, off, &tmp.video,         sizeof(VideoSettings));
    readBytes_(buf, off, &tmp.audio,         sizeof(AudioSettings));
    readBytes_(buf, off, &tmp.controls,      sizeof(KeyMap));
    readBytes_(buf, off, &tmp.gameplay,      sizeof(GameplaySettings));
    readBytes_(buf, off, &tmp.accessibility, sizeof(AccessibilitySettings));
    readBytes_(buf, off, &tmp.benchmark,     sizeof(BenchmarkSettings));
    if (off != kFileSize) return false;

    out = tmp;
    return true;
}

} // namespace tou2d
