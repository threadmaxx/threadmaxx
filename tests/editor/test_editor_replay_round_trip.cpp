/// @file test_editor_replay_round_trip.cpp
/// @brief E15 — three captured snapshots survive save/load and the
/// (tick, entity-count, mask) tuple of each is preserved.

#include "Check.hpp"

#include <threadmaxx_editor/replay.hpp>

#include <threadmaxx/Components.hpp>
#include <threadmaxx/Serialization.hpp>

#include <sstream>

namespace {

threadmaxx::WorldSnapshot makeSnapshot(std::uint32_t entityCount,
                                       std::uint32_t generation) {
    threadmaxx::WorldSnapshot s;
    s.entities.reserve(entityCount);
    s.transforms.reserve(entityCount);
    s.velocities.reserve(entityCount);
    s.renderTags.reserve(entityCount);
    s.userData.reserve(entityCount);
    s.accelerations.reserve(entityCount);
    s.parents.reserve(entityCount);
    s.healths.reserve(entityCount);
    s.factions.reserve(entityCount);
    s.animationStates.reserve(entityCount);
    s.physicsBodies.reserve(entityCount);
    s.navAgents.reserve(entityCount);
    s.boundingVolumes.reserve(entityCount);
    s.masks.reserve(entityCount);
    for (std::uint32_t i = 0; i < entityCount; ++i) {
        s.entities.push_back({i + 1, generation});
        s.transforms.push_back({});
        s.velocities.push_back({});
        s.renderTags.push_back({});
        s.userData.push_back({});
        s.accelerations.push_back({});
        s.parents.push_back({});
        s.healths.push_back({});
        s.factions.push_back({});
        s.animationStates.push_back({});
        s.physicsBodies.push_back({});
        s.navAgents.push_back({});
        s.boundingVolumes.push_back({});
        s.masks.push_back(threadmaxx::ComponentSet{
            threadmaxx::Component::Transform});
    }
    return s;
}

} // namespace

int main() {
    using namespace threadmaxx::editor;

    CaptureStream stream;
    stream.append(10, makeSnapshot(2, 1));
    stream.append(20, makeSnapshot(3, 2));
    stream.append(30, makeSnapshot(1, 3));
    CHECK_EQ(stream.frameCount(), 3u);

    std::stringstream blob(std::ios::in | std::ios::out | std::ios::binary);
    stream.save(blob);

    CaptureStream loaded;
    CHECK(loaded.load(blob));
    CHECK_EQ(loaded.frameCount(), 3u);
    CHECK_EQ(loaded.frame(0).tick, 10u);
    CHECK_EQ(loaded.frame(0).snapshot.entities.size(), 2u);
    CHECK_EQ(loaded.frame(1).tick, 20u);
    CHECK_EQ(loaded.frame(1).snapshot.entities.size(), 3u);
    CHECK_EQ(loaded.frame(2).tick, 30u);
    CHECK_EQ(loaded.frame(2).snapshot.entities.size(), 1u);

    // Garbage input doesn't damage the loaded stream.
    std::stringstream noise("garbage", std::ios::in | std::ios::binary);
    CaptureStream populated;
    populated.append(99, makeSnapshot(1, 1));
    CHECK(!populated.load(noise));

    EXIT_WITH_RESULT();
}
