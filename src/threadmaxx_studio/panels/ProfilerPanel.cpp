/// @file panels/ProfilerPanel.cpp
/// @brief ST11 — `ProfilerPanel` implementation.

#include <threadmaxx_studio/panels/profiler.hpp>

#include <threadmaxx_editor/backend.hpp>

#include <cstdio>

namespace threadmaxx::studio {

ProfilerPanel::ProfilerPanel(std::size_t capacity)
    : view_(capacity) {}

void ProfilerPanel::onFrame(const threadmaxx::FrameSnapshot& snap) {
    view_.onFrame(snap);
}

void ProfilerPanel::render(editor::IEditorBackend& backend,
                           IStudioDataSource&) {
    const auto s = view_.summary();
    char buf[160];
    std::snprintf(buf, sizeof(buf),
                  "Profiler  samples=%llu  ticks=%llu..%llu  "
                  "avg=%.3fms  min=%.3fms  max=%.3fms",
                  static_cast<unsigned long long>(s.samples),
                  static_cast<unsigned long long>(s.firstTick),
                  static_cast<unsigned long long>(s.lastTick),
                  s.avgStepSeconds * 1000.0,
                  s.minStepSeconds * 1000.0,
                  s.maxStepSeconds * 1000.0);
    backend.drawText(buf, 0.0f, 0.0f);

    float y = 16.0f;
    std::size_t shown = 0;
    for (const auto& row : s.systems) {
        if (shown >= maxRows_) break;
        char rowBuf[160];
        std::snprintf(rowBuf, sizeof(rowBuf),
                      "%-24.24s  avg=%.3fms  max=%.3fms  peak_q=%u",
                      row.name.c_str(),
                      row.avgUpdateSeconds * 1000.0,
                      row.maxUpdateSeconds * 1000.0,
                      row.peakQueueDepth);
        backend.drawText(rowBuf, 0.0f, y);
        y += 14.0f;
        ++shown;
    }
}

} // namespace threadmaxx::studio
