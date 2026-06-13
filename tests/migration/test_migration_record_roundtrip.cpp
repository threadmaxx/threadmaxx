/// @file test_migration_record_roundtrip.cpp
/// @brief M1 — write a Record with 3 fields, read back, compare
/// field names + values byte-for-byte.

#include "Check.hpp"

#include <threadmaxx_migration/io.hpp>
#include <threadmaxx_migration/records.hpp>

#include <cstring>

namespace {

threadmaxx::migration::FieldValue makeField(std::initializer_list<unsigned char> bytes) {
    threadmaxx::migration::FieldValue v;
    for (auto b : bytes) v.bytes.push_back(static_cast<std::byte>(b));
    return v;
}

} // namespace

int main() {
    using namespace threadmaxx::migration;

    Record source{};
    source.typeName = "Health";
    source.stableId = 0xDEADBEEFCAFEBABEull;
    source.sourceVersion = SchemaVersion{1, 0, 0};
    source.fields.push_back({"current", makeField({0x10, 0x20, 0x30, 0x40})});
    source.fields.push_back({"max",     makeField({0x42, 0x43})});
    source.fields.push_back({"alive",   makeField({0x01})});

    RecordSet rs{};
    rs.metadata.productName = "test";
    rs.metadata.schemaVersion = SchemaVersion{1, 0, 0};
    rs.records.push_back(source);

    auto bytes = writeRecordSet(rs);
    CHECK(bytes.size() > 0u);

    RecordSet reloaded{};
    CHECK(readRecordSet(bytes, reloaded));
    CHECK_EQ(reloaded.records.size(), 1u);

    const auto& got = reloaded.records[0];
    CHECK_EQ(got.typeName, std::string{"Health"});
    CHECK_EQ(got.stableId, source.stableId);
    CHECK(got.sourceVersion == source.sourceVersion);
    CHECK_EQ(got.fields.size(), 3u);

    for (std::size_t i = 0; i < source.fields.size(); ++i) {
        CHECK_EQ(got.fields[i].name, source.fields[i].name);
        CHECK_EQ(got.fields[i].value.bytes.size(),
                 source.fields[i].value.bytes.size());
        if (!source.fields[i].value.bytes.empty()) {
            CHECK_EQ(std::memcmp(got.fields[i].value.bytes.data(),
                                 source.fields[i].value.bytes.data(),
                                 source.fields[i].value.bytes.size()), 0);
        }
    }

    // Field-with-empty-payload edge case.
    Record empty{};
    empty.typeName = "Empty";
    empty.stableId = 7;
    empty.fields.push_back({"flag", FieldValue{}});
    rs.records.clear();
    rs.records.push_back(empty);
    bytes = writeRecordSet(rs);
    CHECK(readRecordSet(bytes, reloaded));
    CHECK_EQ(reloaded.records.size(), 1u);
    CHECK_EQ(reloaded.records[0].fields[0].value.bytes.size(), 0u);

    EXIT_WITH_RESULT();
}
