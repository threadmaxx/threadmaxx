#include "DemoGame.hpp"

#include "AnimationSystem.hpp"
#include "CameraSystem.hpp"
#include "CombatSystem.hpp"
#include "CubeRenderSystem.hpp"
#include "DamageSystem.hpp"
#include "PreloadLoader.hpp"
#include "QuestSystem.hpp"
#include "DayNightSystem.hpp"
#include "DebugOverlaySystem.hpp"
#include "HealthBarSystem.hpp"
#include "HudSystem.hpp"
#include "MovementSystem.hpp"
#include "NPCBrainSystem.hpp"
#include "PickupSystem.hpp"
#include "PlayerInputSystem.hpp"
#include "RespawnSystem.hpp"
#include "SaveLoadSystem.hpp"

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Components.hpp>
#include <threadmaxx/Engine.hpp>
#include <threadmaxx/System.hpp>
#include <threadmaxx/UserComponent.hpp>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <random>
#include <utility>

namespace rpg {

namespace {

// §3.11.5 batch D5 — stress mode replaces these defaults via
// `worldState_.stressMode` set by main.cpp from `--stress`. See
// `kStressNpcCount` / `kStressPickupCount` in DemoTypes.hpp.
constexpr float         kPlayWidth   = 60.0f;

threadmaxx::BoundingVolume cubeAABB(const threadmaxx::Vec3& center, float halfSize) {
    return threadmaxx::BoundingVolume{
        {center.x - halfSize, center.y - halfSize, center.z - halfSize},
        {center.x + halfSize, center.y + halfSize, center.z + halfSize},
    };
}

} // namespace

DemoGame::DemoGame() = default;
DemoGame::~DemoGame() = default;

void DemoGame::onSetup(threadmaxx::Engine& engine,
                       threadmaxx::World&,
                       threadmaxx::CommandBuffer& seed) {
    // ---- Register user-components -------------------------------------------
    ids_.cubeRender  = engine.registerUserComponent<CubeRender>();
    ids_.npcState    = engine.registerUserComponent<NpcState>();
    ids_.playerState = engine.registerUserComponent<PlayerState>();
    ids_.pickup      = engine.registerUserComponent<Pickup>();
    ids_.swordTag    = engine.registerUserComponent<SwordTag>();
    ids_.animState   = engine.registerUserComponent<AnimState>();

    // §3.11.7 batch D7 — register the simulated boot-time loader.
    // The engine pumps `update()` once per `step()`; `preloadUntil`
    // below blocks until `loader->allDone()` flips. Loader is engine-
    // owned but we keep a raw pointer for the HUD to query stats.
    auto* preload = static_cast<PreloadLoader*>(
        engine.addResourceLoader(std::make_unique<PreloadLoader>()));

    // §3.11.7 batch D7 — block boot until the simulated assets finish
    // loading. `preloadUntil` pumps every registered loader's
    // `update()` in a yield loop. With kAssetCount=64 + kPerTick=4
    // that's ~16 pumps; the engine ticks once per pump so the
    // simulation doesn't advance. Falls back after 5s if something
    // is wedged.
    {
        const auto preloadStart = std::chrono::steady_clock::now();
        const bool ok = engine.preloadUntil(
            [preload]() noexcept { return preload->allDone(); },
            std::chrono::milliseconds(5000));
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - preloadStart);
        const auto stats = engine.aggregateLoaderStats();
        std::printf("[preload] done=%d ready=%llu pending=%llu memMiB=%.1f elapsed=%lldms\n",
                    int(ok),
                    static_cast<unsigned long long>(stats.ready),
                    static_cast<unsigned long long>(stats.pendingLoads),
                    static_cast<double>(stats.memoryFootprint) / (1024.0 * 1024.0),
                    static_cast<long long>(elapsed.count()));
    }

    // §3.11.1 batch D1 — pre-warm typed event channels on the sim
    // thread so worker emits don't pay the eventChannelsMtx_ insert
    // contention on first-use from a non-sim thread. Matches the
    // documented "warm channels at setup" pattern.
    (void)engine.events<PickupCollected>();
    (void)engine.events<DamageDealt>();
    (void)engine.events<EntityDied>();
    (void)engine.events<threadmaxx::SystemSkipped>();
    (void)engine.events<threadmaxx::BudgetExceeded>();
    (void)engine.events<QuestProgressed>();

    // §3.11.5 batch D5 — choose spawn counts. `worldState_.stressMode`
    // is set by main.cpp from the `--stress` CLI flag (default false).
    worldState_.npcCount    = worldState_.stressMode ? kStressNpcCount
                                                     : kNormalNpcCount;
    worldState_.pickupCount = worldState_.stressMode ? kStressPickupCount
                                                     : kNormalPickupCount;

    // §3.11.5 batch D5 — enable the tick budget + skip policy when
    // stress mode is on. The cosmetic systems (HUD / DebugOverlay /
    // DayNight) declare `skippable() = true`; the engine emits
    // `SystemSkipped` events on the typed channel when a wave
    // exceeds the budget, and HudSystem aggregates those for display.
    if (worldState_.stressMode) {
        engine.setTickBudget(kTickBudgetSeconds);
        engine.setSkipPolicy(threadmaxx::SkipPolicy::Budget);
    }

    // ---- Reserve handles up front -------------------------------------------
    // We need the player's handle before commits to stash in WorldState.
    const auto playerH = engine.reserveEntityHandle();
    worldState_.player = playerH;

    // ---- Player -------------------------------------------------------------
    {
        threadmaxx::Bundle b = {};
        b.transform.position = {0, 1, 0};
        b.transform.scale    = {1.0f, 1.8f, 1.0f};
        b.faction.id         = kFactionPlayer;
        b.boundingVolume     = cubeAABB({0, 1, 0}, 0.9f);
        b.health             = threadmaxx::Health{kPlayerMaxHP, kPlayerMaxHP};
        b.initialMask        = threadmaxx::ComponentSet{
            threadmaxx::Component::Transform,
            threadmaxx::Component::Velocity,
            threadmaxx::Component::Faction,
            threadmaxx::Component::BoundingVolume,
            threadmaxx::Component::Health,
        };
        seed.spawnBundle(playerH, b);

        threadmaxx::addUserComponent(seed, ids_.cubeRender, playerH,
            CubeRender{{0.30f, 0.50f, 1.0f, 1.0f}, 1.0f});
        threadmaxx::addUserComponent(seed, ids_.playerState, playerH, PlayerState{});
        // §3.11.6 batch D6 — player walk-bob.
        threadmaxx::addUserComponent(seed, ids_.animState, playerH,
            AnimState{/*baseY*/ 1.0f, /*phase*/ 0.0f,
                      /*freq*/ 8.0f, /*amp*/ 0.10f, 0.0f});
    }

    // ---- Sword (player's child, propagated by HierarchySystem) --------------
    // §3.11.1 batch D1: demonstrates the Parent component + hierarchy
    // propagation against a real game asset. The sword's local offset
    // hangs in front of and slightly to the right of the player; the
    // HierarchySystem composes player.world × sword.local each tick.
    {
        const auto swordH = engine.reserveEntityHandle();
        worldState_.sword = swordH;
        threadmaxx::Parent p;
        p.parent              = playerH;
        p.localOffset.position = {0.5f, 0.8f, -0.8f};  // ~hip-front
        p.localOffset.scale    = {0.18f, 0.18f, 1.4f};
        threadmaxx::Bundle b = {};
        b.transform.position  = {0, 1, 0};  // overwritten by hierarchy
        b.transform.scale     = {0.18f, 0.18f, 1.4f};
        b.parent              = p;
        b.boundingVolume      = cubeAABB({0, 1, 0}, 0.3f);
        b.initialMask         = threadmaxx::ComponentSet{
            threadmaxx::Component::Transform,
            threadmaxx::Component::Parent,
            threadmaxx::Component::BoundingVolume,
        };
        seed.spawnBundle(swordH, b);
        threadmaxx::addUserComponent(seed, ids_.cubeRender, swordH,
            CubeRender{{0.85f, 0.85f, 0.95f, 1.0f}, 1.0f, {0,0,0}});
        threadmaxx::addUserComponent(seed, ids_.swordTag, swordH,
            SwordTag{1.4f});
    }

    // ---- Terrain ------------------------------------------------------------
    {
        const auto terrainH = engine.reserveEntityHandle();
        threadmaxx::Bundle b = {};
        b.transform.position = {0, -0.5f, 0};
        b.transform.scale    = {kPlayWidth, 0.2f, kPlayWidth};
        b.faction.id         = kFactionNeutral;
        b.boundingVolume     = cubeAABB({0, -0.5f, 0}, kPlayWidth * 0.5f);
        b.initialMask        = threadmaxx::ComponentSet{
            threadmaxx::Component::Transform,
            threadmaxx::Component::Faction,
            threadmaxx::Component::BoundingVolume,
            threadmaxx::Component::StaticTag,
        };
        seed.spawnBundle(terrainH, b);
        threadmaxx::addUserComponent(seed, ids_.cubeRender, terrainH,
            CubeRender{{0.25f, 0.45f, 0.25f, 1.0f}, 1.0f});
    }

    // ---- NPCs ---------------------------------------------------------------
    std::mt19937 rng(0xBEEFCAFEu);
    std::uniform_real_distribution<float> px(-kPlayWidth * 0.45f, kPlayWidth * 0.45f);
    std::uniform_real_distribution<float> hostility(0.0f, 1.0f);
    worldState_.hostileSpawnCount = 0;
    for (std::uint32_t i = 0; i < worldState_.npcCount; ++i) {
        const auto h = engine.reserveEntityHandle();
        const threadmaxx::Vec3 pos{px(rng), 1.0f, px(rng)};
        const bool hostile = hostility(rng) < 0.6f;
        if (hostile) ++worldState_.hostileSpawnCount;
        const std::uint32_t fac = hostile ? kFactionHostile : kFactionFriendly;
        const float hostileColor[4]  = {0.95f, 0.20f, 0.20f, 1.0f};
        const float friendlyColor[4] = {0.30f, 0.85f, 0.40f, 1.0f};
        const float* color = hostile ? hostileColor : friendlyColor;

        threadmaxx::Bundle b = {};
        b.transform.position = pos;
        b.transform.scale    = {0.8f, 1.6f, 0.8f};
        b.faction.id         = fac;
        b.boundingVolume     = cubeAABB(pos, 0.8f);
        b.health             = threadmaxx::Health{
            hostile ? kHostileMaxHP : kFriendlyMaxHP,
            hostile ? kHostileMaxHP : kFriendlyMaxHP};
        b.initialMask        = threadmaxx::ComponentSet{
            threadmaxx::Component::Transform,
            threadmaxx::Component::Velocity,
            threadmaxx::Component::Faction,
            threadmaxx::Component::BoundingVolume,
            threadmaxx::Component::Health,
        };
        seed.spawnBundle(h, b);

        CubeRender cr;
        cr.color[0] = color[0];
        cr.color[1] = color[1];
        cr.color[2] = color[2];
        cr.color[3] = color[3];
        cr.scale = 1.0f;
        threadmaxx::addUserComponent(seed, ids_.cubeRender, h, cr);

        NpcState st;
        st.aoiRadius = hostile ? 7.0f : 4.0f;
        threadmaxx::addUserComponent(seed, ids_.npcState, h, st);

        // §3.11.6 batch D6 — give each NPC a unique phase so the
        // group bobs out of sync. The phase is derived from the
        // spawn index — deterministic across runs.
        AnimState anim;
        anim.baseY     = pos.y;
        anim.phase     = static_cast<float>(i) * 0.31f;
        anim.frequency = hostile ? 7.0f : 5.0f;
        anim.amplitude = 0.20f;
        threadmaxx::addUserComponent(seed, ids_.animState, h, anim);
    }

    // ---- Pickups ------------------------------------------------------------
    for (std::uint32_t i = 0; i < worldState_.pickupCount; ++i) {
        const auto h = engine.reserveEntityHandle();
        const threadmaxx::Vec3 pos{px(rng), 0.4f, px(rng)};

        threadmaxx::Bundle b = {};
        b.transform.position = pos;
        b.transform.scale    = {0.4f, 0.4f, 0.4f};
        b.faction.id         = kFactionNeutral;
        b.boundingVolume     = cubeAABB(pos, 0.4f);
        b.initialMask        = threadmaxx::ComponentSet{
            threadmaxx::Component::Transform,
            threadmaxx::Component::Faction,
            threadmaxx::Component::BoundingVolume,
        };
        seed.spawnBundle(h, b);

        threadmaxx::addUserComponent(seed, ids_.cubeRender, h,
            CubeRender{{1.0f, 0.85f, 0.20f, 1.0f}, 0.7f});
        threadmaxx::addUserComponent(seed, ids_.pickup, h, Pickup{1u});
    }

    // §3.11.4 batch D4 — seed the two quests. Targets resolve from
    // the just-counted hostile spawn count + the pickup-quest
    // constant.
    worldState_.quests.clear();
    worldState_.quests.push_back(QuestState{
        QuestId::CollectPickups, 0u, kPickupQuestTarget, false});
    worldState_.quests.push_back(QuestState{
        QuestId::KillHostiles, 0u,
        worldState_.hostileSpawnCount, false});

    // ---- Systems -----------------------------------------------------------
    // Engine takes ownership of each; we cache one raw pointer for the
    // brain so PickupSystem can read the spatial hash it owns.
    auto brain = std::make_unique<NPCBrainSystem>(&worldState_, &ids_);
    brain_ = brain.get();
    engine.registerSystem(std::move(brain));

    engine.registerSystem(std::make_unique<PlayerInputSystem>(&worldState_, &ids_));
    engine.registerSystem(std::make_unique<CameraSystem>(&worldState_, &ids_));
    engine.registerSystem(std::make_unique<MovementSystem>());
    // §3.11.6 batch D6 — Y-bob animation runs AFTER MovementSystem
    // (X/Z integrated from Velocity) and BEFORE HierarchySystem
    // (so Parent-attached children inherit the bobbed Y).
    engine.registerSystem(std::make_unique<AnimationSystem>(&ids_));
    // §3.11.1 batch D1 — hierarchy propagates the sword's world
    // transform AFTER MovementSystem updates the player's position.
    // CombatSystem reads the propagated sword transform, so its
    // registration must come after the hierarchy.
    engine.registerSystem(threadmaxx::makeHierarchySystem());
    engine.registerSystem(std::make_unique<CombatSystem>(
        &engine, &worldState_, &ids_, brain_));
    engine.registerSystem(std::make_unique<DamageSystem>(&engine, &ids_));
    engine.registerSystem(std::make_unique<RespawnSystem>(
        &engine, &worldState_, &ids_));
    engine.registerSystem(std::make_unique<PickupSystem>(&engine, &worldState_, &ids_, brain_));
    // §3.11.4 batch D4 — quest tracker. Registered AFTER Pickup +
    // Damage so its event subscriptions see the PickupCollected /
    // EntityDied events those systems emit in the same tick.
    engine.registerSystem(std::make_unique<QuestSystem>(&engine, &worldState_));
    engine.registerSystem(std::make_unique<DayNightSystem>(&worldState_));
    engine.registerSystem(std::make_unique<CubeRenderSystem>(&ids_, &worldState_));
    engine.registerSystem(std::make_unique<HealthBarSystem>());
    engine.registerSystem(std::make_unique<DebugOverlaySystem>(&worldState_, &ids_));
    engine.registerSystem(std::make_unique<SaveLoadSystem>(
        &engine, &worldState_, &ids_,
        std::filesystem::path("/tmp/rpg_demo_save.bin")));
    // §3.11.5 batch D5 — `FrameBudgetWatcher` posts `BudgetExceeded`
    // events whenever a tick exceeds the target. Registered AFTER all
    // gameplay systems so its `postStep` observes the just-finished
    // tick's duration. Only registered in stress mode; vanilla play
    // doesn't need the alert and the watcher's per-tick virtual call
    // would be a slight overhead.
    if (worldState_.stressMode) {
        engine.registerSystem(std::make_unique<threadmaxx::FrameBudgetWatcher>(
            &engine, kTickBudgetSeconds));
    }
    engine.registerSystem(std::make_unique<HudSystem>(&engine, &worldState_, &ids_));
}

} // namespace rpg
