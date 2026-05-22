#include "DemoGame.hpp"

#include "AnimationSystem.hpp"
#include "CameraSystem.hpp"
#include "CombatSystem.hpp"
#include "CubeRenderSystem.hpp"
#include "DamageSystem.hpp"
#include "Heightmap.hpp"
#include "ObjLoader.hpp"
#include "PreloadLoader.hpp"
#include "QuestSystem.hpp"
#include "DayNightSystem.hpp"
#include "DebugOverlaySystem.hpp"
#include "HealthBarSystem.hpp"
#include "HudSystem.hpp"
#include "MovementSystem.hpp"
#include "NPCBrainSystem.hpp"
#include "ParticleEmitterSystem.hpp"
#include "ParticleSystem.hpp"
#include "PickupSystem.hpp"
#include "PlayerInputSystem.hpp"
#include "RespawnSystem.hpp"
#include "SaveLoadSystem.hpp"
#include "SkinnedRenderSystem.hpp"
#include "TerrainAttachSystem.hpp"

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
    ids_.cubeRender   = engine.registerUserComponent<CubeRender>();
    ids_.npcState     = engine.registerUserComponent<NpcState>();
    ids_.playerState  = engine.registerUserComponent<PlayerState>();
    ids_.pickup       = engine.registerUserComponent<Pickup>();
    ids_.swordTag     = engine.registerUserComponent<SwordTag>();
    ids_.animState    = engine.registerUserComponent<AnimState>();
    ids_.terrainPatch = engine.registerUserComponent<TerrainPatch>();
    ids_.particle     = engine.registerUserComponent<Particle>();
    ids_.particleEmitter = engine.registerUserComponent<ParticleEmitter>();

    // §3.11.8 batch D8 — generate the heightmap once at boot. The
    // resolution is fixed; only the tile count (i.e. how densely the
    // continuous field is sampled into entities) scales with stress
    // mode.
    if (!worldState_.heightmap) {
        worldState_.heightmap = std::make_shared<Heightmap>(
            kHeightmapResolution, kTerrainExtent, kHeightmapSeed);
    }

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

    // §3.11 batch 9b.2b — register the per-entity-class meshes via the
    // renderer-bound callback main.cpp installed. The pyramid asset
    // ships in `examples/rpg_demo/assets/pyramid.obj`; pickups draw
    // with it when the registration succeeds. Headless tests (callback
    // = null) leave `pickupMeshId = 0` and pickups fall back to the
    // default cube — same behavior as pre-9b.2b. Parser failures and
    // upload failures both fall through silently to the cube path.
    if (registerMeshFn_) {
        const std::string pyramidPath = std::string(RPG_DEMO_SOURCE_DIR) +
                                        "/assets/pyramid.obj";
        const auto parsed = parseObjFile(pyramidPath);
        if (parsed.ok) {
            const std::int32_t mid = registerMeshFn_(
                std::span<const float>(parsed.mesh.vertices),
                std::span<const std::uint16_t>(parsed.mesh.indices));
            if (mid > 0) {
                worldState_.pickupMeshId = mid;
                std::printf("[demo] pyramid asset: corners=%u meshId=%d\n",
                            parsed.mesh.cornerCount, mid);
            } else {
                std::printf("[demo] pyramid asset: upload failed; pickups fall back to cube\n");
            }
        } else {
            std::printf("[demo] pyramid asset: parse failed (%s); pickups fall back to cube\n",
                        parsed.error.c_str());
        }
    }

    // §3.11.5 batch D5 — choose spawn counts. `worldState_.stressMode`
    // is set by main.cpp from the `--stress` CLI flag (default false).
    worldState_.npcCount    = worldState_.stressMode ? kStressNpcCount
                                                     : kNormalNpcCount;
    worldState_.pickupCount = worldState_.stressMode ? kStressPickupCount
                                                     : kNormalPickupCount;
    // §3.11.8 batch D8 — terrain tile count. Tests are allowed to
    // pre-seed `terrainCellsPerSide` themselves; if they don't, the
    // default of 0 here means we pick from stress mode like the other
    // counts.
    if (worldState_.terrainCellsPerSide == 0) {
        worldState_.terrainCellsPerSide = worldState_.stressMode
            ? kStressTerrainCellsPerSide
            : kNormalTerrainCellsPerSide;
    }

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

    // §3.11.8 batch D8 — heightmap-aware initial Y. The player stands
    // on top of the terrain at world origin. Half-scale.y is the
    // half-height that lifts the cube off the ground so its base
    // touches `heightAt(x, z)`.
    //
    // 2026-05-22 (round 9, voxel pivot) — the player is now exactly
    // 2 blocks tall (`scale.y = 2.0`) so two stacked 1 m³ blocks
    // match player height. halfY = 1 m, so spawning Y at
    // `heightAt + 1` puts the player's feet flush with the top of
    // the column directly underneath.
    const auto& hmap = *worldState_.heightmap;
    const float playerHeight = 2.0f;
    const float playerBaseY  = hmap.heightAt(0.0f, 0.0f) + playerHeight * 0.5f;
    // 2026-05-22 audit (round 2) — capture the spawn position for
    // RespawnSystem. After a player death, RespawnSystem teleports
    // the player back to this exact transform after the respawn
    // delay.
    worldState_.playerSpawnPos = {0.0f, playerBaseY, 0.0f};

    // ---- Player -------------------------------------------------------------
    {
        threadmaxx::Bundle b = {};
        b.transform.position = {0, playerBaseY, 0};
        b.transform.scale    = {1.0f, playerHeight, 1.0f};
        b.faction.id         = kFactionPlayer;
        b.boundingVolume     = cubeAABB({0, 1, 0}, 1.0f);
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
        // §3.11.6 batch D6 — player walk-bob. §3.11.8 batch D8 —
        // `baseY` is now derived from the terrain underneath the
        // player's spawn so the bob oscillates around the ground.
        threadmaxx::addUserComponent(seed, ids_.animState, playerH,
            AnimState{/*baseY*/ playerBaseY, /*phase*/ 0.0f,
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
            CubeRender{{0.85f, 0.85f, 0.95f, 1.0f}, 1.0f});
        threadmaxx::addUserComponent(seed, ids_.swordTag, swordH,
            SwordTag{1.4f});
    }

    // ---- Terrain (voxel grid, round 9) -------------------------------------
    //
    // 2026-05-22 (round 9, voxel pivot) — the terrain is now a true
    // voxel grid. Per (cx, cz) column we spawn ONE 1 m³ block entity
    // per integer height step from y=0.5 up through the column's
    // quantized height. So a column whose `heightAt` returns 5 spawns
    // 5 cubes at y ∈ {0.5, 1.5, 2.5, 3.5, 4.5}. This makes individual
    // blocks visually read as proper cubes (X = Y = Z = 1 m) instead
    // of the pre-round-9 tall-slab look, and the surface block of
    // each column is the one entity the player snaps onto.
    //
    // Heightmap cellSize == block-XZ size == 1 m, so the boundary at
    // which `heightAt` reports a new value sits EXACTLY where the
    // player visually crosses from one block to the next — closing
    // the round-9 "block changes height in the middle" complaint.
    //
    // Blocks intentionally do NOT carry `BoundingVolume` — combat /
    // AOI queries skip the static-terrain archetype on the mask
    // test, so leaving the bit off keeps those queries lean.
    //
    // Tile count is `cellsPerSide² × avgColumnHeight`. With cells=48
    // and heights in [0, 12] block units (average ~6), the demo
    // spawns ~14 000 terrain cubes. Headless tests can pre-set
    // `terrainCellsPerSide` to keep boot fast.
    const float        blockUnit  = hmap.blockUnit();
    const std::uint32_t cells     = worldState_.terrainCellsPerSide;
    const float terrainExtent     = kTerrainExtent;
    const float tileSize          = terrainExtent / static_cast<float>(cells);
    const float halfExtent        = terrainExtent * 0.5f;
    const float heightRange       = hmap.maxHeight() - hmap.minHeight();
    for (std::uint32_t cz = 0; cz < cells; ++cz) {
        for (std::uint32_t cx = 0; cx < cells; ++cx) {
            const float worldX = -halfExtent + (static_cast<float>(cx) + 0.5f) * tileSize;
            const float worldZ = -halfExtent + (static_cast<float>(cz) + 0.5f) * tileSize;
            const float topY   = hmap.heightAt(worldX, worldZ);  // quantized
            const std::uint32_t blockCount = static_cast<std::uint32_t>(
                std::max(0.0f, std::round(topY / blockUnit)));
            if (blockCount == 0) continue;  // sea level — no surface
            const float colorT = heightRange > 1e-3f
                ? (topY - hmap.minHeight()) / heightRange : 0.5f;
            // 3-stop gradient: grass → rock → snow.
            const float r = 0.20f + colorT * 0.60f;
            const float g = 0.45f - colorT * 0.20f;
            const float b = 0.25f + colorT * 0.40f;

            for (std::uint32_t k = 0; k < blockCount; ++k) {
                const float blockY = (static_cast<float>(k) + 0.5f) * blockUnit;
                const auto blockH = engine.reserveEntityHandle();
                threadmaxx::Bundle bd = {};
                bd.transform.position = {worldX, blockY, worldZ};
                bd.transform.scale    = {tileSize, blockUnit, tileSize};
                bd.faction.id         = kFactionNeutral;
                bd.initialMask        = threadmaxx::ComponentSet{
                    threadmaxx::Component::Transform,
                    threadmaxx::Component::Faction,
                    threadmaxx::Component::StaticTag,
                };
                seed.spawnBundle(blockH, bd);
                threadmaxx::addUserComponent(seed, ids_.cubeRender, blockH,
                    CubeRender{{r, g, b, 1.0f}, 1.0f});
                threadmaxx::addUserComponent(seed, ids_.terrainPatch, blockH,
                    TerrainPatch{cx, cz});
            }
        }
    }
    (void)kPlayWidth;  // pre-D8 constant retained for diff readability.

    // ---- NPCs ---------------------------------------------------------------
    //
    // 2026-05-22 audit (round 3) — spread spawns across the full
    // playable terrain. The legacy `kPlayWidth * 0.45` (±27m) clustered
    // every entity within a tiny dot at world origin while the terrain
    // itself spans ±128m — the rest of the map looked uninhabited.
    //
    // To keep gameplay engaging from tick 0 (and to keep the npc-brain
    // test deterministic), we seed the FIRST ~kStarterClusterFrac of
    // NPCs/pickups in a tight ring around the player and spread the
    // rest across the full terrain. The starter cluster guarantees at
    // least a handful of hostiles inside the player's AoI on boot;
    // the broad spread fills the rest of the map.
    // 2026-05-22 (round 9, voxel pivot) — terrain shrank to 48 m
    // half-extent = 24 m. The starter ring now sits at ~half the
    // playable area; the broad spread covers the full terrain.
    const float     kStarterHalfExtent = std::min(12.0f,
                                                  kTerrainExtent * 0.25f);
    const float     kBroadHalfExtent   = kTerrainExtent * 0.45f; // ~21.6m
    constexpr float kStarterClusterFrac = 0.30f;                 // 30% near player
    std::mt19937 rng(0xBEEFCAFEu);
    std::uniform_real_distribution<float> pxStarter(-kStarterHalfExtent, kStarterHalfExtent);
    std::uniform_real_distribution<float> pxBroad(-kBroadHalfExtent, kBroadHalfExtent);
    std::uniform_real_distribution<float> hostility(0.0f, 1.0f);
    auto sampleSpawnXZ = [&](std::uint32_t i, std::uint32_t total) {
        const std::uint32_t starterCount =
            static_cast<std::uint32_t>(static_cast<float>(total) * kStarterClusterFrac);
        if (i < starterCount) {
            return std::pair<float, float>(pxStarter(rng), pxStarter(rng));
        }
        return std::pair<float, float>(pxBroad(rng), pxBroad(rng));
    };
    worldState_.hostileSpawnCount = 0;
    constexpr float kNpcHalfHeight = 0.8f;  // matches scale.y/2 below
    for (std::uint32_t i = 0; i < worldState_.npcCount; ++i) {
        const auto h = engine.reserveEntityHandle();
        // §3.11.8 batch D8 — snap initial NPC Y to terrain. Movement
        // re-snaps each tick via TerrainAttachSystem; this is only
        // here so the very first render frame doesn't show NPCs
        // floating at the pre-D8 default of y=1.
        const auto [npcX, npcZ] = sampleSpawnXZ(i, worldState_.npcCount);
        const float npcY = hmap.heightAt(npcX, npcZ) + kNpcHalfHeight;
        const threadmaxx::Vec3 pos{npcX, npcY, npcZ};
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
        // 2026-05-20 — deterministic per-NPC retreat disposition.
        // The brain checks `fleeRoll < kRetreatChance` to decide
        // whether a low-HP hostile retreats or fights to the death.
        st.fleeRoll  = hostility(rng);
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
    constexpr float kPickupHalfHeight = 0.2f;
    for (std::uint32_t i = 0; i < worldState_.pickupCount; ++i) {
        const auto h = engine.reserveEntityHandle();
        // §3.11.8 batch D8 — pickup Y also follows the terrain.
        // Pickups are static after spawn, so this is a one-shot snap
        // rather than a per-tick `TerrainAttachSystem` consumer.
        const auto [pkX, pkZ] = sampleSpawnXZ(i, worldState_.pickupCount);
        const float pkY = hmap.heightAt(pkX, pkZ) + kPickupHalfHeight;
        const threadmaxx::Vec3 pos{pkX, pkY, pkZ};

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

        {
            CubeRender cr{{1.0f, 0.85f, 0.20f, 1.0f}, 0.7f};
            cr.meshId = worldState_.pickupMeshId;
            threadmaxx::addUserComponent(seed, ids_.cubeRender, h, cr);
        }
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
    auto brain = std::make_unique<NPCBrainSystem>(&engine, &worldState_, &ids_);
    brain_ = brain.get();
    engine.registerSystem(std::move(brain));

    engine.registerSystem(std::make_unique<PlayerInputSystem>(&worldState_, &ids_));
    engine.registerSystem(std::make_unique<CameraSystem>(&worldState_, &ids_));
    engine.registerSystem(std::make_unique<MovementSystem>());
    // §3.11.8 batch D8 — snap movers to terrain Y between MovementSystem
    // (which integrates X/Z) and AnimationSystem (which bobs Y). The
    // bob then oscillates around the just-applied terrain Y rather
    // than the stale `AnimState::baseY` from spawn time.
    engine.registerSystem(std::make_unique<TerrainAttachSystem>(
        &worldState_, &ids_, &engine));
    // §3.11.6 batch D6 — Y-bob animation runs AFTER TerrainAttachSystem
    // and BEFORE HierarchySystem (so Parent-attached children inherit
    // the bobbed Y).
    engine.registerSystem(std::make_unique<AnimationSystem>(&ids_, &worldState_));
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
    // §3.11.9 batch D9 — particle aging + burst emission. ParticleSystem
    // (destroys expired entries) runs first; ParticleEmitterSystem (drains
    // last-tick DamageDealt / EntityDied / PickupCollected and spawns new
    // bursts) runs after Damage / Pickup so the channels have settled
    // through one full tick before it consumes them.
    engine.registerSystem(std::make_unique<ParticleSystem>(&engine, &ids_));
    engine.registerSystem(std::make_unique<ParticleEmitterSystem>(&engine, &ids_));
    // §3.11.4 batch D4 — quest tracker. Registered AFTER Pickup +
    // Damage so its event subscriptions see the PickupCollected /
    // EntityDied events those systems emit in the same tick.
    engine.registerSystem(std::make_unique<QuestSystem>(&engine, &worldState_));
    engine.registerSystem(std::make_unique<DayNightSystem>(&worldState_));
    engine.registerSystem(std::make_unique<CubeRenderSystem>(&ids_, &worldState_));
    // §3.11.7b.5 batch 9b.4.c — single hardcoded skinned-capsule
    // entity. Falls silent when `skinnedMeshId == 0` (headless
    // tests + builds where the renderer-side registration callback
    // wasn't wired).
    engine.registerSystem(std::make_unique<SkinnedRenderSystem>(&worldState_));
    engine.registerSystem(std::make_unique<HealthBarSystem>(&worldState_, &ids_));
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
    engine.registerSystem(std::make_unique<HudSystem>(
        &engine, &worldState_, &ids_, reloadShadersFn_));
}

} // namespace rpg
