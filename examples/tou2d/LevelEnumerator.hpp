#pragma once

// Header-only enumerator that scans `<assetsRoot>/levels/` for valid
// imported level dirs. A "valid" dir contains either an `attribute.tga`
// (the canonical loader input) OR a `visual.jpg` (so the loader's JPEG
// fallback path can derive an attribute map). Stem names are sorted
// alphabetically so two runs against the same disk produce the same
// enumerated order — important for the MatchSetup UI's index-based
// `importedLevelIdx` to mean the same thing across runs / replays.

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace tou2d {

inline constexpr std::size_t kMaxEnumeratedLevels = 64;

struct EnumeratedLevel {
    std::string           name;     // dir slug (e.g. "jungle")
    std::filesystem::path path;     // absolute path to the dir
};

/// Scan `<root>/levels/` for valid imported level dirs. Returns up to
/// `kMaxEnumeratedLevels` entries sorted ascending by `name`. Returns
/// an empty vector when `root/levels/` doesn't exist or contains no
/// valid dirs — that's the "fallback to synthetic/generator" signal
/// the MatchSetup screen will key off.
inline std::vector<EnumeratedLevel>
enumerateImportedLevels(const std::filesystem::path& root) {
    namespace fs = std::filesystem;
    std::vector<EnumeratedLevel> out;
    std::error_code ec;
    const fs::path levelsDir = root / "levels";
    if (!fs::exists(levelsDir, ec) || ec || !fs::is_directory(levelsDir, ec)) {
        return out;
    }
    for (const auto& entry : fs::directory_iterator(levelsDir, ec)) {
        if (ec) break;
        if (!entry.is_directory(ec) || ec) continue;
        const fs::path dir = entry.path();
        const bool hasTga = fs::exists(dir / "attribute.tga", ec) && !ec;
        ec.clear();
        const bool hasJpg = fs::exists(dir / "visual.jpg", ec) && !ec;
        ec.clear();
        if (!hasTga && !hasJpg) continue;
        out.push_back({dir.filename().string(), dir});
        if (out.size() >= kMaxEnumeratedLevels) break;
    }
    std::sort(out.begin(), out.end(),
              [](const EnumeratedLevel& a, const EnumeratedLevel& b) {
                  return a.name < b.name;
              });
    return out;
}

} // namespace tou2d
