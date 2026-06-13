/// @file test_migration_registry_hasPath.cpp
/// @brief M3 — addMigration + hasPath + findMigration + listSteps.

#include "Check.hpp"

#include <threadmaxx_migration/registry.hpp>

int main() {
    using namespace threadmaxx::migration;

    MigrationRegistry reg;
    reg.registerType("Health", SchemaVersion{1, 0, 0});

    // No steps registered yet → no path (unless from==to).
    CHECK(reg.hasPath("Health", SchemaVersion{1, 0, 0}, SchemaVersion{1, 0, 0}));
    CHECK(!reg.hasPath("Health", SchemaVersion{1, 0, 0}, SchemaVersion{2, 0, 0}));

    int applied = 0;
    reg.addMigration("Health", SchemaVersion{1, 0, 0}, SchemaVersion{1, 1, 0},
                     [&applied](Record&) { ++applied; });
    reg.addMigration("Health", SchemaVersion{1, 1, 0}, SchemaVersion{2, 0, 0},
                     [](Record&) {});

    // Single step.
    CHECK(reg.hasPath("Health", SchemaVersion{1, 0, 0}, SchemaVersion{1, 1, 0}));

    // Chained path: 1.0.0 → 1.1.0 → 2.0.0.
    CHECK(reg.hasPath("Health", SchemaVersion{1, 0, 0}, SchemaVersion{2, 0, 0}));

    // Missing intermediate step.
    CHECK(!reg.hasPath("Health", SchemaVersion{1, 0, 0}, SchemaVersion{3, 0, 0}));

    // Wrong direction (no reverse step).
    CHECK(!reg.hasPath("Health", SchemaVersion{2, 0, 0}, SchemaVersion{1, 0, 0}));

    // Unknown type.
    CHECK(!reg.hasPath("Unknown", SchemaVersion{1, 0, 0}, SchemaVersion{2, 0, 0}));

    // Resolves aliases.
    CHECK(reg.aliasType("HP", "Health"));
    CHECK(reg.hasPath("HP", SchemaVersion{1, 0, 0}, SchemaVersion{2, 0, 0}));

    // findMigration returns the exact step's body.
    const auto* step = reg.findMigration("Health",
                                         SchemaVersion{1, 0, 0},
                                         SchemaVersion{1, 1, 0});
    CHECK(step != nullptr);
    Record r{};
    (*step)(r);
    CHECK_EQ(applied, 1);

    // Alias lookup also finds it.
    const auto* aliasStep = reg.findMigration("HP",
                                              SchemaVersion{1, 0, 0},
                                              SchemaVersion{1, 1, 0});
    CHECK(aliasStep != nullptr);

    // Missing step → nullptr.
    CHECK(reg.findMigration("Health",
                            SchemaVersion{1, 0, 0},
                            SchemaVersion{3, 0, 0}) == nullptr);
    CHECK(reg.findMigration("Unknown",
                            SchemaVersion{1, 0, 0},
                            SchemaVersion{2, 0, 0}) == nullptr);

    // listSteps enumerates everything for a type.
    auto steps = reg.listSteps("Health");
    CHECK_EQ(steps.size(), 2u);
    auto aliasSteps = reg.listSteps("HP");
    CHECK_EQ(aliasSteps.size(), 2u);

    // Re-adding the same (from, to) overwrites the body.
    int newApplied = 0;
    reg.addMigration("Health", SchemaVersion{1, 0, 0}, SchemaVersion{1, 1, 0},
                     [&newApplied](Record&) { ++newApplied; });
    // listSteps shouldn't have grown.
    CHECK_EQ(reg.listSteps("Health").size(), 2u);
    auto* refreshed = reg.findMigration("Health",
                                        SchemaVersion{1, 0, 0},
                                        SchemaVersion{1, 1, 0});
    CHECK(refreshed != nullptr);
    Record r2{};
    (*refreshed)(r2);
    CHECK_EQ(newApplied, 1);

    // addMigration on an unknown type auto-registers it.
    reg.addMigration("Mana", SchemaVersion{1, 0, 0}, SchemaVersion{1, 1, 0},
                     [](Record&) {});
    CHECK(reg.knowsType("Mana"));
    CHECK(reg.introducedAt("Mana") == (SchemaVersion{1, 0, 0}));

    EXIT_WITH_RESULT();
}
