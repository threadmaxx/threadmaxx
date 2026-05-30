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

void DebugOverlaySystem::setGameStats(const DebugGameStats& g) {
    gameAliveBullets_   = g.aliveBullets;
    gameAliveParticles_ = g.aliveParticles;
    gameSolidCells_     = g.solidTerrainCells;
    gameViewports_      = g.viewportCount;
    gameCameraMode_.assign(g.cameraMode);
    gameWorldSeed_.assign(g.worldSeed);
    hasGameStats_       = true;
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
    // Upper bound: 5 fixed + 1 humans + 1 rfb + 4 game-state +
    // kTopSystemRows + 1 slack. Reserve up front so the strings'
    // data pointers stay valid for the rest of the call (the producer-
    // owned `addDebugText(DebugText)` path takes a borrowed string_view).
    rowStorage_.reserve(kTopSystemRows + 12);

    int nextRow = 0;
    const auto pushRow =
        [&](std::uint32_t color, std::string s) {
            const float y = yTop -
                static_cast<float>(nextRow++) * kRowSpacingWU;
            rowStorage_.emplace_back(std::move(s));
            threadmaxx::DebugText t{};
            t.position  = {xLeft, y, 0.0f};
            t.text      = rowStorage_.back();
            t.colorRGBA = color;
            b.addDebugText(t);
        };

    char buf[128];

    // Row — FPS / ms.
    const double avgStep = engineStats.avgStepSeconds;
    const double fps = (avgStep > 0.0) ? (1.0 / avgStep) : 0.0;
    std::snprintf(buf, sizeof(buf),
                  "FPS %5.1f (%5.2fms)", fps, avgStep * 1000.0);
    pushRow(kHeaderColor, buf);

    // Row — tick / commitHash / workers.
    std::snprintf(buf, sizeof(buf),
                  "tick=%llu hash=%08llx workers=%u",
                  static_cast<unsigned long long>(engineStats.tick),
                  static_cast<unsigned long long>(
                      engineStats.commitHash & 0xFFFFFFFFull),
                  static_cast<unsigned>(jobStats.workerCount));
    pushRow(kRowColor, buf);

    // Row — entity count / cmds / jobs.
    std::snprintf(buf, sizeof(buf),
                  "entities=%llu cmds/step=%llu jobs/step=%llu",
                  static_cast<unsigned long long>(engineStats.aliveEntities),
                  static_cast<unsigned long long>(
                      engineStats.commandsCommittedLastStep),
                  static_cast<unsigned long long>(
                      engineStats.jobsSubmittedLastStep));
    pushRow(kRowColor, buf);

    // Row — commit + render breakdown (microseconds).
    std::snprintf(buf, sizeof(buf),
                  "commit=%5.1fus render=%5.1fus",
                  engineStats.commitDurationSeconds * 1e6,
                  engineStats.engineBuildRenderFrameSeconds * 1e6);
    pushRow(kRowColor, buf);

    // M6.9b — Row — buildRenderFrame budget gate. Sum SystemStats::
    // buildRenderFrameSeconds across every system in the snapshot;
    // color the row red when the total exceeds 150 µs so a regression
    // is immediately visible on the F3 overlay.
    double rfbSumUs = 0.0;
    for (const auto& s : systemStats) {
        rfbSumUs += s.buildRenderFrameSeconds * 1e6;
    }
    const std::uint32_t rfbColor =
        rfbSumUs > kRenderFrameBudgetUs ? 0xFFFF6666u   // pale red
                                        : kRowColor;
    std::snprintf(buf, sizeof(buf),
                  "rfb=%5.1fus / %3.0fus budget",
                  rfbSumUs, kRenderFrameBudgetUs);
    pushRow(rfbColor, buf);

    // Row — human / bot slot count + camera-mode label.
    if (camera_) {
        std::snprintf(buf, sizeof(buf),
                      "humans=%u mode=%s (slot count)",
                      static_cast<unsigned>(camera_->numHumans()),
                      camera_->modeLabel());
        pushRow(kRowColor, buf);
    }

    // M6.9b — Game-state rows. Only fire when the host has called
    // `setGameStats` at least once (test paths without a wired host
    // suppress these rows entirely).
    if (hasGameStats_) {
        std::snprintf(buf, sizeof(buf),
                      "bullets=%u particles=%u",
                      static_cast<unsigned>(gameAliveBullets_),
                      static_cast<unsigned>(gameAliveParticles_));
        pushRow(kRowColor, buf);
        std::snprintf(buf, sizeof(buf),
                      "terrain=%u solid cells",
                      static_cast<unsigned>(gameSolidCells_));
        pushRow(kRowColor, buf);
        std::snprintf(buf, sizeof(buf),
                      "viewports=%u (%.*s)",
                      static_cast<unsigned>(gameViewports_),
                      static_cast<int>(gameCameraMode_.size()),
                      gameCameraMode_.data());
        pushRow(kRowColor, buf);
        std::snprintf(buf, sizeof(buf),
                      "seed=%.*s",
                      static_cast<int>(gameWorldSeed_.size()),
                      gameWorldSeed_.data());
        pushRow(kRowColor, buf);
    }

    // Rows — top-N systems by avgUpdateSeconds.
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
        for (std::size_t k = 0; k < topCount; ++k) {
            const auto& s = systemStats[topIdx[k]];
            std::snprintf(buf, sizeof(buf),
                          "top%zu %s %5.1fus",
                          k + 1,
                          s.name ? s.name : "(unnamed)",
                          s.avgUpdateSeconds * 1e6);
            pushRow(kRowColor, buf);
        }
    }
}

} // namespace tou2d
