/// @file test_migration_report_summary.cpp
/// @brief M8 — summarize() rolls a MigrationResult into a
/// fixed-shape report (record count, warning count, applied steps).

#include "Check.hpp"

#include <threadmaxx_migration/pipeline.hpp>
#include <threadmaxx_migration/report.hpp>

int main() {
    using namespace threadmaxx::migration;

    MigrationRegistry reg;
    reg.registerType("Health", SchemaVersion{1, 0, 0});
    reg.addMigration("Health", SchemaVersion{1, 0, 0}, SchemaVersion{1, 1, 0},
                     [](Record&) {});
    MigrationPipeline pipeline{reg};

    RecordSet input{};
    for (std::uint64_t i = 0; i < 3; ++i) {
        Record r{};
        r.typeName = "Health";
        r.stableId = i;
        r.sourceVersion = SchemaVersion{1, 0, 0};
        input.records.push_back(r);
    }

    auto result = pipeline.migrate(input, SchemaVersion{1, 1, 0});
    CHECK(result.ok);
    CHECK_EQ(result.appliedSteps.size(), 3u);

    auto summary = summarize(result);
    CHECK(summary.ok);
    CHECK_EQ(summary.recordCount, 3u);
    CHECK_EQ(summary.warningCount, 0u);
    CHECK_EQ(summary.errorCount, 0u);
    CHECK_EQ(summary.appliedStepCount, 3u);

    // A second run with a deliberately-bad target produces an error.
    auto bad = pipeline.migrate(input, SchemaVersion{9, 9, 9});
    CHECK(!bad.ok);
    auto badSummary = summarize(bad);
    CHECK(!badSummary.ok);
    CHECK(badSummary.errorCount > 0u);

    EXIT_WITH_RESULT();
}
