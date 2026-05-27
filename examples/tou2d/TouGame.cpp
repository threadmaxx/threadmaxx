#include "TouGame.hpp"

#include "BotControlSystem.hpp"
#include "BulletShipCollisionSystem.hpp"
#include "BulletTerrainSystem.hpp"
#include "CameraSystem.hpp"
#include "HudSystem.hpp"
#include "InputSystem.hpp"
#include "LevelLoader.hpp"
#include "MovementSystem.hpp"
#include "ProjectileSystem.hpp"
#include "RoundRestartSystem.hpp"
#include "ShipLifecycleSystem.hpp"
#include "TerrainCollisionSystem.hpp"
#include "WeaponFireSystem.hpp"

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Engine.hpp>
#include <threadmaxx/EventChannel.hpp>

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
/// just enough to be visible inside the synthetic arena. `isBot` flips
/// the input source — P1 is the human, P2-P4 are bots by default.
/// `shipKindIdx` indexes `kShipKinds` — see TouGame::onSetup for slot
/// → kind assignments.
threadmaxx::EntityHandle spawnShip(threadmaxx::Engine& engine,
                                   threadmaxx::CommandBuffer& seed,
                                   const UserComponentIds& ids,
                                   std::uint8_t slot,
                                   float x, float y,
                                   std::uint8_t isBot,
                                   std::uint16_t shipKindIdx) {
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

    LocalPlayer lp{};
    lp.slot  = slot;
    lp.isBot = isBot;
    threadmaxx::addUserComponent(seed, ids.localPlayer, h, lp);
    threadmaxx::addUserComponent(seed, ids.playerInput, h, PlayerInput{});

    // M4.5 — per-kind HP from the manual stat table.
    const ShipKind& kind = shipKindAt(shipKindIdx);
    const float kindHp = kind.strength * kShipHpPerStrength;

    Ship s{};
    s.currentHp   = kindHp;
    s.maxHp       = kindHp;
    s.spawnX      = x;
    s.spawnY      = y;
    s.shipKindIdx    = shipKindIdx;
    s.respawnIn      = 0;
    s.kills          = 0;
    s.tilesDestroyed = 0;
    threadmaxx::addUserComponent(seed, ids.ship, h, s);

    // M4.2 — start with a full magazine on both weapons. Reload counters
    // are zero (ready to fire). ShipLifecycleSystem rewrites this to the
    // same default on respawn, so a player who dies mid-reload comes
    // back with a fresh full magazine instead of a partial-reload limbo.
    WeaponLoadout loadout{};
    loadout.dumbfireAmmo     = kDumbfireMagazine;
    loadout.dumbfireReloadIn = 0;
    loadout.spreadAmmo       = kSpreadMagazine;
    loadout.spreadReloadIn   = 0;
    threadmaxx::addUserComponent(seed, ids.loadout, h, loadout);

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
    ids_.loadout     = engine.registerUserComponent<WeaponLoadout>();

    // ---- Pre-warm typed event channels on the sim thread -------------------
    // M4.3 — collision is now the authoritative writer for the round-
    // end shared atomic AND the winner pointers (no subscription).
    // The typed `RoundEnded` channel is still emitted-into so future
    // listeners (audio sting, telemetry) can subscribe — pre-warm here
    // on the sim thread so the first emit doesn't race a concurrent
    // factory call.
    (void) engine.events<RoundEnded>();

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
    input->setRoundEndedFlag(roundEnded_);
    auto botControl    = std::make_unique<BotControlSystem>(ids_);
    botControl->setRoundEndedFlag(roundEnded_);
    auto roundRestart  = std::make_unique<RoundRestartSystem>(window_, ids_);
    roundRestart->setRoundEndedFlag(roundEnded_, &winnerSlot_, &winnerKills_);
    roundRestart->setTerrainGrid(&grid_);
    auto movement      = std::make_unique<MovementSystem>(ids_);
    auto* movementPtr  = movement.get();
    auto collision     = std::make_unique<TerrainCollisionSystem>(ids_, &grid_);
    auto* collisionPtr = collision.get();
    auto weaponFire    = std::make_unique<WeaponFireSystem>(ids_);
    weaponFire->setRoundEndedFlag(roundEnded_);
    auto projectile    = std::make_unique<ProjectileSystem>(ids_);
    auto* projectilePtr = projectile.get();
    auto bulletShip        = std::make_unique<BulletShipCollisionSystem>(ids_, &engine);
    bulletShip->setRoundEndedFlag(roundEnded_, &winnerSlot_, &winnerKills_);
    bulletShip->setMatchMode(&matchMode_);
    auto bulletTerrain     = std::make_unique<BulletTerrainSystem>(ids_, &grid_);
    auto* bulletTerrainPtr = bulletTerrain.get();
    auto shipLife          = std::make_unique<ShipLifecycleSystem>(ids_);
    shipLife->setMatchMode(&matchMode_);
    shipLife->setTerrainGrid(&grid_);
    auto camera            = std::make_unique<CameraSystem>(ids_);
    camera_         = camera.get();
    bulletTerrain_  = bulletTerrainPtr;
    collision_      = collisionPtr;
    auto hud        = std::make_unique<HudSystem>(ids_, camera_);
    hud->setRoundEndedFlag(roundEnded_, &winnerSlot_, &winnerKills_);

    movementPtr  ->setLevelRect(minX, minY, maxX, maxY);
    projectilePtr->setLevelRect(minX, minY, maxX, maxY);

    engine.registerSystem(std::move(input));
    engine.registerSystem(std::move(botControl));   // overrides PlayerInput for bot slots
    engine.registerSystem(std::move(roundRestart)); // preStep; resets everything when human presses fire post-round
    engine.registerSystem(std::move(movement));
    engine.registerSystem(std::move(collision));
    engine.registerSystem(std::move(weaponFire));
    engine.registerSystem(std::move(projectile));
    engine.registerSystem(std::move(bulletShip));   // ships first → bullet despawn before terrain check
    engine.registerSystem(std::move(bulletTerrain));
    engine.registerSystem(std::move(shipLife));     // late — sees commits from movement/collision
    engine.registerSystem(std::move(camera));
    engine.registerSystem(std::move(hud));          // last — buildRenderFrame reads camera state

    // ---- Seed 4 ships ------------------------------------------------------
    // P1 stays at (0, 0) so the headless smoke test continues to find
    // it. P2-P4 are placed at small offsets — close enough that they
    // remain inside the synthetic arena's interior, far enough that
    // they don't visually overlap on spawn. P1 is human; P2-P4 are
    // bots by default (BotControlSystem drives PlayerInput for them).
    //
    // M4.5 — distinct ship kinds per slot show off the stat-spread:
    //   P1 = Basic ship   (150 HP, 1.0× thrust, 1.0× turn) — neutral
    //   P2 = Bee          ( 50 HP, 3.3× thrust, 2.2× turn) — fast/fragile
    //   P3 = X Wing       (125 HP, 1.5× thrust, 1.3× turn) — balanced
    //   P4 = Destroyer    (300 HP, 0.5× thrust, 0.3× turn) — tank/slow
    constexpr float kOffset = 40.0f;
    struct ShipSeed { float x, y; std::uint16_t kindIdx; };
    const std::array<ShipSeed, 4> seeds = {{
        { 0.0f,        0.0f,     0 },   // P1 — Basic
        { +kOffset,    0.0f,     6 },   // P2 — Bee
        { -kOffset,    0.0f,     4 },   // P3 — X Wing
        { 0.0f,        +kOffset, 8 },   // P4 — Destroyer
    }};
    for (std::uint8_t slot = 0; slot < 4; ++slot) {
        const auto& sp = seeds[slot];
        const std::uint8_t isBot = (slot == 0) ? std::uint8_t{0} : std::uint8_t{1};
        playerShips_[slot] = spawnShip(engine, seed, ids_,
                                       slot, sp.x, sp.y, isBot, sp.kindIdx);
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
