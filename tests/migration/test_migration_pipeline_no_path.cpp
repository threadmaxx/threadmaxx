/// @file test_migration_pipeline_no_path.cpp
/// @brief M5 — no chain to target → ok=false; error mentions the gap.

#include "Check.hpp"

#include <threadmaxx_migration/pipeline.hpp>

int main() {
    using namespace threadmaxx::migration;

    MigrationRegistry reg;
    reg.registerType("Health", SchemaVersion{1, 0, 0});
    reg.addMigration("Health", SchemaVersion{1, 0, 0}, SchemaVersion{1, 1, 0},
                     [](Record&) {});

    MigrationPipeline pipeline{reg};

    RecordSet input{};
    Record rec{};
    rec.typeName = "Health";
    rec.stableId = 1;
    rec.sourceVersion = SchemaVersion{1, 0, 0};
    input.records.push_back(rec);

    // Target 2.0.0 not reachable from registered chain.
    auto result = pipeline.migrate(input, SchemaVersion{2, 0, 0});
    CHECK(!result.ok);
    CHECK_EQ(result.errors.size(), 1u);
    const auto& msg = result.errors[0];
    CHECK(msg.find("no migration path") != std::string::npos);
    CHECK(msg.find("Health") != std::string::npos);
    CHECK(msg.find("1.0.0") != std::string::npos);
    CHECK(msg.find("2.0.0") != std::string::npos);

    EXIT_WITH_RESULT();
}
