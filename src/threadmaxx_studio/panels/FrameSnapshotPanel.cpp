/// @file panels/FrameSnapshotPanel.cpp

#include <threadmaxx_studio/panels/frame_snapshot.hpp>

#include <threadmaxx_editor/backend.hpp>

#include <threadmaxx/Stats.hpp>
#include <threadmaxx/Trace.hpp>

#include <cstdio>

namespace threadmaxx::studio {

namespace {

constexpr std::size_t kHistogramBins = 8;

} // namespace

FrameSnapshotPanel::FrameSnapshotPanel(std::size_t historyCapacity)
    : ring_(historyCapacity > 0 ? historyCapacity : 1, 0.0) {}

void FrameSnapshotPanel::onFrame(const threadmaxx::FrameSnapshot& snap) {
    latestTick_ = snap.engine.tick;
    latestStepSeconds_ = snap.engine.lastStepSeconds;
    ring_[writeIdx_] = snap.engine.lastStepSeconds;
    writeIdx_ = (writeIdx_ + 1) % ring_.size();
    if (sampleCount_ < ring_.size()) {
        ++sampleCount_;
    }
}

int FrameSnapshotPanel::latestFps() const noexcept {
    if (latestStepSeconds_ <= 0.0) {
        return 0;
    }
    const double f = 1.0 / latestStepSeconds_ + 0.5;
    if (f <= 0.0 || f >= 1.0e9) {
        return 0;
    }
    return static_cast<int>(f);
}

void FrameSnapshotPanel::render(editor::IEditorBackend& backend,
                                IStudioDataSource&) {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "FPS %d  tick=%llu  step=%.3fms",
                  latestFps(),
                  static_cast<unsigned long long>(latestTick_),
                  latestStepSeconds_ * 1000.0);
    backend.drawText(buf, 0.0f, 0.0f);

    if (sampleCount_ == 0) {
        return;
    }

    // Find max for normalization, then emit one row per histogram bin
    // as a string of '#' characters. Cheap, no GPU geometry needed at
    // ST10 — backends with a proper bar primitive can override.
    double maxSeen = 0.0;
    for (std::size_t i = 0; i < sampleCount_; ++i) {
        if (ring_[i] > maxSeen) maxSeen = ring_[i];
    }
    if (maxSeen <= 0.0) maxSeen = 1.0;

    std::size_t binCount[kHistogramBins] = {};
    for (std::size_t i = 0; i < sampleCount_; ++i) {
        const double v = ring_[i] / maxSeen; // [0,1]
        std::size_t bin = static_cast<std::size_t>(v * (kHistogramBins - 1));
        if (bin >= kHistogramBins) bin = kHistogramBins - 1;
        ++binCount[bin];
    }

    float y = 16.0f;
    for (std::size_t b = 0; b < kHistogramBins; ++b) {
        char row[64];
        std::size_t bars = binCount[b] > 32 ? 32 : binCount[b];
        char fill[33];
        for (std::size_t i = 0; i < bars; ++i) fill[i] = '#';
        fill[bars] = '\0';
        std::snprintf(row, sizeof(row), "bin %zu: %s (%zu)",
                      b, fill, binCount[b]);
        backend.drawText(row, 0.0f, y);
        y += 14.0f;
    }
}

} // namespace threadmaxx::studio
