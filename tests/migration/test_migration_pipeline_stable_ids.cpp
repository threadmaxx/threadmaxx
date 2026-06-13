/// @file test_migration_pipeline_stable_ids.cpp
/// @brief M5 — stableIds survive the pipeline; a step that mutates
/// stableId is caught when preserveStableIds=true.

#include "Check.hpp"

#include <threadmaxx_migration/pipeline.hpp>

int main() {
    using namespace threadmaxx::migration;

    MigrationRegistry reg;
    reg.registerType("Entity", SchemaVersion{1, 0, 0});
    reg.addMigration("Entity", SchemaVersion{1, 0, 0}, SchemaVersion{1, 1, 0},
                     [](Record& r) {
                         // Innocuous transform — adds a field.
                         RecordField f{};
                         f.name = "added";
                         r.fields.push_back(std::move(f));
                     });

    MigrationPipeline pipeline{reg};

    // Multi-record run: every stableId preserved.
    RecordSet input{};
    for (std::uint64_t i = 1; i <= 4; ++i) {
        Record r{};
        r.typeName = "Entity";
        r.stableId = i * 100;
        r.sourceVersion = SchemaVersion{1, 0, 0};
        input.records.push_back(r);
    }
    auto result = pipeline.migrate(input, SchemaVersion{1, 1, 0});
    CHECK(result.ok);
    CHECK_EQ(result.output.records.size(), 4u);
    for (std::size_t i = 0; i < result.output.records.size(); ++i) {
        CHECK_EQ(result.output.records[i].stableId, (i + 1) * 100);
    }

    // A mischievous step that rewrites stableId is caught when
    // preserveStableIds = true (default).
    MigrationRegistry mischievous;
    mischievous.registerType("Entity", SchemaVersion{1, 0, 0});
    mischievous.addMigration("Entity",
                             SchemaVersion{1, 0, 0},
                             SchemaVersion{1, 1, 0},
                             [](Record& r) { r.stableId = 0; });
    MigrationPipeline mp{mischievous};
    RecordSet single{};
    Record one{};
    one.typeName = "Entity";
    one.stableId = 42;
    one.sourceVersion = SchemaVersion{1, 0, 0};
    single.records.push_back(one);
    auto mischiefResult = mp.migrate(single, SchemaVersion{1, 1, 0});
    CHECK(!mischiefResult.ok);
    CHECK(!mischiefResult.errors.empty());
    CHECK(mischiefResult.errors[0].find("stableId") != std::string::npos);

    // Opt out: preserveStableIds = false accepts the rewrite.
    MigrationOptions opts{};
    opts.preserveStableIds = false;
    auto relaxedResult = mp.migrate(single, SchemaVersion{1, 1, 0}, opts);
    CHECK(relaxedResult.ok);
    CHECK_EQ(relaxedResult.output.records[0].stableId, 0u);

    EXIT_WITH_RESULT();
}
