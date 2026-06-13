/// @file test_migration_snapshot_stable_id_preservation.cpp
/// @brief M6 — entity ordinals are preserved across export → pipeline
/// → import: Record.stableId == original entity index, and import
/// produces the entities in the same order.

#include "Check.hpp"

#include <threadmaxx_migration/pipeline.hpp>
#include <threadmaxx_migration/world.hpp>

#include <threadmaxx/Components.hpp>
#include <threadmaxx/Handles.hpp>
#include <threadmaxx/Serialization.hpp>

int main() {
    using namespace threadmaxx::migration;

    threadmaxx::WorldSnapshot source{};
    for (std::uint32_t i = 0; i < 8; ++i) {
        source.entities.push_back(threadmaxx::EntityHandle{i, 1u});
        source.transforms.push_back(threadmaxx::Transform{
            {static_cast<float>(i), 0.0f, 0.0f}, {}, {1.0f, 1.0f, 1.0f}});
        source.masks.push_back(threadmaxx::ComponentSet{
            threadmaxx::Component::Transform});
    }

    SaveMetadata meta{};
    meta.schemaVersion = SchemaVersion{1, 0, 0};
    auto rs = exportSnapshot(source, meta);
    CHECK_EQ(rs.records.size(), 8u);
    for (std::size_t i = 0; i < rs.records.size(); ++i) {
        CHECK_EQ(rs.records[i].stableId, i);
    }

    // Empty pipeline = no-op migration; stableIds carry through.
    MigrationRegistry reg;
    reg.registerType(kEntityRecordTypeName, SchemaVersion{1, 0, 0});
    MigrationPipeline pipeline{reg};
    auto result = pipeline.migrate(rs, SchemaVersion{1, 0, 0});
    CHECK(result.ok);

    threadmaxx::WorldSnapshot reloaded{};
    CHECK(importSnapshot(result.output, reloaded));
    CHECK_EQ(reloaded.entities.size(), 8u);
    for (std::size_t i = 0; i < 8u; ++i) {
        // Generation preserved.
        CHECK_EQ(reloaded.entities[i].generation, 1u);
        // Index preserved.
        CHECK_EQ(reloaded.entities[i].index, i);
        // Per-component data preserved at the right slot.
        CHECK_EQ(reloaded.transforms[i].position.x,
                 static_cast<float>(i));
    }

    EXIT_WITH_RESULT();
}
