/// @file test_migration_metadata_reject_unknown_format.cpp
/// @brief M2 — unknown FormatVersion fails loadRecordSet; error
/// message includes the observed value.

#include "Check.hpp"

#include <threadmaxx_migration/io.hpp>

#include <cstring>

int main() {
    using namespace threadmaxx::migration;

    RecordSet rs{};
    rs.metadata.productName = "Demo";
    auto bytes = writeRecordSet(rs);
    CHECK(bytes.size() > 8u);

    // Stomp the FormatVersion u32 at byte offset 4 with an unknown value.
    auto altered = bytes;
    std::uint32_t observed = 42u;
    std::memcpy(altered.data() + 4, &observed, sizeof(observed));

    auto result = loadRecordSet(altered);
    CHECK(!result.ok);
    CHECK(!result.error.empty());
    CHECK(result.error.find("FormatVersion") != std::string::npos);
    CHECK(result.error.find("42") != std::string::npos);

    // FormatVersion=0 also rejected; error names "0".
    std::uint32_t zero = 0u;
    auto zeroed = bytes;
    std::memcpy(zeroed.data() + 4, &zero, sizeof(zero));
    auto zeroResult = loadRecordSet(zeroed);
    CHECK(!zeroResult.ok);
    CHECK(zeroResult.error.find("FormatVersion") != std::string::npos);

    // SchemaVersion below the host's minimum.
    rs.metadata.schemaVersion = SchemaVersion{1, 0, 0};
    auto goodBytes = writeRecordSet(rs);
    CompatibilityRules tooNew{};
    tooNew.minSchemaVersion = SchemaVersion{2, 0, 0};
    auto rangeResult = loadRecordSet(goodBytes, tooNew);
    CHECK(!rangeResult.ok);
    CHECK(rangeResult.error.find("schemaVersion") != std::string::npos);
    CHECK(rangeResult.error.find("1.0.0") != std::string::npos);
    CHECK(rangeResult.error.find("2.0.0") != std::string::npos);

    // SchemaVersion above the host's maximum.
    rs.metadata.schemaVersion = SchemaVersion{5, 0, 0};
    auto futureBytes = writeRecordSet(rs);
    CompatibilityRules tooOld{};
    tooOld.maxSchemaVersion = SchemaVersion{3, 9, 9};
    auto futureResult = loadRecordSet(futureBytes, tooOld);
    CHECK(!futureResult.ok);
    CHECK(futureResult.error.find("schemaVersion") != std::string::npos);
    CHECK(futureResult.error.find("5.0.0") != std::string::npos);
    CHECK(futureResult.error.find("3.9.9") != std::string::npos);

    EXIT_WITH_RESULT();
}
