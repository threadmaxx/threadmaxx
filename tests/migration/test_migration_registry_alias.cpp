/// @file test_migration_registry_alias.cpp
/// @brief M3 — aliasType + resolveAlias; cycle detection; aliases of
/// aliases resolve to the canonical name.

#include "Check.hpp"

#include <threadmaxx_migration/registry.hpp>

int main() {
    using namespace threadmaxx::migration;

    MigrationRegistry reg;
    reg.registerType("Health", SchemaVersion{1, 0, 0});

    // Aliasing onto an unknown target fails.
    CHECK(!reg.aliasType("HP", "DoesNotExist"));

    // Valid alias.
    CHECK(reg.aliasType("HP", "Health"));
    CHECK_EQ(reg.aliasCount(), 1u);
    CHECK(reg.knowsType("HP"));
    CHECK(reg.introducedAt("HP") == (SchemaVersion{1, 0, 0}));
    CHECK_EQ(reg.resolveAlias("HP"), std::string_view{"Health"});

    // Aliases of aliases.
    CHECK(reg.aliasType("HealthPoints", "HP"));
    CHECK(reg.knowsType("HealthPoints"));
    CHECK_EQ(reg.resolveAlias("HealthPoints"), std::string_view{"Health"});

    // Idempotent: re-declaring an existing alias succeeds.
    CHECK(reg.aliasType("HP", "Health"));
    CHECK_EQ(reg.aliasCount(), 2u);

    // Re-declaring with a different target fails.
    reg.registerType("Vitality", SchemaVersion{1, 0, 0});
    CHECK(!reg.aliasType("HP", "Vitality"));

    // Self-alias fails.
    CHECK(!reg.aliasType("HP", "HP"));

    // Cycle: HP→Health. Now trying Health→HP would create a cycle.
    CHECK(!reg.aliasType("Health", "HP"));

    // Aliasing a real registered type is also refused (can't shadow).
    CHECK(!reg.aliasType("Health", "Vitality"));

    // Empty names refused.
    CHECK(!reg.aliasType("", "Health"));
    CHECK(!reg.aliasType("Alias", ""));

    // Unknown lookup returns the input verbatim (no exception).
    CHECK_EQ(reg.resolveAlias("Unknown"), std::string_view{"Unknown"});

    EXIT_WITH_RESULT();
}
