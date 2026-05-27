#pragma once

#include "DemoTypes.hpp"

#include <threadmaxx/EventChannel.hpp>
#include <threadmaxx/Game.hpp>
#include <threadmaxx/Handles.hpp>

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>

struct GLFWwindow;

namespace tou2d {

class BulletTerrainSystem;
class CameraSystem;
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

    void onSetup(threadmaxx::Engine& engine,
                 threadmaxx::World&  world,
                 threadmaxx::CommandBuffer& seed) override;
    void onTeardown(threadmaxx::Engine& engine,
                    threadmaxx::World&  world) override;

    CameraSystem* cameraSystem() noexcept { return camera_; }

    /// Handle of P1's ship — host-side smoke tests use this to verify
    /// final position. Valid only between onSetup and onTeardown.
    threadmaxx::EntityHandle playerShip() const noexcept { return playerShips_[0]; }

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
    std::array<threadmaxx::EntityHandle, 4> playerShips_{};
    std::filesystem::path    levelDir_;
    std::int32_t             cellsX_         = 0;
    std::int32_t             cellsY_         = 0;
    TerrainGrid              grid_;
    /// Round-end shared latch. Always-allocated; default-false.
    std::shared_ptr<std::atomic<bool>>      roundEnded_ =
        std::make_shared<std::atomic<bool>>(false);
    std::uint8_t                            winnerSlot_  = 0;
    std::uint16_t                           winnerKills_ = 0;
    threadmaxx::Subscription                roundEndSub_{};
};

} // namespace tou2d
