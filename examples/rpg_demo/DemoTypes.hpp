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
struct NpcState {
    enum Mode : std::uint32_t {
        Idle   = 0,
        Wander = 1,
        Flee   = 2,
    };
    std::uint32_t mode       = Idle;
    float         stateTimer = 0.0f;     // seconds since last transition
    float         targetX    = 0.0f;     // wander/flee waypoint
    float         targetZ    = 0.0f;
    /// Awareness radius — the NPC notices the player within this range.
    float         aoiRadius  = 6.0f;
};

/// Player-only metadata.
struct PlayerState {
    std::uint32_t pickups        = 0;
    float         yawRadians     = 0.0f;
    float         runSpeed       = 5.0f;
};

/// Pickup tag — collected via spatial-hash overlap with the player.
struct Pickup {
    std::uint32_t value = 1;  // score added on collect
};

/// One pickup event drained by the HUD subscriber on the next tick.
struct PickupCollected {
    threadmaxx::EntityHandle pickup;
    threadmaxx::EntityHandle player;
    std::uint32_t            value;
    std::uint32_t            totalPickups;  // post-increment
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
};

/// Game-wide state shared across systems via the resource registry.
/// Mutable from the simulation thread only.
struct WorldState {
    threadmaxx::EntityHandle player = {};
    /// Sun angle in radians; advanced by DayNightSystem each tick.
    float sunAngle = 1.2f;
    /// Day-night cycle length in seconds (one full rotation).
    float dayLengthSeconds = 120.0f;
    /// Pixel framebuffer dimensions, updated by the GLFW resize callback.
    std::uint32_t framebufferWidth  = 1280;
    std::uint32_t framebufferHeight = 720;
};

} // namespace rpg
