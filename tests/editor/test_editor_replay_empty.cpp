/// @file test_editor_replay_empty.cpp
/// @brief E15 — empty CaptureStream + ReplaySession returns sensible
/// defaults: nullptr current, zero tick, no-op seek/step.

#include "Check.hpp"

#include <threadmaxx_editor/replay.hpp>

int main() {
    using namespace threadmaxx::editor;

    CaptureStream empty;
    CHECK_EQ(empty.frameCount(), 0u);
    CHECK(empty.empty());

    ReplaySession session{empty};
    CHECK_EQ(session.frameCount(), 0u);
    CHECK_EQ(session.cursor(), 0u);
    CHECK(session.current() == nullptr);
    CHECK_EQ(session.currentTick(), 0u);
    CHECK_EQ(session.listEntities().size(), 0u);

    // No-op on empty stream.
    session.seek(99);
    CHECK_EQ(session.cursor(), 0u);
    session.step(5);
    CHECK_EQ(session.cursor(), 0u);
    session.step(-5);
    CHECK_EQ(session.cursor(), 0u);

    EXIT_WITH_RESULT();
}
