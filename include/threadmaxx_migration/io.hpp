#pragma once

/// @file io.hpp
/// @brief Free functions to serialize / deserialize `RecordSet` from
/// raw bytes. Host-endian, magic-prefixed; rejects truncated or
/// magic-mismatched blobs.
///
/// Container layout (host-endian, all integers little-endian-on-LE):
///
///   [u32 magic = kSaveFileMagic]
///   [u32 formatVersion]
///   [u32 productNameLen][bytes]
///   [u32 buildIdLen][bytes]
///   [u32 schemaMajor][u32 schemaMinor][u32 schemaPatch]
///   [u64 worldSeed]
///   [u64 commitHash]
///   [u32 createdUtcLen][bytes]
///   [u64 recordCount]
///   record*  := [u64 stableId]
///               [u32 sourceMajor][u32 sourceMinor][u32 sourcePatch]
///               [u32 typeNameLen][bytes]
///               [u32 fieldCount]
///               field*
///   field*  := [u32 nameLen][bytes][u32 valueLen][bytes]
///
/// `kCurrentFormatVersion` is the FormatVersion value the writer
/// stamps. The reader accepts any value ≤ kCurrentFormatVersion;
/// M2 strengthens this with a strict-version-known check.

#include "savefile.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace threadmaxx::migration {

/// @brief Serialize a `RecordSet` to a fresh byte vector.
[[nodiscard]] std::vector<std::byte>
writeRecordSet(const RecordSet& set);

/// @brief Deserialize a byte span. Returns `true` on success;
/// leaves `out` partially-populated on failure (caller should treat
/// it as opaque on failure).
[[nodiscard]] bool
readRecordSet(std::span<const std::byte> bytes, RecordSet& out);

// -------------------------------------------------------------------------
// M2 — load-time compatibility checks + rich-error variant.

/// @brief Optional rules a loader can enforce on top of the bare
/// container parse. Every field is optional — `nullopt` = "don't
/// check". The intended use is host code that knows its own
/// build's commitHash + supported schema window and wants to refuse
/// saves outside that window before the pipeline runs.
struct CompatibilityRules {
    /// @brief If set, the save's `commitHash` MUST equal this value.
    std::optional<std::uint64_t> requiredCommitHash;
    /// @brief If set, the save's `schemaVersion` MUST be ≥ this.
    std::optional<SchemaVersion> minSchemaVersion;
    /// @brief If set, the save's `schemaVersion` MUST be ≤ this.
    std::optional<SchemaVersion> maxSchemaVersion;
};

/// @brief Result of `loadRecordSet`. `ok` is true on a clean parse
/// AND a compatibility match. `error` is empty on success; on
/// failure it carries a human-readable diagnostic that names the
/// observed-vs-expected mismatch (the test gates the
/// "unknown FormatVersion" message includes the observed value).
struct LoadResult {
    bool        ok{false};
    std::string error;
    RecordSet   set;
};

/// @brief Parse @p bytes and apply @p rules. Always returns a
/// LoadResult; check `.ok` before reading `.set`.
[[nodiscard]] LoadResult
loadRecordSet(std::span<const std::byte> bytes,
              const CompatibilityRules& rules = {});

} // namespace threadmaxx::migration
