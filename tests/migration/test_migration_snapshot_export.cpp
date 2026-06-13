/// @file test_migration_snapshot_export.cpp
/// @brief M6 — exportSnapshot produces one Record per entity.

#include "Check.hpp"

#include <threadmaxx_migration/world.hpp>

#include <threadmaxx/Components.hpp>
#include <threadmaxx/Handles.hpp>
#include <threadmaxx/Serialization.hpp>

int main() {
    using namespace threadmaxx::migration;

    threadmaxx::WorldSnapshot snap{};
    constexpr std::size_t kN = 100;
    snap.entities.reserve(kN);
    snap.transforms.reserve(kN);
    snap.masks.reserve(kN);

    for (std::size_t i = 0; i < kN; ++i) {
        snap.entities.push_back(threadmaxx::EntityHandle{
            static_cast<std::uint32_t>(i), 1u});
        snap.transforms.push_back(threadmaxx::Transform{});
        // Every entity has Transform.
        snap.masks.push_back(threadmaxx::ComponentSet{
            threadmaxx::Component::Transform});
    }

    auto rs = exportSnapshot(snap);
    CHECK_EQ(rs.records.size(), kN);

    for (std::size_t i = 0; i < kN; ++i) {
        const auto& rec = rs.records[i];
        CHECK_EQ(rec.typeName, std::string{kEntityRecordTypeName});
        CHECK_EQ(rec.stableId, i);
    }

    EXIT_WITH_RESULT();
}
