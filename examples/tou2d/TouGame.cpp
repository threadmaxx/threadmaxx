#include "TouGame.hpp"

#include "BulletTerrainSystem.hpp"
#include "CameraSystem.hpp"
#include "InputSystem.hpp"
#include "LevelLoader.hpp"
#include "MovementSystem.hpp"
#include "ProjectileSystem.hpp"
#include "TerrainCollisionSystem.hpp"
#include "WeaponFireSystem.hpp"

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Engine.hpp>

namespace tou2d {

namespace {

/// Spawn the M2 synthetic arena: a solid perimeter ring + solid floor
/// row + air interior, centered on world origin. Replaced in M2.7 by
/// the imported `.lev` attribute grid; useful as a standalone smoke
/// target so the collision system can be exercised before the importer
/// pipeline lands.
void spawnSyntheticArena(threadmaxx::Engine& engine,
                         threadmaxx::CommandBuffer& seed,
                         threadmaxx::UserComponentId terrainBlockId) {
    constexpr int half = kArenaHalfCells;
    const float tile = kTileWorldUnits;

    for (int cy = -half; cy <= half; ++cy) {
        for (int cx = -half; cx <= half; ++cx) {
            const bool isPerimeter =
                cx == -half || cx == half || cy == -half || cy == half;
            if (!isPerimeter) continue;  // interior is Air — skip.

            const auto handle = engine.reserveEntityHandle();

            threadmaxx::Bundle b = {};
            b.transform.position = {
                static_cast<float>(cx) * tile,
                static_cast<float>(cy) * tile,
                0.0f,
            };
            // Slightly under tile to leave a hairline gap visually; the
            // collision side keeps a full-tile AABB (see
            // TerrainCollisionSystem). Z scale stays at tile so the
            // ortho camera looking down -Z sees a square front face.
            b.transform.scale    = {tile * 0.96f, tile * 0.96f, tile};
            b.renderTag          = threadmaxx::RenderTag{0, 1, 0u};  // mat 1 reserved for terrain
            b.initialMask        = threadmaxx::ComponentSet{
                threadmaxx::Component::Transform,
                threadmaxx::Component::RenderTag,
            };
            seed.spawnBundle(handle, b);

            TerrainBlock blk{};
            blk.attr  = Attribute::Solid;
            blk.hp    = 0xFF;   // synthetic arena is bedrock — indestructible.
            blk.cellX = static_cast<std::int16_t>(cx);
            blk.cellY = static_cast<std::int16_t>(cy);
            threadmaxx::addUserComponent(seed, terrainBlockId, handle, blk);
        }
    }
}

} // namespace

TouGame::TouGame(GLFWwindow* window) noexcept : window_(window) {}

void TouGame::onSetup(threadmaxx::Engine& engine,
                      threadmaxx::World&  /*world*/,
                      threadmaxx::CommandBuffer& seed) {
    // ---- User components ----------------------------------------------------
    ids_.playerInput  = engine.registerUserComponent<PlayerInput>();
    ids_.localPlayer  = engine.registerUserComponent<LocalPlayer>();
    ids_.ship         = engine.registerUserComponent<Ship>();
    ids_.terrainBlock = engine.registerUserComponent<TerrainBlock>();
    ids_.bullet       = engine.registerUserComponent<Bullet>();

    // ---- Systems ------------------------------------------------------------
    auto input         = std::make_unique<InputSystem>(window_, ids_);
    auto movement      = std::make_unique<MovementSystem>(ids_);
    auto* movementPtr  = movement.get();
    auto collision     = std::make_unique<TerrainCollisionSystem>(ids_);
    auto* collisionPtr = collision.get();
    auto weaponFire    = std::make_unique<WeaponFireSystem>(ids_);
    auto projectile    = std::make_unique<ProjectileSystem>(ids_);
    auto bulletTerrain     = std::make_unique<BulletTerrainSystem>(ids_, collisionPtr);
    auto* bulletTerrainPtr = bulletTerrain.get();
    auto* projectilePtr    = projectile.get();
    auto camera            = std::make_unique<CameraSystem>(ids_);
    camera_         = camera.get();
    bulletTerrain_  = bulletTerrainPtr;

    engine.registerSystem(std::move(input));
    engine.registerSystem(std::move(movement));
    engine.registerSystem(std::move(collision));      // After movement, before camera latch.
    engine.registerSystem(std::move(weaponFire));     // Spawns bullets — wave by itself per R/W.
    engine.registerSystem(std::move(projectile));     // Integrates bullets after spawn commit.
    engine.registerSystem(std::move(bulletTerrain));  // Bullet-vs-tile after projectile integration.
    engine.registerSystem(std::move(camera));

    // ---- Seed the first ship ------------------------------------------------
    const auto shipH = engine.reserveEntityHandle();
    playerShip_ = shipH;

    threadmaxx::Bundle b = {};
    b.transform.position = {0.0f, 0.0f, 0.0f};
    b.transform.scale    = {28.0f, 28.0f, 28.0f};   // ~32-unit ship-square
    b.velocity           = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
    b.renderTag          = threadmaxx::RenderTag{0, -1, 0u};  // meshId 0 = default cube
    b.initialMask        = threadmaxx::ComponentSet{
        threadmaxx::Component::Transform,
        threadmaxx::Component::Velocity,
        threadmaxx::Component::RenderTag,
    };
    seed.spawnBundle(shipH, b);

    threadmaxx::addUserComponent(seed, ids_.localPlayer, shipH, LocalPlayer{/*slot*/ 0, {}});
    threadmaxx::addUserComponent(seed, ids_.playerInput, shipH, PlayerInput{});
    threadmaxx::addUserComponent(seed, ids_.ship, shipH, Ship{});

    // ---- Seed terrain -------------------------------------------------------
    // Prefer an imported level if the host wired one up; fall back to
    // the synthetic arena for a self-contained smoke target.
    // Helper — compute the level's world rect from cellsX/cellsY using
    // the same mapping LevelLoader applies (halfX = cellsX/2 int div,
    // worldCellX = cx - halfX). For even cellsX the cell range is
    // asymmetric (e.g. cellsX=32 -> cells -16..+15), so the world
    // rect is too. Inclusive of the outer cell edges.
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

    bool loaded = false;
    if (!levelDir_.empty()) {
        const auto info = loadImportedLevel(engine, seed, ids_.terrainBlock, levelDir_);
        loaded = info.loaded;
        if (loaded) {
            cellsX_ = info.cellsX;
            cellsY_ = info.cellsY;
            float minX = 0, minY = 0, maxX = 0, maxY = 0;
            computeRect(info.cellsX, info.cellsY, minX, minY, maxX, maxY);
            // Ship clamps to the same rect as the JPG quad so the
            // boundary visuals + collision line up. Bullets self-
            // destruct on the same rect — no escaping the level.
            movementPtr  ->setLevelRect(minX, minY, maxX, maxY);
            projectilePtr->setLevelRect(minX, minY, maxX, maxY);
        }
    }
    if (!loaded) {
        spawnSyntheticArena(engine, seed, ids_.terrainBlock);
        // Synthetic arena is (2*kArenaHalfCells + 1) cells per side,
        // centered on origin (odd count → symmetric).
        const float h = static_cast<float>(kArenaHalfCells) * kTileWorldUnits +
                        kTileWorldUnits * 0.5f;
        movementPtr  ->setLevelRect(-h, -h, +h, +h);
        projectilePtr->setLevelRect(-h, -h, +h, +h);
    }
}

void TouGame::onTeardown(threadmaxx::Engine& /*engine*/,
                         threadmaxx::World&  /*world*/) {
    camera_        = nullptr;
    bulletTerrain_ = nullptr;
}

void TouGame::setTileDestroyCallback(TileDestroyCallback cb) {
    if (bulletTerrain_) bulletTerrain_->setDestroyCallback(std::move(cb));
}

} // namespace tou2d
