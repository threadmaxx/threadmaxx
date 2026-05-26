#include "TouGame.hpp"

#include "BulletTerrainSystem.hpp"
#include "CameraSystem.hpp"
#include "InputSystem.hpp"
#include "LevelLoader.hpp"
#include "MovementSystem.hpp"
#include "ProjectileSystem.hpp"
#include "ShipLifecycleSystem.hpp"
#include "TerrainCollisionSystem.hpp"
#include "WeaponFireSystem.hpp"

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Engine.hpp>

namespace tou2d {

namespace {

/// M3.3 — synthetic arena fallback. No entities; just populate `grid`
/// with a Solid perimeter ring. The ship-collision system will see the
/// walls and bounce off; rendering shows only the JPG background (none
/// installed in the synthetic-arena path) so visually it's a void with
/// hard walls — fine as a smoke target.
void populateSyntheticArena(TerrainGrid& grid) {
    constexpr int half = kArenaHalfCells;
    const std::int32_t cellsX = 2 * half + 1;
    const std::int32_t cellsY = 2 * half + 1;
    grid.reset(cellsX, cellsY);

    for (int cy = -half; cy <= half; ++cy) {
        for (int cx = -half; cx <= half; ++cx) {
            const bool isPerimeter =
                cx == -half || cx == half || cy == -half || cy == half;
            if (!isPerimeter) continue;
            grid.setSolid(cx, cy, /*hp=*/0xFF, Attribute::Solid);  // bedrock
        }
    }
}

/// Spawn one ship at (x, y) with LocalPlayer slot `slot`. P1 uses
/// (0, 0) so the existing smoke test still works; P2-P4 are offset
/// just enough to be visible inside the synthetic arena.
threadmaxx::EntityHandle spawnShip(threadmaxx::Engine& engine,
                                   threadmaxx::CommandBuffer& seed,
                                   const UserComponentIds& ids,
                                   std::uint8_t slot,
                                   float x, float y) {
    const auto h = engine.reserveEntityHandle();

    threadmaxx::Bundle b = {};
    b.transform.position = {x, y, 0.0f};
    b.transform.scale    = {28.0f, 28.0f, 28.0f};
    b.velocity           = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
    b.renderTag          = threadmaxx::RenderTag{0, -1, 0u};
    b.initialMask        = threadmaxx::ComponentSet{
        threadmaxx::Component::Transform,
        threadmaxx::Component::Velocity,
        threadmaxx::Component::RenderTag,
    };
    seed.spawnBundle(h, b);

    threadmaxx::addUserComponent(seed, ids.localPlayer, h, LocalPlayer{slot, {}});
    threadmaxx::addUserComponent(seed, ids.playerInput, h, PlayerInput{});

    Ship s{};
    s.currentHp   = 100.0f;
    s.maxHp       = 100.0f;
    s.spawnX      = x;
    s.spawnY      = y;
    s.shipKindIdx = 0;
    s.respawnIn   = 0;
    s.score       = 0;
    threadmaxx::addUserComponent(seed, ids.ship, h, s);

    return h;
}

} // namespace

TouGame::TouGame(GLFWwindow* window) noexcept : window_(window) {}

void TouGame::onSetup(threadmaxx::Engine& engine,
                      threadmaxx::World&  /*world*/,
                      threadmaxx::CommandBuffer& seed) {
    // ---- User components ----------------------------------------------------
    ids_.playerInput = engine.registerUserComponent<PlayerInput>();
    ids_.localPlayer = engine.registerUserComponent<LocalPlayer>();
    ids_.ship        = engine.registerUserComponent<Ship>();
    ids_.bullet      = engine.registerUserComponent<Bullet>();

    // ---- Seed terrain grid --------------------------------------------------
    // Populate BEFORE constructing systems so the system constructors
    // can take a borrowed pointer to a grid that's already sized.
    bool loaded = false;
    if (!levelDir_.empty()) {
        const auto info = loadImportedLevel(grid_, levelDir_);
        loaded = info.loaded;
        if (loaded) {
            cellsX_ = info.cellsX;
            cellsY_ = info.cellsY;
        }
    }
    if (!loaded) {
        populateSyntheticArena(grid_);
        cellsX_ = grid_.cellsX;
        cellsY_ = grid_.cellsY;
    }

    // ---- Compute world rect from grid extents ------------------------------
    auto computeRect = [](std::int32_t cellsX, std::int32_t cellsY,
                          float& minX, float& minY,
                          float& maxX, float& maxY) {
        const int   halfX = cellsX / 2;
        const int   halfY = cellsY / 2;
        const float t     = kTileWorldUnits;
        minX = -static_cast<float>(halfX) * t - t * 0.5f;
        maxX = static_cast<float>(cellsX - halfX - 1) * t + t * 0.5f;
        minY = -static_cast<float>(cellsY - halfY - 1) * t - t * 0.5f;
        maxY = static_cast<float>(halfY) * t + t * 0.5f;
    };
    float minX = 0, minY = 0, maxX = 0, maxY = 0;
    computeRect(cellsX_, cellsY_, minX, minY, maxX, maxY);

    // ---- Systems ------------------------------------------------------------
    auto input         = std::make_unique<InputSystem>(window_, ids_);
    auto movement      = std::make_unique<MovementSystem>(ids_);
    auto* movementPtr  = movement.get();
    auto collision     = std::make_unique<TerrainCollisionSystem>(ids_, &grid_);
    auto* collisionPtr = collision.get();
    auto weaponFire    = std::make_unique<WeaponFireSystem>(ids_);
    auto projectile    = std::make_unique<ProjectileSystem>(ids_);
    auto* projectilePtr = projectile.get();
    auto bulletTerrain     = std::make_unique<BulletTerrainSystem>(ids_, &grid_);
    auto* bulletTerrainPtr = bulletTerrain.get();
    auto shipLife          = std::make_unique<ShipLifecycleSystem>(ids_);
    auto camera            = std::make_unique<CameraSystem>(ids_);
    camera_         = camera.get();
    bulletTerrain_  = bulletTerrainPtr;
    collision_      = collisionPtr;

    movementPtr  ->setLevelRect(minX, minY, maxX, maxY);
    projectilePtr->setLevelRect(minX, minY, maxX, maxY);

    engine.registerSystem(std::move(input));
    engine.registerSystem(std::move(movement));
    engine.registerSystem(std::move(collision));
    engine.registerSystem(std::move(weaponFire));
    engine.registerSystem(std::move(projectile));
    engine.registerSystem(std::move(bulletTerrain));
    engine.registerSystem(std::move(shipLife));     // late — sees commits from movement/collision
    engine.registerSystem(std::move(camera));

    // ---- Seed 4 ships ------------------------------------------------------
    // P1 stays at (0, 0) so the headless smoke test continues to find
    // it. P2-P4 are placed at small offsets — close enough that they
    // remain inside the synthetic arena's interior, far enough that
    // they don't visually overlap on spawn.
    constexpr float kOffset = 40.0f;
    const std::array<std::pair<float, float>, 4> seeds = {{
        { 0.0f,        0.0f       },   // P1
        { +kOffset,    0.0f       },   // P2
        { -kOffset,    0.0f       },   // P3
        { 0.0f,        +kOffset   },   // P4
    }};
    for (std::uint8_t slot = 0; slot < 4; ++slot) {
        const auto& sp = seeds[slot];
        playerShips_[slot] = spawnShip(engine, seed, ids_,
                                       slot, sp.first, sp.second);
    }
}

void TouGame::onTeardown(threadmaxx::Engine& /*engine*/,
                         threadmaxx::World&  /*world*/) {
    camera_        = nullptr;
    bulletTerrain_ = nullptr;
    collision_     = nullptr;
}

void TouGame::setTileDestroyCallback(TileDestroyCallback cb) {
    if (bulletTerrain_) bulletTerrain_->setDestroyCallback(cb);
    if (collision_)     collision_    ->setDestroyCallback(std::move(cb));
}

} // namespace tou2d
