#pragma once

#include "DemoTypes.hpp"
#include "SpriteCompositor.hpp"

#include <threadmaxx/Game.hpp>
#include <threadmaxx/Handles.hpp>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

struct GLFWwindow;

namespace tou2d {

class BulletTerrainSystem;
class CameraSystem;
class InputSystem;
class TerrainCollisionSystem;

/// IGame implementation for the tou2d demo.
///
/// onSetup:
///   * Registers user components (PlayerInput, LocalPlayer, Ship,
///     Bullet — no per-tile entity in M3.3+; terrain lives in
///     `TerrainGrid grid_`).
///   * Registers Input / Movement / Collision / WeaponFire /
///     Projectile / BulletTerrain / ShipLifecycle / Camera.
///   * Populates `grid_` from either an imported `.lev` directory or
///     the synthetic arena fallback.
///   * Spawns 4 ships, one per LocalPlayer slot (P2-P4 share the
///     keyboard; physically present even when nobody's holding the
///     keys — they just sit at their spawn point).
class TouGame : public threadmaxx::IGame {
public:
    explicit TouGame(GLFWwindow* window) noexcept;

    void setLevelDir(std::filesystem::path p) noexcept { levelDir_ = std::move(p); }
    /// M4.8 — runtime path that hosts `ships/*.SHP` and `sfx/*.WAV`.
    /// Defaults to the install's `assets/` if not overridden. Set
    /// BEFORE `engine.initialize(game)` so onSetup can load atlases
    /// + sound bank.
    void setAssetDir(std::filesystem::path p) noexcept { assetDir_ = std::move(p); }
    /// M4.8 — borrowed compositor pointer; main.cpp owns the canonical
    /// instance and the engine fills it during onSetup. The compositor
    /// is also the sole reader from main.cpp's per-tick driver.
    void setSpriteCompositor(SpriteCompositor* c) noexcept { compositor_ = c; }
    SpriteCompositor* spriteCompositor() noexcept { return compositor_; }

    /// M4.8 — borrowed view of the user-component IDs. main.cpp needs
    /// the sprite-id to feed the compositor's per-tick walk. Valid
    /// between onSetup and onTeardown.
    const UserComponentIds& userComponentIds() const noexcept { return ids_; }

    /// M4.3 — select round mode. Default is `Deathmatch` (kFragLimit
    /// kills wins, ships respawn). `LastShipStanding` means death is
    /// permanent for the round and the last ship alive wins. Must be
    /// called BEFORE `engine.initialize(game)` — onSetup latches the
    /// mode through to the systems via a borrowed pointer.
    void setMatchMode(MatchMode m) noexcept { matchMode_ = m; }
    MatchMode matchMode() const noexcept { return matchMode_; }

    /// M5.1 — configure how many local-keyboard players and how many
    /// AI bots to spawn. Humans land in slots `[0, humans)`; bots in
    /// `[humans, humans + bots)`. Caller must pre-validate the ranges
    /// (humans in [1, kMaxHumans], bots in [0, kMaxPlayerSlots - kMaxHumans],
    /// humans + bots >= 2). Must be called BEFORE `engine.initialize(game)`.
    void setPlayerCounts(std::uint8_t humans, std::uint8_t bots) noexcept {
        numHumans_ = humans;
        numBots_   = bots;
    }
    std::uint8_t numHumans() const noexcept { return numHumans_; }
    std::uint8_t numBots()   const noexcept { return numBots_;   }

    void onSetup(threadmaxx::Engine& engine,
                 threadmaxx::World&  world,
                 threadmaxx::CommandBuffer& seed) override;
    void onTeardown(threadmaxx::Engine& engine,
                    threadmaxx::World&  world) override;

    CameraSystem* cameraSystem() noexcept { return camera_; }

    /// M5.4 — borrowed pointer to the input system. Valid only between
    /// `onSetup` and `onTeardown`. The host (main.cpp) uses it to wire
    /// a `ReplayPlayer*` for `--play` mode; null after teardown.
    InputSystem*  inputSystem()  noexcept { return input_; }

    /// Handle of P1's ship — host-side smoke tests use this to verify
    /// final position. Valid only between onSetup and onTeardown.
    threadmaxx::EntityHandle playerShip() const noexcept {
        return playerShips_.empty() ? threadmaxx::EntityHandle{}
                                    : playerShips_.front();
    }

    std::int32_t levelCellsX() const noexcept { return cellsX_; }
    std::int32_t levelCellsY() const noexcept { return cellsY_; }

    /// Fires once per destroyed tile from either destruction path.
    using TileDestroyCallback =
        std::function<void(std::int32_t cellX, std::int32_t cellY)>;
    void setTileDestroyCallback(TileDestroyCallback cb);

    /// M4.2 — shared latch flipped to true the first time
    /// `BulletShipCollisionSystem` emits `RoundEnded`. Input / bot /
    /// weapon-fire systems hold the same shared_ptr and short-circuit
    /// while it's set; bullets in flight continue to resolve so the
    /// world drains cleanly. shared_ptr ownership: TouGame owns the
    /// canonical instance; systems borrow it; subscription lambda
    /// holds a weak copy via the shared_ptr capture.
    std::shared_ptr<std::atomic<bool>> roundEndedFlag() const noexcept {
        return roundEnded_;
    }

    /// Winner slot recorded at the moment RoundEnded fired (0-3); valid
    /// only when roundEndedFlag()->load() is true. Used by HudSystem
    /// to paint the winner banner in the correct slot color.
    std::uint8_t  winnerSlot()  const noexcept { return winnerSlot_;  }
    std::uint16_t winnerKills() const noexcept { return winnerKills_; }

private:
    GLFWwindow*              window_         = nullptr;
    UserComponentIds         ids_;
    CameraSystem*            camera_         = nullptr;   // borrowed
    BulletTerrainSystem*     bulletTerrain_  = nullptr;   // borrowed
    TerrainCollisionSystem*  collision_      = nullptr;   // borrowed
    InputSystem*             input_          = nullptr;   // borrowed
    // M5.1 — sized dynamically (1 human + 1 bot minimum, up to
    // kMaxPlayerSlots). Slot index = vector index. Stored solely for
    // playerShip() (smoke-test position log) and onTeardown bookkeeping.
    std::vector<threadmaxx::EntityHandle> playerShips_;
    std::uint8_t             numHumans_      = 1;
    std::uint8_t             numBots_        = 3;
    std::filesystem::path    levelDir_;
    std::filesystem::path    assetDir_;
    SpriteCompositor*        compositor_     = nullptr;   // borrowed
    std::int32_t             cellsX_         = 0;
    std::int32_t             cellsY_         = 0;
    TerrainGrid              grid_;
    /// Round-end shared latch. Always-allocated; default-false.
    /// M4.3 — collision now writes this directly (via shared_ptr
    /// setter), and RoundRestartSystem clears it on reset. The
    /// subscription on the typed channel is gone — collision is the
    /// authoritative writer and the channel remains a public hook for
    /// future listeners (audio, telemetry).
    std::shared_ptr<std::atomic<bool>>      roundEnded_ =
        std::make_shared<std::atomic<bool>>(false);
    std::uint8_t                            winnerSlot_  = 0;
    std::uint16_t                           winnerKills_ = 0;
    MatchMode                               matchMode_   = MatchMode::Deathmatch;
};

} // namespace tou2d
