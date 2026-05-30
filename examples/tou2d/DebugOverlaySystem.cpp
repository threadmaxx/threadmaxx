#include "DebugOverlaySystem.hpp"

#include "CameraSystem.hpp"

#include <threadmaxx/Engine.hpp>
#include <threadmaxx/Trace.hpp>
#include <threadmaxx/render/DebugGeometry.hpp>
#include <threadmaxx/render/RenderFrameBuilder.hpp>

#include <cstdint>
#include <cstdio>

namespace tou2d {

namespace {

/// Layout — top-left anchor expressed as a fraction of the slot-0
/// viewport's `(halfH, halfH*aspect)` extent. Left edge sits a small
/// inset from -halfW; top row sits a small inset below +halfH. Each
/// successive row drops `kRowSpacingWU`.
constexpr float kLeftInsetFracOfHalfW = 0.93f;  ///< 7% in from the left
constexpr float kTopInsetFracOfHalfH  = 0.93f;  ///< 7% down from the top
constexpr float kRowSpacingWU         = 5.0f;
constexpr std::uint32_t kRowColor     = 0xFFCCFFCCu;  ///< pale green ARGB-friendly
constexpr std::uint32_t kHeaderColor  = 0xFFFFFFFFu;  ///< white for the FPS row

} // namespace

DebugOverlaySystem::DebugOverlaySystem(threadmaxx::Engine* eng,
                                      const CameraSystem* camera) noexcept
    : engine_(eng), camera_(camera) {}

void DebugOverlaySystem::update(threadmaxx::SystemContext&) {
    // No per-tick work — buildRenderFrame pulls fresh telemetry every
    // hook invocation. Update body kept empty so the system slots into
    // its own wave at zero cost when visible_ is false.
}

void DebugOverlaySystem::setTestSnapshot(
    const threadmaxx::EngineStats& engine,
    const std::vector<threadmaxx::SystemStats>& systems,
    const threadmaxx::JobSystemStats& jobs) noexcept {
    testEngine_     = engine;
    testSystems_    = systems;
    testJobs_       = jobs;
    hasTestSnapshot_ = true;
}

void DebugOverlaySystem::buildRenderFrame(threadmaxx::RenderFrameBuilder& b) {
    if (!visible_) return;

    threadmaxx::EngineStats                   engineStats{};
    std::span<const threadmaxx::SystemStats>  systemStats;
    threadmaxx::JobSystemStats                jobStats{};
    if (hasTestSnapshot_) {
        engineStats = testEngine_;
        systemStats = std::span<const threadmaxx::SystemStats>(
            testSystems_.data(), testSystems_.size());
        jobStats    = testJobs_;
    } else if (engine_) {
        const threadmaxx::FrameSnapshot snap = engine_->frameSnapshot();
        engineStats = snap.engine;
        systemStats = snap.systems;
        jobStats    = snap.jobs;
    } else {
        return;
    }

    // Anchor the strip at the top-left of slot-0's viewport. We tolerate
    // a null camera (tests sometimes skip it) — fall back to origin so
    // the overlay still surfaces text rows.
    float halfH = 80.0f;
    float halfW = 80.0f;
    threadmaxx::Vec3 center{0.0f, 0.0f, 0.0f};
    if (camera_) {
        halfH = camera_->effectiveOrthoHalfH();
        const float aspect = camera_->viewportAspect();
        halfW = halfH * aspect;
        center = camera_->followCenter(0);
    }
    const float xLeft = center.x - halfW * kLeftInsetFracOfHalfW;
    const float yTop  = center.y + halfH * kTopInsetFracOfHalfH;

    rowStorage_.clear();
    // Tight upper bound: kTopSystemRows + 5 fixed rows + 1 slack.
    rowStorage_.reserve(kTopSystemRows + 6);

    const auto pushRow =
        [&](int rowIndex, std::uint32_t color, std::string s) {
            const float y = yTop -
                static_cast<float>(rowIndex) * kRowSpacingWU;
            rowStorage_.emplace_back(std::move(s));
            threadmaxx::DebugText t{};
            t.position  = {xLeft, y, 0.0f};
            t.text      = rowStorage_.back();
            t.colorRGBA = color;
            b.addDebugText(t);
        };

    char buf[96];

    // Row 0 — FPS / ms.
    const double avgStep = engineStats.avgStepSeconds;
    const double fps = (avgStep > 0.0) ? (1.0 / avgStep) : 0.0;
    std::snprintf(buf, sizeof(buf),
                  "FPS %5.1f (%5.2fms)", fps, avgStep * 1000.0);
    pushRow(0, kHeaderColor, buf);

    // Row 1 — tick / commitHash / workers.
    std::snprintf(buf, sizeof(buf),
                  "tick=%llu hash=%08llx workers=%u",
                  static_cast<unsigned long long>(engineStats.tick),
                  static_cast<unsigned long long>(
                      engineStats.commitHash & 0xFFFFFFFFull),
                  static_cast<unsigned>(jobStats.workerCount));
    pushRow(1, kRowColor, buf);

    // Row 2 — entity count / cmds / jobs.
    std::snprintf(buf, sizeof(buf),
                  "entities=%llu cmds/step=%llu jobs/step=%llu",
                  static_cast<unsigned long long>(engineStats.aliveEntities),
                  static_cast<unsigned long long>(
                      engineStats.commandsCommittedLastStep),
                  static_cast<unsigned long long>(
                      engineStats.jobsSubmittedLastStep));
    pushRow(2, kRowColor, buf);

    // Row 3 — commit + render breakdown (microseconds).
    std::snprintf(buf, sizeof(buf),
                  "commit=%5.1fus render=%5.1fus",
                  engineStats.commitDurationSeconds * 1e6,
                  engineStats.engineBuildRenderFrameSeconds * 1e6);
    pushRow(3, kRowColor, buf);

    // Row 4 — human / bot slot count, sourced from the borrowed camera.
    if (camera_) {
        std::snprintf(buf, sizeof(buf),
                      "humans=%u (slot count)",
                      static_cast<unsigned>(camera_->numHumans()));
        pushRow(4, kRowColor, buf);
    }

    // Rows 5+ — top-N systems by avgUpdateSeconds.
    if (!systemStats.empty()) {
        std::array<std::size_t, kTopSystemRows> topIdx{};
        std::size_t topCount = 0;
        for (std::size_t i = 0; i < systemStats.size(); ++i) {
            const double s = systemStats[i].avgUpdateSeconds;
            std::size_t insertAt = topCount;
            while (insertAt > 0 &&
                   systemStats[topIdx[insertAt - 1]].avgUpdateSeconds < s) {
                if (insertAt < topIdx.size()) {
                    topIdx[insertAt] = topIdx[insertAt - 1];
                }
                --insertAt;
            }
            if (insertAt < topIdx.size()) {
                topIdx[insertAt] = i;
                if (topCount < topIdx.size()) ++topCount;
            }
        }
        const int firstRow = camera_ ? 5 : 4;
        for (std::size_t k = 0; k < topCount; ++k) {
            const auto& s = systemStats[topIdx[k]];
            std::snprintf(buf, sizeof(buf),
                          "top%zu %s %5.1fus",
                          k + 1,
                          s.name ? s.name : "(unnamed)",
                          s.avgUpdateSeconds * 1e6);
            pushRow(firstRow + static_cast<int>(k), kRowColor, buf);
        }
    }
}

} // namespace tou2d
