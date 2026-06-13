/// @file Profiler.cpp
/// @brief E13 — live profiler view over `FrameSnapshot` streams.

#include "threadmaxx_editor/profiler.hpp"

#include <algorithm>
#include <limits>
#include <unordered_map>

namespace threadmaxx::editor {

ProfilerView::ProfilerView(std::size_t capacity) noexcept
    : capacity_(capacity == 0 ? 1u : capacity) {}

void ProfilerView::onFrame(const threadmaxx::FrameSnapshot& snap) {
    StepSample sample;
    sample.tick        = snap.engine.tick;
    sample.stepSeconds = snap.engine.lastStepSeconds;
    sample.systems.reserve(snap.systems.size());
    for (const auto& s : snap.systems) {
        SystemSample row;
        row.name           = s.name != nullptr ? std::string(s.name)
                                               : std::string{};
        row.updateSeconds  = s.lastUpdateSeconds;
        row.waitSeconds    = s.waitSeconds;
        row.peakQueueDepth = s.peakQueueDepth;
        sample.systems.push_back(std::move(row));
    }
    samples_.push_back(std::move(sample));
    while (samples_.size() > capacity_) samples_.pop_front();
}

ProfilerSummary ProfilerView::summary() const {
    ProfilerSummary out;
    if (samples_.empty()) return out;

    out.samples        = samples_.size();
    out.firstTick      = samples_.front().tick;
    out.lastTick       = samples_.back().tick;
    out.minStepSeconds = std::numeric_limits<double>::infinity();
    out.maxStepSeconds = 0.0;

    double totalStep = 0.0;
    std::unordered_map<std::string, std::size_t> rowIndex;
    for (const auto& s : samples_) {
        totalStep += s.stepSeconds;
        if (s.stepSeconds < out.minStepSeconds) out.minStepSeconds = s.stepSeconds;
        if (s.stepSeconds > out.maxStepSeconds) out.maxStepSeconds = s.stepSeconds;
        for (const auto& sys : s.systems) {
            auto it = rowIndex.find(sys.name);
            if (it == rowIndex.end()) {
                rowIndex.emplace(sys.name, out.systems.size());
                ProfilerSystemRow row;
                row.name               = sys.name;
                row.totalUpdateSeconds = sys.updateSeconds;
                row.totalWaitSeconds   = sys.waitSeconds;
                row.maxUpdateSeconds   = sys.updateSeconds;
                row.peakQueueDepth     = sys.peakQueueDepth;
                row.sampleCount        = 1;
                out.systems.push_back(std::move(row));
            } else {
                auto& row = out.systems[it->second];
                row.totalUpdateSeconds += sys.updateSeconds;
                row.totalWaitSeconds   += sys.waitSeconds;
                if (sys.updateSeconds > row.maxUpdateSeconds) {
                    row.maxUpdateSeconds = sys.updateSeconds;
                }
                if (sys.peakQueueDepth > row.peakQueueDepth) {
                    row.peakQueueDepth = sys.peakQueueDepth;
                }
                ++row.sampleCount;
            }
        }
    }

    out.avgStepSeconds = totalStep / static_cast<double>(out.samples);
    for (auto& row : out.systems) {
        const auto denom = static_cast<double>(row.sampleCount);
        row.avgUpdateSeconds = denom > 0.0 ? (row.totalUpdateSeconds / denom)
                                           : 0.0;
    }

    std::sort(out.systems.begin(), out.systems.end(),
              [](const ProfilerSystemRow& a, const ProfilerSystemRow& b) {
                  return a.avgUpdateSeconds > b.avgUpdateSeconds;
              });

    return out;
}

void ProfilerView::clear() noexcept {
    samples_.clear();
}

void ProfilerView::setCapacity(std::size_t cap) noexcept {
    capacity_ = cap == 0 ? 1u : cap;
    while (samples_.size() > capacity_) samples_.pop_front();
}

} // namespace threadmaxx::editor
