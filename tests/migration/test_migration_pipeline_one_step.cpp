/// @file test_migration_pipeline_one_step.cpp
/// @brief M5 — single registered step migrates input from 1.0.0 to
/// 1.1.0; output records carry the new sourceVersion.

#include "Check.hpp"

#include <threadmaxx_migration/pipeline.hpp>
#include <threadmaxx_migration/rename.hpp>

int main() {
    using namespace threadmaxx::migration;

    MigrationRegistry reg;
    reg.registerType("Health", SchemaVersion{1, 0, 0});
    reg.addMigration("Health", SchemaVersion{1, 0, 0}, SchemaVersion{1, 1, 0},
                     [](Record& r) {
                         (void)applyRename("Health", "current", "hp", r);
                     });

    MigrationPipeline pipeline{reg};

    RecordSet input{};
    input.metadata.schemaVersion = SchemaVersion{1, 0, 0};
    Record rec{};
    rec.typeName = "Health";
    rec.stableId = 42;
    rec.sourceVersion = SchemaVersion{1, 0, 0};
    rec.fields.push_back({"current", FieldValue{{std::byte{0xAB}}}});
    input.records.push_back(rec);

    auto result = pipeline.migrate(input, SchemaVersion{1, 1, 0});
    CHECK(result.ok);
    CHECK_EQ(result.output.records.size(), 1u);
    CHECK(result.output.metadata.schemaVersion == (SchemaVersion{1, 1, 0}));
    CHECK(result.output.records[0].sourceVersion ==
          (SchemaVersion{1, 1, 0}));
    CHECK_EQ(result.output.records[0].fields[0].name, std::string{"hp"});
    CHECK_EQ(result.appliedSteps.size(), 1u);

    EXIT_WITH_RESULT();
}
