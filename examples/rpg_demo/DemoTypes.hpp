#pragma once

#include "Heightmap.hpp"

#include <threadmaxx/Components.hpp>
#include <threadmaxx/Handles.hpp>
#include <threadmaxx/UserComponent.hpp>
#include <threadmaxx/render/Camera.hpp>

#include <cstdint>
#include <memory>
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
    /// 2026-05-20 — per-NPC retreat disposition in [0, 1]. Rolled once
    /// at spawn time. On low HP the brain only transitions to Retreat
    /// if this is below `kRetreatChance`; otherwise the NPC fights to
    /// the death. Deterministic across runs (spawned from the demo's
    /// seeded RNG).
    float         fleeRoll   = 0.0f;
    /// 2026-05-20 — cooldown between NPC-to-player melee swings, in
    /// seconds. NPCs in Fight state can damage the player when this
    /// drops to 0 and they're inside `kNpcAttackRange`.
    float         attackCooldown = 0.0f;
};

/// Player-only metadata.
///
/// §3.11.1 batch D1: added `swordSwingTimer` to drive the attack-window
/// animation. While > 0 the sword's localOffset rotation kicks forward
/// and the CombatSystem checks for hits in front of the player.
///
/// 2026-05-22 audit refactor — added `pitchRadians`, `verticalVel`,
/// `blockTimer`, `firstPerson` to back the spec's full
/// rpg_first_person_input_system_spec.md control set.
/// Layout note: all fields are 4-byte sized so the struct has zero
/// padding. UserComponent blobs are memcpy'd into chunk byte buffers
/// and fed verbatim into the engine's commit-hash FNV; any
/// uninitialized padding bytes would silently break determinism
/// (see 2026-05-22 audit refactor). bools that look like they should
/// be `bool` are stored as `std::uint32_t` for the same reason.
struct PlayerState {
    std::uint32_t pickups          = 0;
    /// Player facing yaw (camera-controlled). Movement vector is
    /// rotated by this so W/S/A/D track the player's forward.
    float         yawRadians       = 0.0f;
    /// Camera pitch (mouse Y). Clamped in CameraSystem; only used
    /// for camera aim, never propagated into the player's
    /// world-space orientation (movement stays horizontal).
    float         pitchRadians     = 0.0f;
    float         runSpeed         = 5.0f;
    /// Seconds remaining in the current sword swing. Drives the sword's
    /// localOffset rotation and gates the combat damage check.
    float         swordSwingTimer  = 0.0f;
    /// 2026-05-22 — seconds remaining in the current block window.
    /// While > 0 the player's incoming-damage multiplier is halved
    /// (handled in DamageSystem). Rising-edge consumed from mouse
    /// right.
    float         blockTimer       = 0.0f;
    /// 2026-05-22 — vertical velocity for the spec's jump. Set on a
    /// Space-edge if grounded; integrated each tick by
    /// PlayerInputSystem against `kGravity`; zeroed by
    /// TerrainAttachSystem when the player snaps back to the ground.
    float         verticalVel      = 0.0f;
    /// 2026-05-22 — non-zero while a jump is in flight (Y above the
    /// terrain by more than `kGroundedSlack`). TerrainAttachSystem
    /// resets it when contact resumes. Stored as uint32 (0/1) not
    /// `bool` so the struct has no padding bytes — see layout note
    /// above.
    std::uint32_t airborne         = 0u;
    /// 2026-05-22 — non-zero → first-person camera (eye at player
    /// head, no body render). Toggled by R via kEdgeCameraToggle.
    /// Default = 0 (third-person, the legacy demo behaviour).
    std::uint32_t firstPerson      = 0u;
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

/// §3.11.8 batch D8 — per-terrain-tile metadata. Cells are arranged in
/// a `cellsPerSide × cellsPerSide` grid centered on the world origin;
/// the `(cellX, cellZ)` pair indexes into that grid. Game systems
/// generally don't read this; it's here so the engine's archetype
/// machinery has a distinct mask for terrain tiles versus other
/// `Transform + Faction + StaticTag` static entities (e.g. props,
/// buildings — once D13 lands those).
struct TerrainPatch {
    std::uint32_t cellX = 0;
    std::uint32_t cellZ = 0;
};

/// §3.11.9 batch D9 — short-lived visual particle. Spawned in bursts by
/// `ParticleEmitterSystem` in response to combat / death / pickup
/// events; aged out by `ParticleSystem`. Each particle is a regular
/// ECS entity living in the same chunk storage as everything else —
/// the bench gate is the burst-spawn / burst-destroy pressure on the
/// commit path, not any specialized lifetime store.
///
/// The struct is set once at spawn and never mutated after that.
/// `ParticleSystem` derives remaining lifetime from
/// `engine.tick() * dt - spawnTimeSeconds`; once that crosses
/// `initialLifetime` the particle's entity is destroyed via
/// `cb.destroy`. Motion comes from the engine's existing `Velocity`
/// component — particles spawn with both `Transform` and `Velocity`,
/// and `MovementSystem` integrates them like any other moving entity.
/// The `fadeSeconds` window at the tail of the lifetime is the
/// declared cosmetic-fade budget; v1 leaves visual fade unimplemented
/// (particles pop out at end of life) — adding fade only requires
/// plumbing this UC id into `CubeRenderSystem` for an alpha-scale
/// multiply, which is a future-polish item.
///
/// Keeping the UC immutable is deliberate: the alternative
/// (`removeUserComponent` + `addUserComponent` to update remaining
/// lifetime each tick) would migrate the entity twice per tick — out
/// of the Particle chunk and back into it — which defeats the
/// chunk-stability premise the burst-spawn/destroy bench is measuring.
struct Particle {
    float spawnTimeSeconds = 0.0f;
    float initialLifetime  = 0.5f;
    float color[4]         = {1.0f, 1.0f, 1.0f, 1.0f};
    float fadeSeconds      = 0.2f;
    float pad[2]           = {0.0f, 0.0f};   // pad to 32 bytes
};

/// §3.11.9 batch D9 — per-emitter parameters. Currently unused as a
/// UserComponent (the emitter logic in `ParticleEmitterSystem` keys
/// off engine event channels rather than per-entity emitter state),
/// but reserved as a UC bit so D10+ can attach it to the player /
/// sword / NPC torsos for tunable rates. Registering it now keeps the
/// archetype layout stable as D10's burst content lands.
struct ParticleEmitter {
    float    rateHz       = 0.0f;
    float    speed        = 2.0f;
    float    lifeSeconds  = 0.5f;
    float    sizeScale    = 0.08f;
    float    color[4]     = {1.0f, 1.0f, 1.0f, 1.0f};
    std::uint32_t kind    = 0;   // 0=spark, 1=dust, 2=puff, 3=blood
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
    /// §3.11.6 batch D6 — procedural-animation parameters.
    threadmaxx::UserComponentId animState;
    /// §3.11.8 batch D8 — terrain-tile cell coordinate.
    threadmaxx::UserComponentId terrainPatch;
    /// §3.11.9 batch D9 — short-lived visual particle.
    threadmaxx::UserComponentId particle;
    /// §3.11.9 batch D9 — per-entity emitter knobs (reserved; see
    /// `ParticleEmitter` doc).
    threadmaxx::UserComponentId particleEmitter;
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

    /// 2026-05-20 — sticky toggle for the aim PIP camera. Flipped
    /// each time `kEdgeAimToggle` (V key) is observed by
    /// PlayerInputSystem. Decoupled from the sword-swing timer so
    /// you can leave the PIP open while running around.
    bool          aimPipVisible    = false;

    /// §3.11.8 batch D8 — generated at boot in `DemoGame::onSetup`.
    /// Borrowed (by raw pointer) by `TerrainAttachSystem` and
    /// `NPCBrainSystem` to snap entities to the ground and to reject
    /// over-steep Wander targets. `shared_ptr` so headless tests can
    /// hold their own copy alongside the demo's.
    std::shared_ptr<const Heightmap> heightmap;
    /// §3.11.8 batch D8 — width/depth of the terrain grid in tiles.
    /// Chosen at boot from `worldState_.stressMode`: stress uses
    /// `kStressTerrainCellsPerSide`, otherwise `kNormalTerrainCellsPerSide`.
    /// Tests can override before `engine.initialize()` to keep boots
    /// fast.
    std::uint32_t terrainCellsPerSide = 0;
};

/// 2026-05-22 audit refactor — jump + block tuning. The Space-edge
/// kicks `verticalVel = kJumpVelocity` if the player is grounded;
/// gravity decelerates each tick; TerrainAttachSystem resets both
/// `verticalVel` and `airborne` when contact resumes.
constexpr float kJumpVelocity       = 5.5f;    // m/s initial upward
constexpr float kGravity            = -18.0f;  // m/s² (slightly heavy)
constexpr float kGroundedSlack      = 0.02f;   // m — terrain hysteresis
constexpr float kBlockSeconds       = 0.40f;   // hold window per block press

/// 2026-05-22 audit refactor — pitch clamp ± 80°. Matches the spec's
/// "clamp pitch to a reasonable range" requirement.
constexpr float kPitchMinRadians    = -1.396f;
constexpr float kPitchMaxRadians    =  1.396f;

/// §3.11.1 batch D1 — gameplay tuning constants.
constexpr float kSwordSwingSeconds  = 0.30f;
constexpr float kSwordDamage        = 25.0f;
constexpr float kSwordTipRadius     = 0.7f;
constexpr float kPlayerMaxHP        = 100.0f;
constexpr float kHostileMaxHP       =  60.0f;
constexpr float kFriendlyMaxHP      =  80.0f;

/// 2026-05-20 — sword chop animation arc around the player's local
/// +X axis. The sword pivot stays at (+0.5, 0.8, -0.8) for the
/// whole swing; only its orientation rotates. Start raised
/// overhead, pass through forward-level at mid-swing, end at a
/// shallow downward thrust. CombatSystem samples positions along
/// the same arc so the hit volume matches what the player sees.
constexpr float kSwingAngleStart    =  1.0f;   // raised overhead
constexpr float kSwingAngleEnd      = -0.3f;   // mild downward
constexpr int   kSwingHitSamples    = 5;       // angles tested
constexpr float kSwordRestX         =  0.5f;
constexpr float kSwordRestY         =  0.8f;
constexpr float kSwordRestZ         = -0.8f;

/// 2026-05-20 — NPC behavioural tuning.
constexpr float kRetreatChance      = 0.5f;    // fraction that flees on low HP
constexpr float kNpcAttackRange     = 1.6f;    // melee reach (world units)
constexpr float kNpcAttackDamage    = 8.0f;    // HP per hit
constexpr float kNpcAttackCooldown  = 1.0f;    // seconds between swings

/// §3.11.5 batch D5 — scale-stress entity counts. Tuned so the
/// rpg_demo intentionally pushes the engine past 16.67ms/tick on
/// modest hardware, exercising `SkipPolicy::Budget`.
///
/// 2026-05-20 (rev 2) — NPC count bumped from 10k to 100k. With
/// the bottleneck removals landed in the same commit (no spatial
/// hash rebuild, no per-tick AnimationSystem transform write,
/// brain skips command writes when state is unchanged, movement
/// skips zero-velocity writes, simd-batched math) the demo now
/// sustains ~30 Hz at 100k NPCs on a 72-core box. Pickup count
/// stays at 5k — pickups don't drive per-tick CPU work.
constexpr std::uint32_t kStressNpcCount       = 100000u;
constexpr std::uint32_t kStressPickupCount    = 5000u;
constexpr std::uint32_t kNormalNpcCount       = 50u;
constexpr std::uint32_t kNormalPickupCount    = 100u;
constexpr double        kTickBudgetSeconds    = 1.0 / 60.0;

/// §3.11.8 batch D8 — terrain extent + tile counts. The terrain spans
/// `[-kTerrainExtent/2, +kTerrainExtent/2]` along X and Z. Tiles are
/// `cellsPerSide × cellsPerSide`; the same Heightmap resolution is
/// used regardless (so normal-mode tiles sample a coarser version of
/// the same continuous field stress-mode tiles see).
constexpr float          kTerrainExtent              = 256.0f;
constexpr std::uint32_t  kHeightmapResolution        = 128u;
constexpr std::uint32_t  kStressTerrainCellsPerSide  = 256u;   // 65 536 tiles
constexpr std::uint32_t  kNormalTerrainCellsPerSide  = 32u;    //  1 024 tiles
constexpr std::uint32_t  kHeightmapSeed              = 0xD8000001u;

/// §3.11.8 batch D8 — NPCBrainSystem rejects a candidate Wander
/// target with slopeAt > this threshold. The bilinearly-interpolated
/// fBm field maxes out at gradient ~0.4 (tan⁻¹(0.4) ≈ 22°); the
/// threshold sits at 0.35 (~19°) so realistic hillsides actually
/// trigger the reject. Up to `kMaxSlopeRejectAttempts` re-rolls
/// before falling through to whatever the last sample was — picking
/// a steep target beats standing still forever.
constexpr float          kSlopeRejectThreshold       = 0.35f;
constexpr int            kMaxSlopeRejectAttempts     = 3;

/// §3.11.9 batch D9 — particle burst tuning. Counts are intentionally
/// modest in normal play (the demo's pre-D9 NPCs only emit when an
/// event fires, not per tick) but scale with `kStressNpcCount` in
/// stress mode where the player can be hit by dozens of NPCs at once.
constexpr std::uint32_t kParticlesPerSwordHit       = 14u;
constexpr std::uint32_t kParticlesPerDeath          = 32u;
constexpr std::uint32_t kParticlesPerPickup         = 10u;
constexpr float         kParticleSparkLifeSeconds   = 0.45f;
constexpr float         kParticleDustLifeSeconds    = 0.65f;
constexpr float         kParticlePuffLifeSeconds    = 0.80f;
constexpr float         kParticleSparkSpeed         = 6.0f;
constexpr float         kParticleDustSpeed          = 2.5f;
constexpr float         kParticlePuffSpeed          = 1.5f;
constexpr float         kParticleScale              = 0.08f;

/// §3.11.2 batch D2 — multi-camera layout (normalized viewport coords).
constexpr threadmaxx::Viewport kViewportMain    = {0.0f, 0.0f, 1.0f, 1.0f};
constexpr threadmaxx::Viewport kViewportMinimap = {0.78f, 0.02f, 0.20f, 0.30f};
constexpr threadmaxx::Viewport kViewportAimPip  = {0.35f, 0.10f, 0.30f, 0.25f};
constexpr std::uint32_t kCameraIdMain    = 1;
constexpr std::uint32_t kCameraIdMinimap = 2;
constexpr std::uint32_t kCameraIdAim     = 3;

} // namespace rpg
