#include "DemoGame.hpp"

#include "CameraSystem.hpp"
#include "CombatSystem.hpp"
#include "CubeRenderSystem.hpp"
#include "DamageSystem.hpp"
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

#include <cstdint>
#include <filesystem>
#include <memory>
#include <random>
#include <utility>

namespace rpg {

namespace {

constexpr std::uint32_t kNPCCount    = 50;
constexpr std::uint32_t kPickupCount = 100;
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

    // §3.11.1 batch D1 — pre-warm typed event channels on the sim
    // thread so worker emits don't pay the eventChannelsMtx_ insert
    // contention on first-use from a non-sim thread. Matches the
    // documented "warm channels at setup" pattern.
    (void)engine.events<PickupCollected>();
    (void)engine.events<DamageDealt>();
    (void)engine.events<EntityDied>();

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
    for (std::uint32_t i = 0; i < kNPCCount; ++i) {
        const auto h = engine.reserveEntityHandle();
        const threadmaxx::Vec3 pos{px(rng), 1.0f, px(rng)};
        const bool hostile = hostility(rng) < 0.6f;
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
    }

    // ---- Pickups ------------------------------------------------------------
    for (std::uint32_t i = 0; i < kPickupCount; ++i) {
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

    // ---- Systems -----------------------------------------------------------
    // Engine takes ownership of each; we cache one raw pointer for the
    // brain so PickupSystem can read the spatial hash it owns.
    auto brain = std::make_unique<NPCBrainSystem>(&worldState_, &ids_);
    brain_ = brain.get();
    engine.registerSystem(std::move(brain));

    engine.registerSystem(std::make_unique<PlayerInputSystem>(&worldState_, &ids_));
    engine.registerSystem(std::make_unique<CameraSystem>(&worldState_, &ids_));
    engine.registerSystem(std::make_unique<MovementSystem>());
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
    engine.registerSystem(std::make_unique<DayNightSystem>(&worldState_));
    engine.registerSystem(std::make_unique<CubeRenderSystem>(&ids_));
    engine.registerSystem(std::make_unique<HealthBarSystem>());
    engine.registerSystem(std::make_unique<DebugOverlaySystem>(&worldState_, &ids_));
    engine.registerSystem(std::make_unique<SaveLoadSystem>(
        &worldState_, &ids_, std::filesystem::path("/tmp/rpg_demo_save.bin")));
    engine.registerSystem(std::make_unique<HudSystem>(&engine, &worldState_, &ids_));
}

} // namespace rpg
