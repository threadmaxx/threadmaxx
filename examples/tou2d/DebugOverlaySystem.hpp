#pragma once

#include <threadmaxx/Stats.hpp>
#include <threadmaxx/System.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace threadmaxx { class Engine; }

namespace tou2d {

class CameraSystem;

/// M6.9b — game-side numbers the F3 overlay reads. The host (main.cpp)
/// rebuilds this each frame from the per-system accessors and pushes it
/// in via `DebugOverlaySystem::setGameStats`. Strings are `string_view`
/// so the caller owns lifetime through the next `buildRenderFrame`; the
/// overlay copies them into its row storage before emitting.
struct DebugGameStats {
    std::uint32_t    aliveBullets       = 0;
    std::uint32_t    aliveParticles     = 0;
    std::uint32_t    solidTerrainCells  = 0;
    std::uint8_t     viewportCount      = 0;  ///< == CameraSystem::numHumans()
    std::string_view cameraMode         = {}; ///< CameraSystem::modeLabel()
    std::string_view worldSeed          = {}; ///< TouGame::worldSeedDescriptor()
};

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

    /// M6.9b — game-side stats source. Host (main.cpp) pushes a fresh
    /// `DebugGameStats` each frame; the overlay paints additional rows
    /// for projectile / particle / terrain counts, viewport mode, and
    /// world seed. Until `setGameStats` is called the game rows are
    /// suppressed (the existing engine telemetry rows still fire).
    /// Owned strings inside `gameStats_` survive to the next call, so
    /// the overlay can deep-copy the `string_view` payloads at row-build
    /// time without lifetime concerns.
    void setGameStats(const DebugGameStats& g);
    bool hasGameStats() const noexcept { return hasGameStats_; }

    /// M6.9b — buildRenderFrame budget gate. The overlay aggregates
    /// `SystemStats::buildRenderFrameSeconds` across every system in
    /// the snapshot and colors the row red when the total exceeds
    /// `kRenderFrameBudgetUs`. Exposed for tests so they can pin the
    /// exact threshold without recomputing it.
    static constexpr double kRenderFrameBudgetUs = 150.0;

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

    // M6.9b — game-side stats latched from `setGameStats`. Strings are
    // owned (deep-copy) so the snapshot survives until the next set
    // call even if the producer's underlying storage moves.
    bool                                  hasGameStats_      = false;
    std::uint32_t                         gameAliveBullets_  = 0;
    std::uint32_t                         gameAliveParticles_ = 0;
    std::uint32_t                         gameSolidCells_    = 0;
    std::uint8_t                          gameViewports_     = 0;
    std::string                           gameCameraMode_;
    std::string                           gameWorldSeed_;

    // Per-tick scratch — owned strings keep the DebugText views alive
    // through the renderer's consume of this frame. Cleared at the top
    // of every `buildRenderFrame`; reserved once for the worst-case row
    // count so push paths never invalidate earlier views.
    std::vector<std::string> rowStorage_;
};

} // namespace tou2d
