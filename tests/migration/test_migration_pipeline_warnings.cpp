/// @file test_migration_pipeline_warnings.cpp
/// @brief M5 — a step that tags itself lossy emits a warning when
/// allowLossy=true, or fails the run when allowLossy=false.

#include "Check.hpp"

#include <threadmaxx_migration/pipeline.hpp>

int main() {
    using namespace threadmaxx::migration;

    MigrationRegistry reg;
    reg.registerType("Stats", SchemaVersion{1, 0, 0});
    reg.addMigration("Stats", SchemaVersion{1, 0, 0}, SchemaVersion{1, 1, 0},
                     [](Record& r) {
                         // Tag lossy via the sentinel field.
                         RecordField f{};
                         f.name = kLossyMarkerField;
                         r.fields.push_back(f);
                     });

    MigrationPipeline pipeline{reg};

    RecordSet input{};
    Record rec{};
    rec.typeName = "Stats";
    rec.stableId = 5;
    rec.sourceVersion = SchemaVersion{1, 0, 0};
    input.records.push_back(rec);

    // allowLossy = false (default): the run fails.
    {
        auto result = pipeline.migrate(input, SchemaVersion{1, 1, 0});
        CHECK(!result.ok);
        CHECK(!result.errors.empty());
        CHECK(result.errors[0].find("lossy") != std::string::npos);
    }

    // allowLossy = true: ok=true, but the warning is recorded.
    {
        MigrationOptions opts{};
        opts.allowLossy = true;
        auto result = pipeline.migrate(input, SchemaVersion{1, 1, 0}, opts);
        CHECK(result.ok);
        CHECK_EQ(result.warnings.size(), 1u);
        CHECK(result.warnings[0].find("lossy") != std::string::npos);
        // The sentinel field is stripped before publish.
        for (const auto& f : result.output.records[0].fields) {
            CHECK(f.name != kLossyMarkerField);
        }
    }

    EXIT_WITH_RESULT();
}
