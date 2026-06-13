/// @file test_migration_registry_register.cpp
/// @brief M3 — registerType + knowsType + introducedAt + typeCount.

#include "Check.hpp"

#include <threadmaxx_migration/registry.hpp>

int main() {
    using namespace threadmaxx::migration;

    MigrationRegistry reg;
    CHECK_EQ(reg.typeCount(), 0u);
    CHECK(!reg.knowsType("Health"));

    reg.registerType("Health", SchemaVersion{1, 0, 0});
    CHECK(reg.knowsType("Health"));
    CHECK_EQ(reg.typeCount(), 1u);
    CHECK(reg.introducedAt("Health") == (SchemaVersion{1, 0, 0}));

    reg.registerType("Faction", SchemaVersion{1, 1, 0});
    CHECK_EQ(reg.typeCount(), 2u);
    CHECK(reg.introducedAt("Faction") == (SchemaVersion{1, 1, 0}));

    // Re-registering replaces the introduction version.
    reg.registerType("Health", SchemaVersion{2, 0, 0});
    CHECK_EQ(reg.typeCount(), 2u);
    CHECK(reg.introducedAt("Health") == (SchemaVersion{2, 0, 0}));

    // Unknown lookup.
    CHECK(!reg.knowsType("Unknown"));
    CHECK(reg.introducedAt("Unknown") == SchemaVersion{});

    EXIT_WITH_RESULT();
}
