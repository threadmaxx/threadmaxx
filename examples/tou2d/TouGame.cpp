#include "TouGame.hpp"

#include "AudioSystem.hpp"
#include "BotControlSystem.hpp"
#include "BulletHomingSystem.hpp"
#include "BulletShipCollisionSystem.hpp"
#include "BulletTerrainSystem.hpp"
#include "ParticleSystem.hpp"
#include "CameraSystem.hpp"
#include "HudSystem.hpp"
#include "InputSystem.hpp"
#include "UISystem.hpp"
#include "LevelLoader.hpp"
#include "MovementSystem.hpp"
#include "ProjectileSystem.hpp"
#include "RepairPickupSystem.hpp"
#include "RoundRestartSystem.hpp"
#include "ShipLifecycleSystem.hpp"
#include "SpriteAtlas.hpp"
#include "TerrainCollisionSystem.hpp"
#include "WeaponFireSystem.hpp"

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Engine.hpp>
#include <threadmaxx/EventChannel.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace tou2d {

namespace {

/// M3.3 — synthetic arena fallback. No entities; just populate `grid`
/// with a Solid perimeter ring. The ship-collision system will see the
/// walls and bounce off; rendering shows only the JPG background (none
/// installed in the synthetic-arena path) so visually it's a void with
/// hard walls — fine as a smoke target.
///
/// M5.7 — also drops a small fixed sprinkle of Repair tiles inside the
/// arena. Fixed positions so the synthetic-path smoke is deterministic
/// without piping a seed through. Eight tiles spaced around the origin
/// at ±r in both axes — far enough from the spawn that early ticks
/// don't immediately consume them.
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

    constexpr int kSpoke = 8;
    constexpr std::array<std::pair<int, int>, 8> kRepairCells = {{
        {  kSpoke,  0       }, { -kSpoke,  0       },
        {  0,       kSpoke  }, {  0,      -kSpoke  },
        {  kSpoke,  kSpoke  }, { -kSpoke, -kSpoke  },
        {  kSpoke, -kSpoke  }, { -kSpoke,  kSpoke  },
    }};
    for (const auto& [cx, cy] : kRepairCells) {
        grid.setRepair(cx, cy);
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
                                   std::uint16_t shipKindIdx,
                                   std::int32_t  spriteAtlasIdx,
                                   std::uint8_t  specialKind) {
    const auto h = engine.reserveEntityHandle();

    threadmaxx::Bundle b = {};
    b.transform.position = {x, y, 0.0f};
    b.transform.scale    = {28.0f, 28.0f, 28.0f};
    b.velocity           = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
    // M4.8 — ships with a loaded sprite atlas render via the
    // foreground sprite layer (SpriteCompositor) and have no
    // RenderTag so the cube path doesn't double-render under them.
    // Ships without an atlas keep the cube fallback.
    const bool useSprite = spriteAtlasIdx >= 0;
    b.renderTag          = threadmaxx::RenderTag{0, -1, 0u};
    b.initialMask        = useSprite
        ? threadmaxx::ComponentSet{
              threadmaxx::Component::Transform,
              threadmaxx::Component::Velocity,
          }
        : threadmaxx::ComponentSet{
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
    // M5.6 — `specialKind` selects which entry in the special-weapon
    // catalogue this ship spawns with. Mag size comes from the same
    // spec so a Rapid ship spawns with its big mag, a Sniper with 3.
    WeaponLoadout loadout{};
    loadout.dumbfireAmmo     = kDumbfireMagazine;
    loadout.dumbfireReloadIn = 0;
    loadout.specialKind      = specialKind;
    loadout.specialAmmo      = specialSpecAt(specialKind).magazine;
    loadout.specialReloadIn  = 0;
    threadmaxx::addUserComponent(seed, ids.loadout, h, loadout);

    if (useSprite && ids.sprite.valid()) {
        ShipSpriteRef ref{};
        ref.atlasIdx = spriteAtlasIdx;
        threadmaxx::addUserComponent(seed, ids.sprite, h, ref);
    }

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
    ids_.sprite      = engine.registerUserComponent<ShipSpriteRef>();

    // ---- Pre-warm typed event channels on the sim thread -------------------
    // M4.3 — collision is now the authoritative writer for the round-
    // end shared atomic AND the winner pointers (no subscription).
    // The typed `RoundEnded` channel is still emitted-into so future
    // listeners (audio sting, telemetry) can subscribe — pre-warm here
    // on the sim thread so the first emit doesn't race a concurrent
    // factory call.
    (void) engine.events<RoundEnded>();
    // M4.8 — pre-warm the AudioPlay channel before gameplay systems
    // start emitting so the first emission doesn't race the factory.
    (void) engine.events<AudioPlay>();

    // ---- Seed terrain grid --------------------------------------------------
    // Populate BEFORE constructing systems so the system constructors
    // can take a borrowed pointer to a grid that's already sized.
    // M5.5 — three-way branch: gen wins over levelDir (parser-side
    // mutex), levelDir wins over synthetic arena fallback.
    bool loaded = false;
    if (genConfig_.has_value()) {
        const auto info = generateProceduralLevel(grid_, *genConfig_);
        loaded = info.loaded;
        if (loaded) {
            cellsX_ = info.cellsX;
            cellsY_ = info.cellsY;
            std::printf("[gen] seed=0x%08x ggLevel=%u stuffD=%u perim=%u "
                        "→ %dx%d, %d solid\n",
                        info.seedUsed,
                        unsigned(genConfig_->ggLevel),
                        unsigned(genConfig_->stuffDensity),
                        unsigned(genConfig_->perimeterBedrock),
                        info.cellsX, info.cellsY, info.solidCount);
        }
    } else if (!levelDir_.empty()) {
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
    // M6.0b — UISystem is registered FIRST so its eventual menu
    // handlers (M6.1+) see input before any gameplay system. Today the
    // skeleton parks on `UIScreen::None` and `update` is a no-op,
    // preserving the CLI-direct-jump bit-for-bit (replay determinism
    // is unaffected by registration-order alone since None-state
    // doesn't read or write any component).
    auto ui            = std::make_unique<UISystem>(&engine, UIScreen::None);
    ui_                = ui.get();
    auto input         = std::make_unique<InputSystem>(window_, ids_);
    input->setRoundEndedFlag(roundEnded_);
    input_             = input.get();   // borrowed; M5.4 replay hook reaches it via TouGame::inputSystem()
    auto botControl    = std::make_unique<BotControlSystem>(ids_);
    botControl->setRoundEndedFlag(roundEnded_);
    botControl->setTerrainGrid(&grid_);
    auto roundRestart  = std::make_unique<RoundRestartSystem>(window_, ids_);
    roundRestart->setRoundEndedFlag(roundEnded_, &winnerSlot_, &winnerKills_);
    roundRestart->setTerrainGrid(&grid_);
    auto movement      = std::make_unique<MovementSystem>(ids_);
    auto* movementPtr  = movement.get();
    auto collision     = std::make_unique<TerrainCollisionSystem>(ids_, &grid_);
    auto* collisionPtr = collision.get();
    auto repairPickup     = std::make_unique<RepairPickupSystem>(ids_, &grid_, &engine);
    auto* repairPickupPtr = repairPickup.get();
    auto weaponFire    = std::make_unique<WeaponFireSystem>(ids_, &engine);
    weaponFire->setRoundEndedFlag(roundEnded_);
    auto homing        = std::make_unique<BulletHomingSystem>(ids_);
    auto projectile    = std::make_unique<ProjectileSystem>(ids_);
    auto* projectilePtr = projectile.get();
    auto bulletShip        = std::make_unique<BulletShipCollisionSystem>(ids_, &engine);
    bulletShip->setRoundEndedFlag(roundEnded_, &winnerSlot_, &winnerKills_);
    bulletShip->setMatchMode(&matchMode_);
    auto bulletTerrain     = std::make_unique<BulletTerrainSystem>(ids_, &grid_, &engine);
    auto* bulletTerrainPtr = bulletTerrain.get();
    auto shipLife          = std::make_unique<ShipLifecycleSystem>(ids_);
    shipLife->setMatchMode(&matchMode_);
    shipLife->setTerrainGrid(&grid_);
    // M5.3 — particle FX system. Owns its own state; ParticleSystem*
    // is shared (borrowed) with the three emitters below. Registered
    // FIRST in the wave list (see below) so its `update()` integrates
    // existing particles BEFORE any emitter spawns this tick's fresh
    // ones — newly-spawned particles render at their spawn position
    // this tick, integrate next tick.
    auto particles         = std::make_unique<ParticleSystem>();
    auto* particlesPtr     = particles.get();
    bulletShip   ->setParticleSystem(particlesPtr);
    bulletTerrain->setParticleSystem(particlesPtr);
    shipLife     ->setParticleSystem(particlesPtr);
    repairPickup ->setParticleSystem(particlesPtr);
    auto camera            = std::make_unique<CameraSystem>(ids_);
    camera->setNumHumans(numHumans_);
    camera_         = camera.get();
    bulletTerrain_  = bulletTerrainPtr;
    collision_      = collisionPtr;
    repairPickup_   = repairPickupPtr;
    auto hud        = std::make_unique<HudSystem>(ids_, camera_);
    hud->setRoundEndedFlag(roundEnded_, &winnerSlot_, &winnerKills_);

    movementPtr  ->setLevelRect(minX, minY, maxX, maxY);
    projectilePtr->setLevelRect(minX, minY, maxX, maxY);

    engine.registerSystem(std::move(ui));           // M6.0b — empty body while current()==None; M6.1 fills handlers
    engine.registerSystem(std::move(input));
    engine.registerSystem(std::move(botControl));   // overrides PlayerInput for bot slots
    engine.registerSystem(std::move(roundRestart)); // preStep; resets everything when human presses fire post-round
    engine.registerSystem(std::move(particles));    // M5.3 — integrate existing particles BEFORE any emitter spawns fresh ones this tick
    engine.registerSystem(std::move(movement));
    engine.registerSystem(std::move(collision));
    engine.registerSystem(std::move(repairPickup)); // M5.7 — consume Repair tiles BEFORE weaponFire so a cycled weapon fires this same tick
    engine.registerSystem(std::move(weaponFire));
    engine.registerSystem(std::move(homing));       // M5.8 — steer Homer bullets BEFORE projectile/bulletShip so a freshly-spawned Homer locks on this tick
    engine.registerSystem(std::move(projectile));
    engine.registerSystem(std::move(bulletShip));   // ships first → bullet despawn before terrain check
    engine.registerSystem(std::move(bulletTerrain));
    engine.registerSystem(std::move(shipLife));     // late — sees commits from movement/collision
    engine.registerSystem(std::move(camera));
    engine.registerSystem(std::move(hud));          // last — buildRenderFrame reads camera state

    // M4.8 — register AudioSystem (subscribes to AudioPlay; no ECS
    // reads/writes; sits in its own wave at the end).
    if (!assetDir_.empty()) {
        engine.registerSystem(std::make_unique<AudioSystem>(&engine, assetDir_));
    }

    // ---- Seed ships --------------------------------------------------------
    // M5.1 — humans land in slots [0, numHumans_); bots fill out
    // [numHumans_, numHumans_ + numBots_). Both humans and bots spawn
    // at random Air cells in the terrain (M5.2 follow-up — the M5.1
    // ring spawn put bots in predictable arcs around the origin, but
    // the user asked for random spawn placement to match the random
    // respawn behaviour). Falls back to a ring around the origin when
    // there's no terrain grid to sample (synthetic arena is a thin
    // perimeter; sampleRandomRespawn still works there because the
    // interior IS Air).
    const std::uint32_t totalShips =
        static_cast<std::uint32_t>(numHumans_) +
        static_cast<std::uint32_t>(numBots_);

    // M5.1 — load the canonical 4 SHP atlases once; humans 0..3 each
    // get their own (matches the M4.8 yellow/blue/red/green palette).
    // Bots cycle through them via `(slot % 4)` so colors repeat in the
    // expected order without per-bot atlas duplication.
    struct AtlasSeed {
        std::uint16_t kindIdx;
        const char*   shpStem;
    };
    constexpr std::array<AtlasSeed, 4> kAtlasSeeds = {{
        { 0, "TIEF" },   // slot 0 / Basic kind / TIE Fighter sprite (yellow)
        { 6, "BEE2" },   // slot 1 / Bee kind / B2 Stealth (blue)
        { 4, "XWIN" },   // slot 2 / X Wing kind + sprite (red)
        { 8, "DEST" },   // slot 3 / Destroyer kind + sprite (green)
    }};
    std::array<std::int32_t, 4> atlasIdxByPalette{ -1, -1, -1, -1 };
    if (compositor_ && !assetDir_.empty()) {
        const std::filesystem::path shipsDir = assetDir_ / "ships";
        for (std::uint8_t pi = 0; pi < kAtlasSeeds.size(); ++pi) {
            const std::filesystem::path p =
                shipsDir / (std::string(kAtlasSeeds[pi].shpStem) + ".SHP");
            SpriteAtlas atlas;
            if (loadSpriteAtlas(p, kSlotColors[pi], atlas)) {
                atlasIdxByPalette[pi] = compositor_->addAtlas(std::move(atlas));
            } else {
                std::fprintf(stderr,
                    "[tou2d] sprite atlas missing for palette %u (%s)\n",
                    unsigned(pi), p.string().c_str());
            }
        }
    }

    playerShips_.assign(totalShips, threadmaxx::EntityHandle{});

    // Fallback ring — radius scales with N so 64 ships don't all stack
    // at the origin if random sampling fails (e.g. degenerate grid).
    constexpr float kBaseRingRadius = 40.0f;
    constexpr float kPerShipPad     = 18.0f;
    const float ringR = std::max(
        kBaseRingRadius,
        kPerShipPad * std::sqrt(static_cast<float>(totalShips)));
    constexpr float kTwoPi = 6.28318530718f;

    // Deterministic per-session RNG — different runs produce different
    // openings, but each run's spawn ordering is reproducible if the
    // engine ever exposes a seed. Use a single shared rng so all slots
    // pull from the same sequence (avoids two slots landing at the
    // same cell by xorshift coincidence).
    std::mt19937 spawnRng(0xC0FFEE5Eu);

    for (std::uint32_t slot = 0; slot < totalShips; ++slot) {
        // Ring fallback in case sampleRandomRespawn fails (synthetic
        // arena with no interior Air, malformed grid).
        const float t     = static_cast<float>(slot) /
                            static_cast<float>(totalShips);
        const float theta = t * kTwoPi;
        float x = std::cos(theta) * ringR;
        float y = std::sin(theta) * ringR;
        sampleRandomRespawn(grid_, spawnRng, x, y);

        const bool isBot = slot >= numHumans_;
        const std::uint16_t kindIdx =
            kAtlasSeeds[slot % kAtlasSeeds.size()].kindIdx;
        const std::int32_t atlasIdx =
            atlasIdxByPalette[slot % atlasIdxByPalette.size()];
        playerShips_[slot] = spawnShip(
            engine, seed, ids_,
            static_cast<std::uint8_t>(slot), x, y,
            isBot ? std::uint8_t{1} : std::uint8_t{0},
            kindIdx, atlasIdx, defaultSpecial_);
    }
}

void TouGame::onTeardown(threadmaxx::Engine& /*engine*/,
                         threadmaxx::World&  /*world*/) {
    camera_        = nullptr;
    bulletTerrain_ = nullptr;
    collision_     = nullptr;
    repairPickup_  = nullptr;
    input_         = nullptr;
}

void TouGame::setTileDestroyCallback(TileDestroyCallback cb) {
    if (bulletTerrain_) bulletTerrain_->setDestroyCallback(cb);
    if (repairPickup_)  repairPickup_ ->setDestroyCallback(cb);
    if (collision_)     collision_    ->setDestroyCallback(std::move(cb));
}

} // namespace tou2d
