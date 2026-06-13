/// @file test_migration_validation_orphaned_aliases.cpp
/// @brief M8 — findOrphanedAliases reports alias names whose chain
/// ends in nothing registered.

#include "Check.hpp"

#include <threadmaxx_migration/validation.hpp>

int main() {
    using namespace threadmaxx::migration;

    MigrationRegistry reg;
    reg.registerType("Health", SchemaVersion{1, 0, 0});
    reg.aliasType("HP", "Health");
    reg.aliasType("HitPoints", "HP");  // chain ends at registered "Health"

    // Build a list with one good alias chain + a bare unregistered name
    // (which we treat as an alias for validation purposes).
    std::vector<std::string> names{
        "HP", "HitPoints", "Bogus", "Unknown"};
    auto orphans = findOrphanedAliases(reg, names);

    // HP + HitPoints resolve to "Health" (a real type) → not orphaned.
    // "Bogus" / "Unknown" never resolve to a registered type → orphan.
    CHECK_EQ(orphans.size(), 2u);
    bool sawBogus = false, sawUnknown = false;
    for (const auto& n : orphans) {
        if (n == "Bogus") sawBogus = true;
        if (n == "Unknown") sawUnknown = true;
    }
    CHECK(sawBogus);
    CHECK(sawUnknown);

    EXIT_WITH_RESULT();
}
