/// @file test_migration_snapshot_import_identity.cpp
/// @brief M6 — exportSnapshot → importSnapshot with no migrations
/// produces a byte-for-byte equal WorldSnapshot.

#include "Check.hpp"

#include <threadmaxx_migration/world.hpp>

#include <threadmaxx/Components.hpp>
#include <threadmaxx/Handles.hpp>
#include <threadmaxx/Serialization.hpp>

#include <sstream>
#include <string>

namespace {

std::string serializeToString(const threadmaxx::WorldSnapshot& s) {
    std::stringstream ss(std::ios::out | std::ios::binary);
    threadmaxx::serialize(ss, s);
    return ss.str();
}

} // namespace

int main() {
    using namespace threadmaxx::migration;

    threadmaxx::WorldSnapshot source{};
    // Entity 0: Transform + Velocity + Health.
    source.entities.push_back(threadmaxx::EntityHandle{0u, 1u});
    source.transforms.push_back(threadmaxx::Transform{
        {1.0f, 2.0f, 3.0f}, {}, {1.0f, 1.0f, 1.0f}});
    source.velocities.push_back(threadmaxx::Velocity{
        {0.5f, -0.5f, 0.0f}, {}});
    source.healths.push_back(threadmaxx::Health{75.0f, 100.0f});
    source.masks.push_back(threadmaxx::ComponentSet{
        threadmaxx::Component::Transform,
        threadmaxx::Component::Velocity,
        threadmaxx::Component::Health});

    // Entity 1: Transform only.
    source.entities.push_back(threadmaxx::EntityHandle{1u, 1u});
    source.transforms.push_back(threadmaxx::Transform{
        {10.0f, 20.0f, 30.0f}, {}, {2.0f, 2.0f, 2.0f}});
    source.masks.push_back(threadmaxx::ComponentSet{
        threadmaxx::Component::Transform});

    // Entity 2: Faction + UserData.
    source.entities.push_back(threadmaxx::EntityHandle{2u, 1u});
    source.factions.push_back(threadmaxx::Faction{7u});
    source.userData.push_back(threadmaxx::UserData{0xDEADBEEFull});
    source.masks.push_back(threadmaxx::ComponentSet{
        threadmaxx::Component::Faction,
        threadmaxx::Component::UserData});

    auto rs = exportSnapshot(source);
    CHECK_EQ(rs.records.size(), 3u);

    threadmaxx::WorldSnapshot reloaded{};
    CHECK(importSnapshot(rs, reloaded));

    // Byte-for-byte equality via the engine's serialize path.
    const auto blobA = serializeToString(source);
    const auto blobB = serializeToString(reloaded);
    CHECK_EQ(blobA.size(), blobB.size());
    CHECK_EQ(blobA, blobB);

    EXIT_WITH_RESULT();
}
