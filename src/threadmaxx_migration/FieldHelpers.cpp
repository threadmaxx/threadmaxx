/// @file FieldHelpers.cpp
/// @brief M4 — FieldRename / FieldRemap + canned transform helpers.

#include <threadmaxx_migration/rename.hpp>
#include <threadmaxx_migration/transform.hpp>

#include <cstring>
#include <utility>

namespace threadmaxx::migration {

std::size_t applyRename(const FieldRename& rename, Record& rec) {
    if (rec.typeName != rename.typeName) return 0;
    if (rename.from == rename.to) return 0;
    std::size_t hits = 0;
    for (auto& field : rec.fields) {
        if (field.name == rename.from) {
            field.name = rename.to;
            ++hits;
        }
    }
    return hits;
}

bool applyRemap(const FieldRemap& remap, Record& rec) {
    if (rec.typeName != remap.typeName) return false;
    if (!remap.transform) return false;
    for (auto& field : rec.fields) {
        if (field.name == remap.field) {
            remap.transform(field.value);
            return true;
        }
    }
    return false;
}

bool applyRemap(const FieldRemapWithRecord& remap, Record& rec) {
    if (rec.typeName != remap.typeName) return false;
    if (!remap.transform) return false;
    remap.transform(rec);
    return true;
}

bool applyDefaultOnMissing(std::string_view typeName,
                           std::string_view fieldName,
                           std::vector<std::byte> defaultBytes,
                           Record& rec) {
    if (rec.typeName != typeName) return false;
    for (const auto& field : rec.fields) {
        if (field.name == fieldName) return false;  // already present
    }
    RecordField added{};
    added.name = std::string{fieldName};
    added.value.bytes = std::move(defaultBytes);
    rec.fields.push_back(std::move(added));
    return true;
}

FieldRemap defaultIfEmpty(std::string typeName,
                          std::string field,
                          std::vector<std::byte> defaultBytes) {
    FieldRemap out{};
    out.typeName  = std::move(typeName);
    out.field     = std::move(field);
    auto bytes    = std::move(defaultBytes);
    out.transform = [bytes](FieldValue& v) {
        if (v.bytes.empty()) v.bytes = bytes;
    };
    return out;
}

FieldRemap widenU16ToU32(std::string typeName, std::string field) {
    FieldRemap out{};
    out.typeName  = std::move(typeName);
    out.field     = std::move(field);
    out.transform = [](FieldValue& v) {
        if (v.bytes.size() != 2) return;  // no-op on shape mismatch
        std::uint16_t narrow{};
        std::memcpy(&narrow, v.bytes.data(), 2);
        const std::uint32_t wide = narrow;
        v.bytes.resize(4);
        std::memcpy(v.bytes.data(), &wide, 4);
    };
    return out;
}

} // namespace threadmaxx::migration
