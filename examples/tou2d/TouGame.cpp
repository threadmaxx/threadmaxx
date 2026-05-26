#include "TouGame.hpp"

#include "CameraSystem.hpp"
#include "InputSystem.hpp"
#include "LevelLoader.hpp"
#include "MovementSystem.hpp"
#include "TerrainCollisionSystem.hpp"

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

    // ---- Systems ------------------------------------------------------------
    auto input     = std::make_unique<InputSystem>(window_, ids_);
    auto movement  = std::make_unique<MovementSystem>(ids_);
    auto collision = std::make_unique<TerrainCollisionSystem>(ids_);
    auto camera    = std::make_unique<CameraSystem>(ids_);
    camera_ = camera.get();

    engine.registerSystem(std::move(input));
    engine.registerSystem(std::move(movement));
    engine.registerSystem(std::move(collision));   // After movement, before camera latch.
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
    bool loaded = false;
    if (!levelDir_.empty()) {
        const auto info = loadImportedLevel(engine, seed, ids_.terrainBlock, levelDir_);
        loaded = info.loaded;
    }
    if (!loaded) {
        spawnSyntheticArena(engine, seed, ids_.terrainBlock);
    }
}

void TouGame::onTeardown(threadmaxx::Engine& /*engine*/,
                         threadmaxx::World&  /*world*/) {
    camera_ = nullptr;
}

} // namespace tou2d
