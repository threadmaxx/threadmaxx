/// @file test_migration_pipeline_unknown_type.cpp
/// @brief M5 — unknown-type behavior depends on failOnUnknownType.

#include "Check.hpp"

#include <threadmaxx_migration/pipeline.hpp>

int main() {
    using namespace threadmaxx::migration;

    MigrationRegistry reg;
    reg.registerType("Known", SchemaVersion{1, 0, 0});
    MigrationPipeline pipeline{reg};

    RecordSet input{};
    Record rec{};
    rec.typeName = "Stranger";
    rec.stableId = 99;
    rec.sourceVersion = SchemaVersion{1, 0, 0};
    input.records.push_back(rec);

    // failOnUnknownType = true (default): the run aborts with an error.
    {
        MigrationOptions opts{};
        auto result = pipeline.migrate(input, SchemaVersion{1, 0, 0}, opts);
        CHECK(!result.ok);
        CHECK(!result.errors.empty());
        CHECK(result.errors[0].find("unknown type") != std::string::npos);
        CHECK(result.errors[0].find("Stranger") != std::string::npos);
    }

    // failOnUnknownType = false: the record passes through as an
    // opaque tombstone with a warning.
    {
        MigrationOptions opts{};
        opts.failOnUnknownType = false;
        auto result = pipeline.migrate(input, SchemaVersion{1, 0, 0}, opts);
        CHECK(result.ok);
        CHECK_EQ(result.output.records.size(), 1u);
        CHECK_EQ(result.output.records[0].typeName, std::string{"Stranger"});
        CHECK_EQ(result.warnings.size(), 1u);
        CHECK(result.warnings[0].find("tombstone") != std::string::npos);
    }

    EXIT_WITH_RESULT();
}
