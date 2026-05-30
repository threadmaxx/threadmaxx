#pragma once

#include <threadmaxx/Stats.hpp>
#include <threadmaxx/System.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace threadmaxx { class Engine; }

namespace tou2d {

class CameraSystem;

/// M6.9 — F3-toggled telemetry overlay.
///
/// The system owns a single visibility flag. When off, `buildRenderFrame`
/// is a no-op (zero render cost). When on, it pulls `Engine::frameSnapshot`
/// from the borrowed engine pointer and emits a vertical strip of
/// `DebugText` rows anchored at the top-left of slot-0's viewport.
///
/// Rows (top to bottom):
///   * `FPS NN.N (TT.Tms)` — derived from `EngineStats::avgStepSeconds`
///   * `tick=NNNN hash=XXXXXXXX workers=N`
///   * `entities=NNNN cmds/step=NN jobs/step=NN`
///   * `commit=NN.Nus render=NN.Nus` — engineBuildRenderFrameSeconds +
///     commitDurationSeconds (the two engine-side post-update slices)
///   * `humans=N bots=N` — derived from the borrowed CameraSystem
///   * `top1 / top2 / top3` — three slowest systems by
///     `avgUpdateSeconds`, formatted as `<name> NN.Nus`
///
/// reads()  = none
/// writes() = none
///
/// The overlay strictly observes — it never mutates state, so the
/// determinism contract is unaffected.
class DebugOverlaySystem : public threadmaxx::ISystem {
public:
    DebugOverlaySystem(threadmaxx::Engine* eng,
                       const CameraSystem* camera) noexcept;

    const char*              name()   const noexcept override { return "tou2d.debug_overlay"; }
    threadmaxx::ComponentSet reads()  const noexcept override {
        return threadmaxx::ComponentSet::none();
    }
    threadmaxx::ComponentSet writes() const noexcept override {
        return threadmaxx::ComponentSet::none();
    }

    void update          (threadmaxx::SystemContext& ctx) override;
    void buildRenderFrame(threadmaxx::RenderFrameBuilder& b) override;

    /// Visibility flag. Off by default. Toggled by the host on F3.
    void setVisible(bool v) noexcept { visible_ = v; }
    void toggle()           noexcept { visible_ = !visible_; }
    bool visible() const    noexcept { return visible_; }

    /// @internal Test hook: install a synthetic snapshot so unit tests can
    /// drive `buildRenderFrame` without standing up an `Engine`. While a
    /// test snapshot is installed, `buildRenderFrame` reads from it
    /// instead of calling `engine_->frameSnapshot`. Pass `clear=true`
    /// (or call `clearTestSnapshot`) to drop the override.
    void setTestSnapshot(const threadmaxx::EngineStats& engine,
                         const std::vector<threadmaxx::SystemStats>& systems,
                         const threadmaxx::JobSystemStats& jobs) noexcept;
    void clearTestSnapshot() noexcept { hasTestSnapshot_ = false; }

    /// Maximum number of top-N system rows emitted. Fixed at 3 per the
    /// M6.9 spec.
    static constexpr std::size_t kTopSystemRows = 3;

private:
    threadmaxx::Engine*        engine_   = nullptr;  // borrowed
    const CameraSystem*        camera_   = nullptr;  // borrowed
    bool                       visible_  = false;

    // Test override.
    bool                                  hasTestSnapshot_ = false;
    threadmaxx::EngineStats               testEngine_{};
    std::vector<threadmaxx::SystemStats>  testSystems_;
    threadmaxx::JobSystemStats            testJobs_{};

    // Per-tick scratch — owned strings keep the DebugText views alive
    // through the renderer's consume of this frame. Cleared at the top
    // of every `buildRenderFrame`.
    std::vector<std::string> rowStorage_;
};

} // namespace tou2d
