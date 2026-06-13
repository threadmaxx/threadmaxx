/// @file test_migration_version_compare.cpp
/// @brief M1 — SchemaVersion ordering follows semver semantics.

#include "Check.hpp"

#include <threadmaxx_migration/version.hpp>

int main() {
    using namespace threadmaxx::migration;

    SchemaVersion v100{1, 0, 0};
    SchemaVersion v110{1, 1, 0};
    SchemaVersion v200{2, 0, 0};
    SchemaVersion v200_alt{2, 0, 0};
    SchemaVersion v101{1, 0, 1};

    // Equality.
    CHECK(v200 == v200_alt);
    CHECK(!(v100 == v110));

    // Strict ordering (semver lexicographic).
    CHECK(v100 < v110);
    CHECK(v110 < v200);
    CHECK(v100 < v200);
    CHECK(v100 < v101);
    CHECK(v101 < v110);
    CHECK(!(v200 < v100));
    CHECK(!(v110 < v100));

    // <= and >=.
    CHECK(v100 <= v100);
    CHECK(v100 <= v110);
    CHECK(v200 >= v100);
    CHECK(v200 >= v200_alt);

    // FormatVersion comparison.
    FormatVersion f1{1};
    FormatVersion f2{2};
    CHECK(f1 < f2);
    CHECK(f1 == FormatVersion{1});

    // Current format.
    CHECK_EQ(kCurrentFormatVersion.value, 1u);

    // Library version stamp.
    CHECK_EQ(kLibraryVersionMajor, 0u);
    CHECK_EQ(kLibraryVersionMinor, 1u);
    CHECK_EQ(kLibraryVersionPatch, 0u);

    EXIT_WITH_RESULT();
}
