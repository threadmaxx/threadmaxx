#pragma once

#include <threadmaxx/Components.hpp>
#include <threadmaxx/Handles.hpp>
#include <threadmaxx/UserComponent.hpp>

#include <cstdint>

namespace rpg {

/// Faction codes, stored in the engine's built-in `Faction.value`.
/// 0 = player, 1 = friendly NPC, 2 = hostile NPC, 3 = neutral / scenery.
enum : std::uint32_t {
    kFactionPlayer   = 0,
    kFactionFriendly = 1,
    kFactionHostile  = 2,
    kFactionNeutral  = 3,
};

/// Per-entity render parameters. Drives `CubeRenderSystem` →
/// `DrawItem::materialOverride.params` + per-instance scale. The Vulkan
/// renderer just multiplies the override into its Lambert shader.
struct CubeRender {
    float color[4] = {0.8f, 0.8f, 0.85f, 1.0f};
    float scale    = 1.0f;
    float pad[3]   = {0.0f, 0.0f, 0.0f};   // pad to 16 bytes
};

/// NPC AI state machine. The brain transitions between states based on
/// the player's distance + a per-state timer.
///
/// §3.11.1 batch D1: extended with `Fight` and `Retreat` states for the
/// combat loop. Hostile NPCs go Wander → Fight when the player is within
/// `aoiRadius`; Fight → Retreat when their `Health.current` drops below
/// 30% of max; Retreat → Wander after `kRetreatSeconds` of running.
struct NpcState {
    enum Mode : std::uint32_t {
        Idle    = 0,
        Wander  = 1,
        Flee    = 2,
        Fight   = 3,
        Retreat = 4,
    };
    std::uint32_t mode       = Idle;
    float         stateTimer = 0.0f;     // seconds since last transition
    float         targetX    = 0.0f;     // wander/flee waypoint
    float         targetZ    = 0.0f;
    /// Awareness radius — the NPC notices the player within this range.
    float         aoiRadius  = 6.0f;
};

/// Player-only metadata.
///
/// §3.11.1 batch D1: added `swordSwingTimer` to drive the attack-window
/// animation. While > 0 the sword's localOffset rotation kicks forward
/// and the CombatSystem checks for hits in front of the player.
struct PlayerState {
    std::uint32_t pickups          = 0;
    float         yawRadians       = 0.0f;
    float         runSpeed         = 5.0f;
    /// Seconds remaining in the current sword swing. Drives the sword's
    /// localOffset rotation and gates the combat damage check.
    float         swordSwingTimer  = 0.0f;
    /// Lifetime damage-dealt counter; surfaced in the HUD.
    std::uint32_t kills            = 0;
};

/// Pickup tag — collected via spatial-hash overlap with the player.
struct Pickup {
    std::uint32_t value = 1;  // score added on collect
};

/// §3.11.1 batch D1: marker for the sword child entity. The sword is a
/// Parent-attached child of the player; its transform is propagated by
/// the engine's HierarchySystem each tick. Game code only stores its
/// length so the CombatSystem can compute the tip world position.
struct SwordTag {
    float length = 1.4f;  // local Z offset from the hilt at the player
};

/// One pickup event drained by the HUD subscriber on the next tick.
struct PickupCollected {
    threadmaxx::EntityHandle pickup;
    threadmaxx::EntityHandle player;
    std::uint32_t            value;
    std::uint32_t            totalPickups;  // post-increment
};

/// §3.11.1 batch D1: emitted by CombatSystem whenever the sword tip
/// overlaps a hostile NPC. Subscribed by DamageSystem, which applies
/// `Health.current -= amount` and emits `EntityDied` on the kill blow.
struct DamageDealt {
    threadmaxx::EntityHandle attacker;
    threadmaxx::EntityHandle target;
    float                    amount;
    /// Hit position in world space; used by the floating damage-text
    /// overlay (DebugText) so the number pops above the victim.
    float                    posX = 0.0f;
    float                    posY = 0.0f;
    float                    posZ = 0.0f;
};

/// §3.11.1 batch D1: emitted by DamageSystem when an entity's HP hits
/// zero. Subscribed by RespawnSystem (drops a gold pickup) and HudSystem
/// (kill counter).
struct EntityDied {
    threadmaxx::EntityHandle entity;
    threadmaxx::EntityHandle killer;
    /// Position at the moment of death — used to spawn the pickup drop.
    float                    posX = 0.0f;
    float                    posY = 0.0f;
    float                    posZ = 0.0f;
};

/// Bundle of user-component ids. Registered once at startup; passed to
/// every system that needs to read or write a UserComponent. Stored in
/// the engine's resource registry so systems can fetch it lazily without
/// constructor injection.
struct UserComponentIds {
    threadmaxx::UserComponentId cubeRender;
    threadmaxx::UserComponentId npcState;
    threadmaxx::UserComponentId playerState;
    threadmaxx::UserComponentId pickup;
    /// §3.11.1 batch D1 — marks the player's attached sword.
    threadmaxx::UserComponentId swordTag;
};

/// Game-wide state shared across systems via the resource registry.
/// Mutable from the simulation thread only.
struct WorldState {
    threadmaxx::EntityHandle player = {};
    /// §3.11.1 batch D1 — child entity of the player; transform is
    /// propagated by the engine's HierarchySystem.
    threadmaxx::EntityHandle sword  = {};
    /// Sun angle in radians; advanced by DayNightSystem each tick.
    float sunAngle = 1.2f;
    /// Day-night cycle length in seconds (one full rotation).
    float dayLengthSeconds = 120.0f;
    /// Pixel framebuffer dimensions, updated by the GLFW resize callback.
    std::uint32_t framebufferWidth  = 1280;
    std::uint32_t framebufferHeight = 720;
    /// §3.11.1 batch D1 — cumulative kill count, surfaced in the HUD.
    std::uint32_t totalKills = 0;
};

/// §3.11.1 batch D1 — gameplay tuning constants.
constexpr float kSwordSwingSeconds  = 0.30f;
constexpr float kSwordDamage        = 25.0f;
constexpr float kSwordTipRadius     = 0.7f;
constexpr float kPlayerMaxHP        = 100.0f;
constexpr float kHostileMaxHP       =  60.0f;
constexpr float kFriendlyMaxHP      =  80.0f;

} // namespace rpg
