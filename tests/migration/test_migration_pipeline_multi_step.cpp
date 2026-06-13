/// @file test_migration_pipeline_multi_step.cpp
/// @brief M5 — chains 1.0.0 → 1.1.0 → 1.2.0 → 2.0.0; migrateToLatest
/// applies all three in order.

#include "Check.hpp"

#include <threadmaxx_migration/pipeline.hpp>

#include <memory>
#include <string>
#include <vector>

int main() {
    using namespace threadmaxx::migration;

    auto sequence = std::make_shared<std::vector<std::string>>();

    MigrationRegistry reg;
    reg.registerType("Health", SchemaVersion{1, 0, 0});
    reg.addMigration("Health", SchemaVersion{1, 0, 0}, SchemaVersion{1, 1, 0},
                     [sequence](Record&) { sequence->push_back("1.0->1.1"); });
    reg.addMigration("Health", SchemaVersion{1, 1, 0}, SchemaVersion{1, 2, 0},
                     [sequence](Record&) { sequence->push_back("1.1->1.2"); });
    reg.addMigration("Health", SchemaVersion{1, 2, 0}, SchemaVersion{2, 0, 0},
                     [sequence](Record&) { sequence->push_back("1.2->2.0"); });

    MigrationPipeline pipeline{reg};

    RecordSet input{};
    input.metadata.schemaVersion = SchemaVersion{1, 0, 0};
    Record rec{};
    rec.typeName = "Health";
    rec.stableId = 7;
    rec.sourceVersion = SchemaVersion{1, 0, 0};
    input.records.push_back(rec);

    // migrateToLatest finds the highest reachable version (2.0.0).
    auto result = pipeline.migrateToLatest(input);
    CHECK(result.ok);
    CHECK_EQ(sequence->size(), 3u);
    CHECK_EQ((*sequence)[0], std::string{"1.0->1.1"});
    CHECK_EQ((*sequence)[1], std::string{"1.1->1.2"});
    CHECK_EQ((*sequence)[2], std::string{"1.2->2.0"});
    CHECK(result.output.records[0].sourceVersion == (SchemaVersion{2, 0, 0}));
    CHECK(result.output.metadata.schemaVersion == (SchemaVersion{2, 0, 0}));
    CHECK_EQ(result.appliedSteps.size(), 3u);

    // explicit target = 1.2.0 stops at the intermediate version.
    sequence->clear();
    auto partial = pipeline.migrate(input, SchemaVersion{1, 2, 0});
    CHECK(partial.ok);
    CHECK_EQ(sequence->size(), 2u);
    CHECK(partial.output.records[0].sourceVersion == (SchemaVersion{1, 2, 0}));

    EXIT_WITH_RESULT();
}
