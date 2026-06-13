#pragma once

/// @file savefile.hpp
/// @brief `SaveMetadata` (header POD shipped at the front of every
/// save file) + `RecordSet` (metadata + records bundle), plus
/// `kSaveFileMagic`. M2 extends `SaveMetadata` with commitHash anchor
/// rules; M1 just carries the field through.

#include "records.hpp"
#include "version.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace threadmaxx::migration {

/// @brief Container magic: "TMSV" — Threadmaxx Migration Save Volume.
inline constexpr std::uint32_t kSaveFileMagic = 0x56534D54u;  // 'TMSV' LE

/// @brief Per-save metadata. Loaders must read this first to decide
/// whether they can handle the file at all (FormatVersion check) and
/// whether they need to migrate it (SchemaVersion check).
struct SaveMetadata {
    std::string   productName;
    std::string   buildId;
    SchemaVersion schemaVersion{};
    FormatVersion formatVersion{kCurrentFormatVersion};
    std::uint64_t worldSeed{0};
    std::uint64_t commitHash{0};
    std::string   createdUtc;
};

/// @brief Metadata + records bundle. The migration pipeline operates
/// on a whole RecordSet at a time.
struct RecordSet {
    SaveMetadata        metadata;
    std::vector<Record> records;
};

} // namespace threadmaxx::migration
