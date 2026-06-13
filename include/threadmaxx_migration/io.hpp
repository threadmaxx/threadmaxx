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
#include <span>
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

} // namespace threadmaxx::migration
