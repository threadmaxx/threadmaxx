/// @file test_editor_profiler_empty.cpp
/// @brief E13 — an unfed ProfilerView yields a zeroed-out summary.

#include "Check.hpp"

#include <threadmaxx_editor/profiler.hpp>

int main() {
    threadmaxx::editor::ProfilerView view{4};
    CHECK_EQ(view.capacity(), 4u);
    CHECK_EQ(view.sampleCount(), 0u);

    const auto s = view.summary();
    CHECK_EQ(s.samples, 0u);
    CHECK_EQ(s.firstTick, 0u);
    CHECK_EQ(s.lastTick, 0u);
    CHECK_EQ(s.systems.size(), 0u);

    EXIT_WITH_RESULT();
}
