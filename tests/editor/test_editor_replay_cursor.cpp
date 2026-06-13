/// @file test_editor_replay_cursor.cpp
/// @brief E15 — ReplaySession cursor moves through frames; out-of-range
/// inputs clamp; listEntities reflects the snapshot at the cursor.

#include "Check.hpp"

#include <threadmaxx_editor/replay.hpp>

#include <threadmaxx/Components.hpp>
#include <threadmaxx/Serialization.hpp>

namespace {

threadmaxx::WorldSnapshot snapshotWith(std::uint32_t entityCount) {
    threadmaxx::WorldSnapshot s;
    for (std::uint32_t i = 0; i < entityCount; ++i) {
        s.entities.push_back({i + 1, 1});
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
            threadmaxx::Component::Transform,
            threadmaxx::Component::Velocity});
    }
    return s;
}

} // namespace

int main() {
    using namespace threadmaxx::editor;

    CaptureStream stream;
    stream.append(100, snapshotWith(2));
    stream.append(200, snapshotWith(5));
    stream.append(300, snapshotWith(0));

    ReplaySession session{stream};
    CHECK_EQ(session.frameCount(), 3u);
    CHECK_EQ(session.cursor(), 0u);
    CHECK(session.current() != nullptr);
    CHECK_EQ(session.currentTick(), 100u);
    CHECK_EQ(session.listEntities().size(), 2u);

    session.seek(1);
    CHECK_EQ(session.cursor(), 1u);
    CHECK_EQ(session.currentTick(), 200u);
    CHECK_EQ(session.listEntities().size(), 5u);

    // Out-of-range forward → clamp to last.
    session.seek(99);
    CHECK_EQ(session.cursor(), 2u);
    CHECK_EQ(session.currentTick(), 300u);
    CHECK_EQ(session.listEntities().size(), 0u);

    // Reverse step clamps at 0; cursor stays after the lower clamp.
    session.step(-5);
    CHECK_EQ(session.cursor(), 0u);
    session.step(1);
    CHECK_EQ(session.cursor(), 1u);
    session.step(100);
    CHECK_EQ(session.cursor(), 2u);

    // Components reflect the snapshot mask.
    session.seek(1);
    const auto rows = session.listEntities();
    CHECK_EQ(rows.size(), 5u);
    bool sawTransform = false, sawVelocity = false;
    for (const auto& c : rows[0].components) {
        if (c == "Transform") sawTransform = true;
        if (c == "Velocity")  sawVelocity = true;
    }
    CHECK(sawTransform);
    CHECK(sawVelocity);

    EXIT_WITH_RESULT();
}
