/// @file test_migration_remap_widen.cpp
/// @brief M4 — widenU16ToU32 helper widens a 2-byte field to 4 bytes.

#include "Check.hpp"

#include <threadmaxx_migration/transform.hpp>

#include <cstdint>
#include <cstring>

int main() {
    using namespace threadmaxx::migration;

    Record rec{};
    rec.typeName = "Health";
    FieldValue v{};
    v.bytes = {std::byte{0x34}, std::byte{0x12}};  // 0x1234 LE
    rec.fields.push_back({"current", std::move(v)});

    const auto widen = widenU16ToU32("Health", "current");
    CHECK(applyRemap(widen, rec));
    CHECK_EQ(rec.fields[0].value.bytes.size(), 4u);

    std::uint32_t wide{};
    std::memcpy(&wide, rec.fields[0].value.bytes.data(), 4);
    CHECK_EQ(wide, 0x1234u);

    // Shape mismatch: bytes not 2 wide → no-op (still applies but
    // transform leaves the value alone).
    Record other{};
    other.typeName = "Health";
    other.fields.push_back({"current",
                            FieldValue{{std::byte{0xAA}, std::byte{0xBB}, std::byte{0xCC}}}});
    CHECK(applyRemap(widen, other));
    CHECK_EQ(other.fields[0].value.bytes.size(), 3u);  // unchanged

    // typeName mismatch returns false.
    Record diff{};
    diff.typeName = "Other";
    diff.fields.push_back({"current", FieldValue{{std::byte{0x11}, std::byte{0x22}}}});
    CHECK(!applyRemap(widen, diff));
    CHECK_EQ(diff.fields[0].value.bytes.size(), 2u);

    // Field missing returns false.
    Record empty{};
    empty.typeName = "Health";
    CHECK(!applyRemap(widen, empty));

    EXIT_WITH_RESULT();
}
