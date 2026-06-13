/// @file RecordIo.cpp
/// @brief M1 — implementation of `readRecordSet` / `writeRecordSet`.
/// M2 — `loadRecordSet` adds compatibility checks + rich errors.

#include <threadmaxx_migration/io.hpp>

#include <threadmaxx_migration/detail/binary_reader.hpp>
#include <threadmaxx_migration/detail/binary_writer.hpp>

#include <cstdio>

namespace threadmaxx::migration {

namespace {

void writeSchemaVersion(detail::BinaryWriter& w, const SchemaVersion& v) {
    w.write(v.major);
    w.write(v.minor);
    w.write(v.patch);
}

bool readSchemaVersion(detail::BinaryReader& r, SchemaVersion& v) {
    if (!r.read(v.major)) return false;
    if (!r.read(v.minor)) return false;
    if (!r.read(v.patch)) return false;
    return true;
}

void writeMetadata(detail::BinaryWriter& w, const SaveMetadata& m) {
    w.writeString(m.productName);
    w.writeString(m.buildId);
    writeSchemaVersion(w, m.schemaVersion);
    w.write(m.worldSeed);
    w.write(m.commitHash);
    w.writeString(m.createdUtc);
}

bool readMetadata(detail::BinaryReader& r, SaveMetadata& m) {
    if (!r.readString(m.productName)) return false;
    if (!r.readString(m.buildId)) return false;
    if (!readSchemaVersion(r, m.schemaVersion)) return false;
    if (!r.read(m.worldSeed)) return false;
    if (!r.read(m.commitHash)) return false;
    if (!r.readString(m.createdUtc)) return false;
    return true;
}

void writeRecord(detail::BinaryWriter& w, const Record& rec) {
    w.write(rec.stableId);
    writeSchemaVersion(w, rec.sourceVersion);
    w.writeString(rec.typeName);
    w.write(static_cast<std::uint32_t>(rec.fields.size()));
    for (const auto& f : rec.fields) {
        w.writeString(f.name);
        w.write(static_cast<std::uint32_t>(f.value.bytes.size()));
        w.writeBytes(std::span<const std::byte>{f.value.bytes.data(),
                                                f.value.bytes.size()});
    }
}

bool readRecord(detail::BinaryReader& r, Record& rec) {
    if (!r.read(rec.stableId)) return false;
    if (!readSchemaVersion(r, rec.sourceVersion)) return false;
    if (!r.readString(rec.typeName)) return false;
    std::uint32_t fieldCount{};
    if (!r.read(fieldCount)) return false;
    rec.fields.resize(fieldCount);
    for (std::uint32_t i = 0; i < fieldCount; ++i) {
        auto& field = rec.fields[i];
        if (!r.readString(field.name)) return false;
        std::uint32_t valueLen{};
        if (!r.read(valueLen)) return false;
        if (!r.readBytes(field.value.bytes, valueLen)) return false;
    }
    return true;
}

} // namespace

std::vector<std::byte> writeRecordSet(const RecordSet& set) {
    std::vector<std::byte> out;
    detail::BinaryWriter w{out};
    w.write(kSaveFileMagic);
    w.write(set.metadata.formatVersion.value);
    writeMetadata(w, set.metadata);
    w.write(static_cast<std::uint64_t>(set.records.size()));
    for (const auto& rec : set.records) {
        writeRecord(w, rec);
    }
    return out;
}

bool readRecordSet(std::span<const std::byte> bytes, RecordSet& out) {
    detail::BinaryReader r{bytes};
    std::uint32_t magic{};
    if (!r.read(magic)) return false;
    if (magic != kSaveFileMagic) return false;
    std::uint32_t formatValue{};
    if (!r.read(formatValue)) return false;
    if (formatValue == 0u || formatValue > kCurrentFormatVersion.value) {
        return false;
    }
    out.metadata.formatVersion = FormatVersion{formatValue};
    if (!readMetadata(r, out.metadata)) return false;
    std::uint64_t recordCount{};
    if (!r.read(recordCount)) return false;
    out.records.resize(recordCount);
    for (std::uint64_t i = 0; i < recordCount; ++i) {
        if (!readRecord(r, out.records[i])) return false;
    }
    return r.ok();
}

// -------------------------------------------------------------------------
// M2 — rich-error loader with compatibility checks.

namespace {

std::string formatSchemaVersion(const SchemaVersion& v) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%u.%u.%u", v.major, v.minor, v.patch);
    return buf;
}

LoadResult parseWithErrors(std::span<const std::byte> bytes,
                           const CompatibilityRules& rules) {
    LoadResult result{};

    detail::BinaryReader r{bytes};
    std::uint32_t magic{};
    if (!r.read(magic)) {
        result.error = "truncated container: missing magic";
        return result;
    }
    if (magic != kSaveFileMagic) {
        char buf[80];
        std::snprintf(buf, sizeof(buf),
                      "bad magic: expected 0x%08X, got 0x%08X",
                      kSaveFileMagic, magic);
        result.error = buf;
        return result;
    }
    std::uint32_t formatValue{};
    if (!r.read(formatValue)) {
        result.error = "truncated container: missing FormatVersion";
        return result;
    }
    if (formatValue == 0u || formatValue > kCurrentFormatVersion.value) {
        char buf[120];
        std::snprintf(buf, sizeof(buf),
                      "unknown FormatVersion: observed %u, supported 1..%u",
                      formatValue, kCurrentFormatVersion.value);
        result.error = buf;
        return result;
    }
    result.set.metadata.formatVersion = FormatVersion{formatValue};
    if (!readMetadata(r, result.set.metadata)) {
        result.error = "truncated container: bad SaveMetadata";
        return result;
    }
    std::uint64_t recordCount{};
    if (!r.read(recordCount)) {
        result.error = "truncated container: missing recordCount";
        return result;
    }
    result.set.records.resize(recordCount);
    for (std::uint64_t i = 0; i < recordCount; ++i) {
        if (!readRecord(r, result.set.records[i])) {
            char buf[80];
            std::snprintf(buf, sizeof(buf),
                          "truncated container: bad record at index %llu",
                          static_cast<unsigned long long>(i));
            result.error = buf;
            return result;
        }
    }
    if (!r.ok()) {
        result.error = "truncated container: trailing read failed";
        return result;
    }

    // Compatibility checks.
    if (rules.requiredCommitHash.has_value() &&
        *rules.requiredCommitHash != result.set.metadata.commitHash) {
        char buf[160];
        std::snprintf(buf, sizeof(buf),
                      "commitHash mismatch: expected 0x%016llx, got 0x%016llx",
                      static_cast<unsigned long long>(*rules.requiredCommitHash),
                      static_cast<unsigned long long>(
                          result.set.metadata.commitHash));
        result.error = buf;
        return result;
    }
    if (rules.minSchemaVersion.has_value() &&
        result.set.metadata.schemaVersion < *rules.minSchemaVersion) {
        result.error =
            "schemaVersion below minimum: observed " +
            formatSchemaVersion(result.set.metadata.schemaVersion) +
            ", minimum " +
            formatSchemaVersion(*rules.minSchemaVersion);
        return result;
    }
    if (rules.maxSchemaVersion.has_value() &&
        *rules.maxSchemaVersion < result.set.metadata.schemaVersion) {
        result.error =
            "schemaVersion above maximum: observed " +
            formatSchemaVersion(result.set.metadata.schemaVersion) +
            ", maximum " +
            formatSchemaVersion(*rules.maxSchemaVersion);
        return result;
    }

    result.ok = true;
    return result;
}

} // namespace

LoadResult loadRecordSet(std::span<const std::byte> bytes,
                         const CompatibilityRules& rules) {
    return parseWithErrors(bytes, rules);
}

} // namespace threadmaxx::migration
