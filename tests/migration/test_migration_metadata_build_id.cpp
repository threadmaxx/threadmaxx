/// @file test_migration_metadata_build_id.cpp
/// @brief M2 — build IDs are case-sensitive verbatim strings.

#include "Check.hpp"

#include <threadmaxx_migration/io.hpp>

int main() {
    using namespace threadmaxx::migration;

    RecordSet rs{};
    rs.metadata.productName = "Demo";
    rs.metadata.buildId     = "BuiLd-AaBbCc-12345";  // mixed case + dashes
    auto bytes = writeRecordSet(rs);

    auto plain = loadRecordSet(bytes);
    CHECK(plain.ok);
    CHECK_EQ(plain.set.metadata.buildId, std::string{"BuiLd-AaBbCc-12345"});

    // Differing case is preserved (no normalisation).
    rs.metadata.buildId = "build-aabbcc-12345";
    auto bytes2 = writeRecordSet(rs);
    auto plain2 = loadRecordSet(bytes2);
    CHECK(plain2.ok);
    CHECK_EQ(plain2.set.metadata.buildId, std::string{"build-aabbcc-12345"});

    // Empty build ID is allowed.
    rs.metadata.buildId = "";
    auto bytes3 = writeRecordSet(rs);
    auto plain3 = loadRecordSet(bytes3);
    CHECK(plain3.ok);
    CHECK_EQ(plain3.set.metadata.buildId, std::string{""});

    // UTF-8 + non-ASCII (the wire is opaque bytes).
    rs.metadata.buildId = "build-ñ-✓";
    auto bytes4 = writeRecordSet(rs);
    auto plain4 = loadRecordSet(bytes4);
    CHECK(plain4.ok);
    CHECK_EQ(plain4.set.metadata.buildId, std::string{"build-ñ-✓"});

    EXIT_WITH_RESULT();
}
