/// @file test_migration_recordset_metadata.cpp
/// @brief M1 — RecordSet metadata round-trips intact.

#include "Check.hpp"

#include <threadmaxx_migration/io.hpp>

int main() {
    using namespace threadmaxx::migration;

    RecordSet rs{};
    rs.metadata.productName = "ThreadmaxxDemo";
    rs.metadata.buildId     = "build-abcdef12";
    rs.metadata.schemaVersion = SchemaVersion{2, 3, 5};
    rs.metadata.formatVersion = kCurrentFormatVersion;
    rs.metadata.worldSeed   = 0xFEEDFACE12345678ull;
    rs.metadata.commitHash  = 0x0102030405060708ull;
    rs.metadata.createdUtc  = "2026-06-01T12:34:56Z";

    auto bytes = writeRecordSet(rs);
    CHECK(bytes.size() > 0u);

    RecordSet reloaded{};
    CHECK(readRecordSet(bytes, reloaded));
    CHECK_EQ(reloaded.metadata.productName, rs.metadata.productName);
    CHECK_EQ(reloaded.metadata.buildId, rs.metadata.buildId);
    CHECK(reloaded.metadata.schemaVersion == rs.metadata.schemaVersion);
    CHECK(reloaded.metadata.formatVersion == rs.metadata.formatVersion);
    CHECK_EQ(reloaded.metadata.worldSeed, rs.metadata.worldSeed);
    CHECK_EQ(reloaded.metadata.commitHash, rs.metadata.commitHash);
    CHECK_EQ(reloaded.metadata.createdUtc, rs.metadata.createdUtc);

    // Empty-records set still round-trips.
    CHECK_EQ(reloaded.records.size(), 0u);

    // Case-sensitive build IDs.
    rs.metadata.buildId = "Build-AbcDef12";
    auto bytes2 = writeRecordSet(rs);
    RecordSet reloaded2{};
    CHECK(readRecordSet(bytes2, reloaded2));
    CHECK_EQ(reloaded2.metadata.buildId, std::string{"Build-AbcDef12"});

    EXIT_WITH_RESULT();
}
