/// @file test_studio_replay_loads_snapshot.cpp
/// @brief ST23 — bind a CaptureStream to ReplayPanel; cursor /
/// currentTick / listEntities reflect the bound stream; `seek` and
/// `step` move through frames.

#include "Check.hpp"

#include <threadmaxx_editor/backends/headless.hpp>
#include <threadmaxx_editor/replay.hpp>
#include <threadmaxx_studio/panels/replay.hpp>

#include <threadmaxx/Components.hpp>
#include <threadmaxx/Serialization.hpp>

namespace {

threadmaxx::WorldSnapshot makeSnap(std::uint32_t entityCount) {
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
            threadmaxx::Component::Transform});
    }
    return s;
}

std::size_t countTextOps(
    const threadmaxx::editor::CapturedFrame& frame) {
    std::size_t n = 0;
    for (const auto& op : frame.ops) {
        if (op.op == threadmaxx::editor::CapturedOp::Op::DrawText) ++n;
    }
    return n;
}

struct NullSource : threadmaxx::studio::IStudioDataSource {
    threadmaxx::studio::AttachMode mode() const noexcept override {
        return threadmaxx::studio::AttachMode::Direct;
    }
};

} // namespace

int main() {
    using namespace threadmaxx;
    studio::ReplayPanel panel;
    CHECK(panel.stream() == nullptr);

    editor::HeadlessBackend backend;
    backend.initialize();
    NullSource source;

    // Detached.
    backend.beginFrame();
    panel.render(backend, source);
    backend.endFrame();
    CHECK_EQ(panel.rowCount(), 1u);
    CHECK_EQ(countTextOps(backend.capturedFrame()), 1u);

    editor::CaptureStream stream;
    stream.append(100, makeSnap(2));
    stream.append(200, makeSnap(5));
    stream.append(300, makeSnap(0));

    panel.setStream(&stream);
    CHECK(panel.stream() == &stream);
    CHECK_EQ(panel.cursor(), 0u);
    CHECK_EQ(panel.currentTick(), 100u);

    backend.beginFrame();
    panel.render(backend, source);
    backend.endFrame();
    // header + entities-count + 2 entity rows = 4
    CHECK_EQ(panel.rowCount(), 4u);
    CHECK_EQ(countTextOps(backend.capturedFrame()), 4u);

    // Step forward.
    panel.step(1);
    CHECK_EQ(panel.cursor(), 1u);
    CHECK_EQ(panel.currentTick(), 200u);
    panel.setMaxEntityRows(3);
    backend.beginFrame();
    panel.render(backend, source);
    backend.endFrame();
    // 5 entities but cap at 3 → 2 + 3 = 5 rows.
    CHECK_EQ(panel.rowCount(), 5u);

    // Seek past end clamps.
    panel.seek(99);
    CHECK_EQ(panel.cursor(), 2u);
    CHECK_EQ(panel.currentTick(), 300u);
    backend.beginFrame();
    panel.render(backend, source);
    backend.endFrame();
    CHECK_EQ(panel.rowCount(), 2u); // header + entities-count, no rows

    // Detach via setStream(nullptr).
    panel.setStream(nullptr);
    CHECK(panel.stream() == nullptr);
    CHECK_EQ(panel.cursor(), 0u);
    backend.beginFrame();
    panel.render(backend, source);
    backend.endFrame();
    CHECK_EQ(panel.rowCount(), 1u);

    backend.shutdown();
    EXIT_WITH_RESULT();
}
