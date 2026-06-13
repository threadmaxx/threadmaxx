/// @file test_migration_remap_default_on_missing.cpp
/// @brief M4 — applyDefaultOnMissing inserts a default when the
/// field is absent; defaultIfEmpty fills present-but-empty values.

#include "Check.hpp"

#include <threadmaxx_migration/transform.hpp>

int main() {
    using namespace threadmaxx::migration;

    // Record missing the "layerMask" field gets 0xFFFFFFFF appended.
    Record rec{};
    rec.typeName = "Collider";
    rec.fields.push_back({"radius", FieldValue{{std::byte{0x01}}}});

    std::vector<std::byte> defBytes{
        std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}};
    CHECK(applyDefaultOnMissing("Collider", "layerMask", defBytes, rec));
    CHECK_EQ(rec.fields.size(), 2u);
    CHECK_EQ(rec.fields[1].name, std::string{"layerMask"});
    CHECK_EQ(rec.fields[1].value.bytes.size(), 4u);
    CHECK(rec.fields[1].value.bytes[0] == std::byte{0xFF});

    // Second application is a no-op (field already present).
    std::vector<std::byte> empty{};
    CHECK(!applyDefaultOnMissing("Collider", "layerMask", empty, rec));
    CHECK_EQ(rec.fields[1].value.bytes.size(), 4u);

    // Type mismatch: no-op.
    CHECK(!applyDefaultOnMissing("Other", "layerMask", defBytes, rec));

    // defaultIfEmpty (FieldRemap form): fills present-but-empty.
    Record empty2{};
    empty2.typeName = "Collider";
    empty2.fields.push_back({"layerMask", FieldValue{}});  // empty bytes
    auto remap = defaultIfEmpty("Collider", "layerMask", defBytes);
    CHECK(applyRemap(remap, empty2));
    CHECK_EQ(empty2.fields[0].value.bytes.size(), 4u);

    // defaultIfEmpty leaves a non-empty field alone.
    Record nonEmpty{};
    nonEmpty.typeName = "Collider";
    nonEmpty.fields.push_back({"layerMask",
                               FieldValue{{std::byte{0x01}}}});
    CHECK(applyRemap(remap, nonEmpty));
    CHECK_EQ(nonEmpty.fields[0].value.bytes.size(), 1u);
    CHECK(nonEmpty.fields[0].value.bytes[0] == std::byte{0x01});

    EXIT_WITH_RESULT();
}
