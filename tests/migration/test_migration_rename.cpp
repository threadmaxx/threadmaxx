/// @file test_migration_rename.cpp
/// @brief M4 — FieldRename leaves bytes intact, renames matching
/// fields; mismatched typeName is a no-op.

#include "Check.hpp"

#include <threadmaxx_migration/rename.hpp>

namespace {

threadmaxx::migration::FieldValue mkBytes(std::initializer_list<unsigned char> bs) {
    threadmaxx::migration::FieldValue v;
    for (auto b : bs) v.bytes.push_back(static_cast<std::byte>(b));
    return v;
}

} // namespace

int main() {
    using namespace threadmaxx::migration;

    Record rec{};
    rec.typeName = "Health";
    rec.fields.push_back({"current", mkBytes({0x10, 0x20, 0x30, 0x40})});
    rec.fields.push_back({"max",     mkBytes({0x42, 0x43})});

    // Rename "current" → "hp"; the bytes stay byte-for-byte.
    CHECK_EQ(applyRename(FieldRename{"Health", "current", "hp"}, rec), 1u);
    CHECK_EQ(rec.fields[0].name, std::string{"hp"});
    CHECK_EQ(rec.fields[0].value.bytes.size(), 4u);
    CHECK(rec.fields[0].value.bytes[0] == std::byte{0x10});
    CHECK(rec.fields[0].value.bytes[3] == std::byte{0x40});

    // Other field untouched.
    CHECK_EQ(rec.fields[1].name, std::string{"max"});

    // Type mismatch: no-op.
    CHECK_EQ(applyRename(FieldRename{"Other", "hp", "x"}, rec), 0u);
    CHECK_EQ(rec.fields[0].name, std::string{"hp"});

    // Missing source field: no-op.
    CHECK_EQ(applyRename(FieldRename{"Health", "doesnotexist", "x"}, rec), 0u);

    // from == to: refused.
    CHECK_EQ(applyRename(FieldRename{"Health", "hp", "hp"}, rec), 0u);

    // Convenience overload.
    CHECK_EQ(applyRename("Health", "max", "maxHp", rec), 1u);
    CHECK_EQ(rec.fields[1].name, std::string{"maxHp"});

    // Multiple matches all rename.
    Record dup{};
    dup.typeName = "X";
    dup.fields.push_back({"a", mkBytes({0x01})});
    dup.fields.push_back({"a", mkBytes({0x02})});
    CHECK_EQ(applyRename("X", "a", "b", dup), 2u);
    CHECK_EQ(dup.fields[0].name, std::string{"b"});
    CHECK_EQ(dup.fields[1].name, std::string{"b"});

    EXIT_WITH_RESULT();
}
