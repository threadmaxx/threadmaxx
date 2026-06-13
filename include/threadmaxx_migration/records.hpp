#pragma once

/// @file records.hpp
/// @brief Generic record model. The migration library never sees
/// game-side types directly — every operation rewrites this neutral
/// `RecordSet` shape. Component-codec bridges (M7) marshal between
/// real types and this representation.

#include "version.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace threadmaxx::migration {

/// @brief Opaque byte payload for one field. The codec layer
/// interprets `bytes` according to the field name + type, but the
/// migration helpers (FieldRename / FieldRemap) treat it as opaque.
struct FieldValue {
    std::vector<std::byte> bytes;
};

/// @brief One named field on a `Record`.
struct RecordField {
    std::string name;
    FieldValue  value;
};

/// @brief A single migrating object — usually one entity, but the
/// record model is general (could be a singleton, a sub-resource, a
/// chunked piece of world state, etc.).
struct Record {
    std::string              typeName;
    std::uint64_t            stableId{0};
    SchemaVersion            sourceVersion{};
    std::vector<RecordField> fields;
};

} // namespace threadmaxx::migration
