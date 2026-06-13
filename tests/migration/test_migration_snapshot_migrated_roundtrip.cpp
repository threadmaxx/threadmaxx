/// @file test_migration_snapshot_migrated_roundtrip.cpp
/// @brief M6 — old-version snapshot → migrate through the pipeline
/// → import yields the upgraded WorldSnapshot. Verifies the
/// pipeline-between-export-and-import flow is end-to-end usable.

#include "Check.hpp"

#include <threadmaxx_migration/pipeline.hpp>
#include <threadmaxx_migration/world.hpp>

#include <threadmaxx/Components.hpp>
#include <threadmaxx/Handles.hpp>
#include <threadmaxx/Serialization.hpp>

#include <cstring>

int main() {
    using namespace threadmaxx::migration;

    // Source snapshot at v1.0.0.
    threadmaxx::WorldSnapshot source{};
    source.entities.push_back(threadmaxx::EntityHandle{0u, 1u});
    source.transforms.push_back(threadmaxx::Transform{
        {1.0f, 2.0f, 3.0f}, {}, {1.0f, 1.0f, 1.0f}});
    source.healths.push_back(threadmaxx::Health{50.0f, 50.0f});
    source.masks.push_back(threadmaxx::ComponentSet{
        threadmaxx::Component::Transform,
        threadmaxx::Component::Health});

    SaveMetadata meta{};
    meta.schemaVersion = SchemaVersion{1, 0, 0};
    auto rs = exportSnapshot(source, meta);
    CHECK_EQ(rs.records.size(), 1u);

    // Register a migration that doubles every entity's Health
    // current value (1.0.0 → 1.1.0).
    MigrationRegistry reg;
    reg.registerType(kEntityRecordTypeName, SchemaVersion{1, 0, 0});
    reg.addMigration(kEntityRecordTypeName,
                     SchemaVersion{1, 0, 0},
                     SchemaVersion{1, 1, 0},
                     [](Record& r) {
                         for (auto& f : r.fields) {
                             if (f.name != "health") continue;
                             threadmaxx::Health h{};
                             if (f.value.bytes.size() == sizeof(h)) {
                                 std::memcpy(&h, f.value.bytes.data(),
                                             sizeof(h));
                                 h.current *= 2.0f;
                                 std::memcpy(f.value.bytes.data(), &h,
                                             sizeof(h));
                             }
                         }
                     });

    MigrationPipeline pipeline{reg};
    auto result = pipeline.migrate(rs, SchemaVersion{1, 1, 0});
    CHECK(result.ok);

    threadmaxx::WorldSnapshot upgraded{};
    CHECK(importSnapshot(result.output, upgraded));
    CHECK_EQ(upgraded.entities.size(), 1u);
    CHECK_EQ(upgraded.healths.size(), 1u);
    CHECK_EQ(upgraded.healths[0].current, 100.0f);
    CHECK_EQ(upgraded.healths[0].max, 50.0f);

    // Transform unchanged.
    CHECK_EQ(upgraded.transforms.size(), 1u);
    CHECK_EQ(upgraded.transforms[0].position.x, 1.0f);

    EXIT_WITH_RESULT();
}
