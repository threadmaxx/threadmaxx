#pragma once

/// @file version.hpp
/// @brief SchemaVersion (semver) + FormatVersion (container format
/// tag) + VersionRange. The migration library uses these to describe
/// at which version a save file was written, and at which version a
/// type was introduced / migrated to.
///
/// SchemaVersion is ordered by `(major, minor, patch)` lexicographic
/// comparison — the same precedence as Semantic Versioning 2.0.0.

#include <compare>
#include <cstdint>
#include <string_view>

namespace threadmaxx::migration {

/// @brief Library version stamp (`0.1.0` at M1 ship). Tracks the
/// `threadmaxx_migration` library's own release line, distinct from
/// `SchemaVersion` (which describes the save schema) and
/// `FormatVersion` (the container layout). Reaches `1.0.0` when the
/// M1..M8 batch sequence closes out.
inline constexpr std::uint32_t kLibraryVersionMajor = 0;
inline constexpr std::uint32_t kLibraryVersionMinor = 1;
inline constexpr std::uint32_t kLibraryVersionPatch = 0;
inline constexpr std::string_view kLibraryVersionString = "0.1.0";

/// @brief Semver-style schema version. Equality requires all three
/// components match; ordering is lexicographic.
struct SchemaVersion {
    std::uint32_t major{0};
    std::uint32_t minor{0};
    std::uint32_t patch{0};

    friend constexpr auto operator<=>(const SchemaVersion&,
                                      const SchemaVersion&) = default;
};

/// @brief Container format tag — monotonically-increasing integer
/// that identifies the binary container layout of a save file.
/// Bumping this is a hard break (older readers cannot parse).
/// SchemaVersion bumps are softer (migrations bridge gaps).
struct FormatVersion {
    std::uint32_t value{0};

    friend constexpr auto operator<=>(const FormatVersion&,
                                      const FormatVersion&) = default;
};

/// @brief A closed range of `SchemaVersion`. Used by validation and
/// the registry's compatibility queries.
struct VersionRange {
    SchemaVersion min{};
    SchemaVersion max{};
};

/// @brief Latest FormatVersion the library can read + write today.
inline constexpr FormatVersion kCurrentFormatVersion{1};

} // namespace threadmaxx::migration
