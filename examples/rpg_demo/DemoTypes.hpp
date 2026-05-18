#pragma once

#include <threadmaxx/Components.hpp>
#include <threadmaxx/Handles.hpp>
#include <threadmaxx/UserComponent.hpp>
#include <threadmaxx/render/Camera.hpp>

#include <cstdint>
#include <vector>

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
///
/// §3.11 batch 9b.2b — added `meshId`. The default (0) selects the
/// renderer's default mesh (the unit-cube replacement loaded from
/// `assets/cube.obj`). Positive values select a renderer-registered
/// mesh slot via `VulkanRenderer::registerMesh*`. Changing this
/// field's layout bumped `kRpgSaveVersion` (see `SaveLoadSystem.cpp`);
/// pre-9b.2b saves are rejected at load time.
struct CubeRender {
    float    color[4] = {0.8f, 0.8f, 0.85f, 1.0f};
    float    scale    = 1.0f;
    std::int32_t meshId = 0;
    float    pad[2]   = {0.0f, 0.0f};   // pad to 32 bytes
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

/// §3.11.4 batch D4: persistent quest state. Two slots are seeded in
/// `DemoGame::onSetup`:
///   [0] "Collect 25 pickups"      (target = 25)
///   [1] "Defeat all hostiles"     (target = hostile-NPC spawn count)
/// `QuestSystem` updates `progress` from event subscribes and emits
/// `QuestProgressed` whenever it advances (with `completed = true` on
/// the final tick).
enum class QuestId : std::uint32_t {
    CollectPickups = 0,
    KillHostiles   = 1,
};

struct QuestState {
    QuestId       id        = QuestId::CollectPickups;
    std::uint32_t progress  = 0;
    std::uint32_t target    = 0;
    bool          completed = false;
};

struct QuestProgressed {
    QuestId       id;
    std::uint32_t progress;
    std::uint32_t target;
    bool          completed;
};

constexpr std::uint32_t kPickupQuestTarget = 25u;

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
    /// §3.11.6 batch D6 — procedural-animation parameters.
    threadmaxx::UserComponentId animState;
};

/// §3.11.6 batch D6 — procedural animation parameters.
///
/// The library's `AnimationStateRef` + `AnimationPoseRef` engine
/// slots are reserved for **real** skinned-mesh playback (which the
/// Vulkan renderer doesn't yet implement). For the demo we use a
/// cheap procedural Y-bob: when an entity is moving (XZ-speed > a
/// small threshold), `AnimationSystem` modulates `Transform.position.y`
/// by `sin(time * frequency + phase) * amplitude * speedRatio`. Each
/// entity carries its own `phase` so a group of NPCs doesn't bob in
/// lockstep.
///
/// `baseY` is the entity's resting Y position — the bob oscillates
/// around it. Set at spawn time to match the entity's initial Y.
struct AnimState {
    float baseY     = 1.0f;
    float phase     = 0.0f;     // initial offset in radians
    float frequency = 6.0f;     // rad/sec
    float amplitude = 0.18f;    // world units of bob at full speed
    float pad       = 0.0f;     // align to 16 bytes
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
    /// §3.11.2 batch D2 — cameras populated by CameraSystem each tick;
    /// CubeRenderSystem reads them in `buildRenderFrame` to call
    /// `cullByFrustum`. Array order matches the bit position in
    /// `DrawItem::cameraMask`:
    ///   0 = main third-person  (full screen)
    ///   1 = mini-map top-down  (top-right corner)
    ///   2 = aim PIP            (center, only when sword is drawn)
    std::vector<threadmaxx::Camera> activeCameras;

    /// §3.11.5 batch D5 — stress-mode configuration. `stressMode` is
    /// set by main.cpp from the `--stress` CLI flag; DemoGame consults
    /// it during onSetup to scale up the spawn counts. Tests leave it
    /// false. The actual entity counts use the constants below.
    bool          stressMode      = false;
    std::uint32_t npcCount        = 0;   // chosen by DemoGame::onSetup
    std::uint32_t pickupCount     = 0;   // chosen by DemoGame::onSetup
    /// §3.11 batch 9b.2b — meshIds assigned to entity classes when the
    /// renderer's registration callback is wired (main.cpp). Zero
    /// means "fall back to the default cube" (the headless / null-
    /// callback case). Set in `DemoGame::onSetup` before the spawn
    /// loops run.
    std::int32_t  pickupMeshId    = 0;
    /// §3.11.7b.5 batch 9b.4.c — skinned mesh registered by main.cpp
    /// after `engine.initialize`. Zero (or non-positive) means the
    /// renderer's skinning path isn't wired (e.g. headless tests, or
    /// a build where `registerSkinnedMeshFromData` returned -1) and
    /// `SkinnedRenderSystem` falls silent.
    std::int32_t  skinnedMeshId   = 0;
    /// §3.11.5 batch D5 — `FrameBudgetWatcher` reports per-tick alerts
    /// on this counter; HudSystem surfaces it.
    std::uint32_t budgetExceededCount = 0;
    /// §3.11.5 batch D5 — `SystemSkipped` event drain bumps these so
    /// HudSystem can report skip rates per cosmetic system. Indexed by
    /// system name (free-form string).
    std::uint32_t totalSkippedHud       = 0;
    std::uint32_t totalSkippedOverlay   = 0;
    std::uint32_t totalSkippedDayNight  = 0;

    /// §3.11.4 batch D4 — active quests. `DemoGame::onSetup` seeds two
    /// entries; `QuestSystem` updates them via event subscriptions.
    std::vector<QuestState> quests;
    /// §3.11.4 batch D4 — hostile NPC count cached at spawn time so
    /// the "Defeat all hostiles" quest knows its target. Filled by
    /// `DemoGame::onSetup`.
    std::uint32_t hostileSpawnCount = 0;
};

/// §3.11.1 batch D1 — gameplay tuning constants.
constexpr float kSwordSwingSeconds  = 0.30f;
constexpr float kSwordDamage        = 25.0f;
constexpr float kSwordTipRadius     = 0.7f;
constexpr float kPlayerMaxHP        = 100.0f;
constexpr float kHostileMaxHP       =  60.0f;
constexpr float kFriendlyMaxHP      =  80.0f;

/// §3.11.5 batch D5 — scale-stress entity counts. Tuned so the
/// rpg_demo intentionally pushes the engine past 16.67ms/tick on
/// modest hardware, exercising `SkipPolicy::Budget`.
constexpr std::uint32_t kStressNpcCount       = 10000u;
constexpr std::uint32_t kStressPickupCount    = 50000u;
constexpr std::uint32_t kNormalNpcCount       = 50u;
constexpr std::uint32_t kNormalPickupCount    = 100u;
constexpr double        kTickBudgetSeconds    = 1.0 / 60.0;

/// §3.11.2 batch D2 — multi-camera layout (normalized viewport coords).
constexpr threadmaxx::Viewport kViewportMain    = {0.0f, 0.0f, 1.0f, 1.0f};
constexpr threadmaxx::Viewport kViewportMinimap = {0.78f, 0.02f, 0.20f, 0.30f};
constexpr threadmaxx::Viewport kViewportAimPip  = {0.35f, 0.10f, 0.30f, 0.25f};
constexpr std::uint32_t kCameraIdMain    = 1;
constexpr std::uint32_t kCameraIdMinimap = 2;
constexpr std::uint32_t kCameraIdAim     = 3;

} // namespace rpg
