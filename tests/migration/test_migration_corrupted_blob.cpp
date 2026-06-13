/// @file test_migration_corrupted_blob.cpp
/// @brief M1 — corrupted magic / truncated blobs fail loading.

#include "Check.hpp"

#include <threadmaxx_migration/io.hpp>

#include <cstring>
#include <vector>

int main() {
    using namespace threadmaxx::migration;

    RecordSet rs{};
    rs.metadata.productName = "Demo";
    Record r{};
    r.typeName = "X";
    r.stableId = 1;
    rs.records.push_back(r);
    auto bytes = writeRecordSet(rs);
    CHECK(bytes.size() > 4u);

    // Empty buffer fails.
    RecordSet out{};
    CHECK(!readRecordSet({}, out));

    // Wrong magic fails.
    auto wrongMagic = bytes;
    wrongMagic[0] = std::byte{0x42};
    CHECK(!readRecordSet(wrongMagic, out));

    // Truncated — drop last 4 bytes.
    auto truncated = bytes;
    if (truncated.size() >= 4) truncated.resize(truncated.size() - 4);
    CHECK(!readRecordSet(truncated, out));

    // Drop one byte off the end.
    auto truncated2 = bytes;
    truncated2.pop_back();
    CHECK(!readRecordSet(truncated2, out));

    // Unknown FormatVersion (e.g. 99) is rejected.
    auto wrongFormat = bytes;
    // Overwrite the formatVersion u32 at byte offset 4.
    std::uint32_t bogus = 99;
    std::memcpy(wrongFormat.data() + 4, &bogus, sizeof(bogus));
    CHECK(!readRecordSet(wrongFormat, out));

    // FormatVersion = 0 is also rejected.
    auto zeroFormat = bytes;
    std::uint32_t zero = 0;
    std::memcpy(zeroFormat.data() + 4, &zero, sizeof(zero));
    CHECK(!readRecordSet(zeroFormat, out));

    // Sanity: the original blob still loads.
    CHECK(readRecordSet(bytes, out));
    CHECK_EQ(out.records.size(), 1u);

    EXIT_WITH_RESULT();
}
