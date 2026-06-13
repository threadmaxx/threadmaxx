/// @file test_migration_metadata_commit_hash.cpp
/// @brief M2 — SaveMetadata.commitHash round-trips; the loader can
/// reject saves whose commitHash conflicts with the host's rule.

#include "Check.hpp"

#include <threadmaxx_migration/io.hpp>

int main() {
    using namespace threadmaxx::migration;

    RecordSet rs{};
    rs.metadata.productName  = "Demo";
    rs.metadata.schemaVersion = SchemaVersion{1, 2, 3};
    rs.metadata.commitHash   = 0xCAFEBABE12345678ull;
    auto bytes = writeRecordSet(rs);

    // No rules → loads fine, commitHash round-trips intact.
    auto plain = loadRecordSet(bytes);
    CHECK(plain.ok);
    CHECK(plain.error.empty());
    CHECK_EQ(plain.set.metadata.commitHash, 0xCAFEBABE12345678ull);

    // Matching rule → loads fine.
    CompatibilityRules matchRule{};
    matchRule.requiredCommitHash = 0xCAFEBABE12345678ull;
    auto matched = loadRecordSet(bytes, matchRule);
    CHECK(matched.ok);
    CHECK(matched.error.empty());

    // Mismatched rule → fails; error mentions both values.
    CompatibilityRules wrongRule{};
    wrongRule.requiredCommitHash = 0xDEADBEEFull;
    auto mismatched = loadRecordSet(bytes, wrongRule);
    CHECK(!mismatched.ok);
    CHECK(!mismatched.error.empty());
    CHECK(mismatched.error.find("commitHash") != std::string::npos);

    EXIT_WITH_RESULT();
}
