#pragma once

// tou2d — game-side user component PODs.
//
// Every type here must be trivially copyable; the engine memcpys
// values into chunked storage and never invokes ctors/dtors on user
// components (see threadmaxx/UserComponent.hpp).

#include <threadmaxx/UserComponent.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <random>
#include <vector>

namespace tou2d {

/// Per-tick input snapshot. Filled by InputSystem at the top of each
/// step (preStep), consumed by MovementSystem inside the wave.
///
/// Bits are not bitfields — keeps the type a plain 8-byte POD so
/// memcpy / wide loads are uncomplicated.
struct PlayerInput {
    std::uint8_t thrust       = 0;  ///< Up / W / I / Numpad-5
    std::uint8_t back         = 0;  ///< Down / S / K / Numpad-2 (reverse thrust, weaker)
    std::uint8_t turnLeft     = 0;
    std::uint8_t turnRight    = 0;
    std::uint8_t fireBasic    = 0;  ///< M3.1 Dumbfire
    std::uint8_t fireSpecial  = 0;  ///< M3.3 Spread shot
    std::uint8_t menuButton   = 0;  ///< "Launch all" / menu in TOU
    std::uint8_t _pad         = 0;
};
static_assert(sizeof(PlayerInput) == 8, "PlayerInput must stay 8 bytes");

/// M7.4 — sentinel meaning "use slot as faction" for LocalPlayer /
/// PlayerSlotSetup. With the default sentinel every slot ends up in
/// its own faction, which reproduces the pre-M7.4 free-for-all (no
/// ally relationships) since `findAlly` requires equal factionId.
inline constexpr std::uint8_t kFactionAuto = 0xFFu;

/// Marks an entity as one of the local players. `slot` ∈ [0, 3] picks
/// the key-binding row (P1 arrows / P2 WSAD / P3 IJKL / P4 numpad).
///
/// `isBot != 0` switches the input source from keyboard (InputSystem)
/// to seek-and-fire AI (BotControlSystem). Both systems are preStep —
/// BotControlSystem is registered AFTER InputSystem so it overrides
/// the keyboard read for bot ships only.
///
/// M7.4 — `factionId` groups ships into ally pools. BotControlSystem
/// skips same-faction candidates when picking a target; WeaponFireSystem
/// suppresses a bot's basic/special shot whose forward arc covers a
/// same-faction ally. Humans aren't filtered (friendly fire stays as a
/// game rule when the human aims at an ally manually). The resolved
/// value is always < `kFactionAuto`; TouGame::spawnShip rewrites the
/// sentinel to `slot` at spawn time so default = unique faction =
/// pre-M7.4 free-for-all behaviour.
struct LocalPlayer {
    std::uint8_t slot      = 0;
    std::uint8_t isBot     = 0;
    std::uint8_t factionId = kFactionAuto;
    std::uint8_t _pad[5]   = {};
};
static_assert(sizeof(LocalPlayer) == 8, "LocalPlayer must stay 8 bytes");

/// Ship runtime state.
///
/// `respawnIn` is the post-death freeze counter — 0 means the ship is
/// alive; non-zero means it's hidden (DisabledTag attached) and will be
/// rematerialized at (spawnX, spawnY) when the counter reaches 0.
/// `kills` is the deathmatch score (incremented by
/// `BulletShipCollisionSystem` when one of this ship's bullets deals a
/// killing blow). `tilesDestroyed` is the secondary "wreckage" counter
/// (incremented by `BulletTerrainSystem` when a bullet clears a tile);
/// kept separate so deathmatch scoring isn't polluted by terrain spam.
struct Ship {
    float         currentHp      = 200.0f;
    float         maxHp          = 200.0f;
    float         spawnX         = 0.0f;
    float         spawnY         = 0.0f;
    std::uint16_t shipKindIdx    = 0;
    std::uint16_t respawnIn      = 0;     ///< ticks until respawn; 0 = alive
    std::uint16_t kills          = 0;     ///< deathmatch score
    std::uint16_t tilesDestroyed = 0;     ///< secondary terrain-wreckage counter
};
static_assert(sizeof(Ship) == 24, "Ship must stay 24 bytes");

/// Default per-ship max HP — used only as a fallback for cases where
/// `Ship.shipKindIdx` is out of range. Per-kind HP is now derived from
/// `ShipKind::strength * kShipHpPerStrength` (see `kShipKinds`); the
/// flat 200 used in M4.4 became the baseline-Basic 150 once strength
/// 3.0 × 50 = 150 wired through.
inline constexpr float kBaseShipHp = 150.0f;

/// M4.5 — immutable ship-design archetype. Maps directly onto the
/// `toudoc_ships.htm` Strength/Thrusters/Turning table. `displayName`
/// is a fixed 12-byte NUL-padded slot so the POD stays trivially
/// copyable (engine memcpys user components into chunked storage).
///
/// Stat -> game mapping:
///   * `strength`    → maxHp = strength * kShipHpPerStrength (Basic=150,
///                              Bee=50, Destroyer=300 at scale 50).
///   * `thrustForce` → MovementSystem thrust scalar; ratio against the
///                     Basic baseline (3.0) — Bee=10/3≈3.33×, Dest=0.5×.
///   * `turnRate`    → MovementSystem turn-rate + angular-speed scalar;
///                     same Basic-baseline ratio.
struct ShipKind {
    std::array<char, 12> displayName;
    float                strength;
    float                thrustForce;
    float                turnRate;
};
static_assert(sizeof(ShipKind) == 24, "ShipKind must stay 24 bytes");

/// HP per `strength` unit. Picked so the Basic-ship strength 2.5
/// lands on 125 HP at the rebalanced defaults.
inline constexpr float kShipHpPerStrength = 50.0f;

/// Reference strength/thrust/turn around which per-ship scalars are
/// computed. 2026-05-28 — dropped from 3.0 to 2.5 to give every ship
/// a slightly lowered baseline; per-kind bonuses then accent ONE
/// stat by ~25% (kBalancedKindBonus). Result: ships feel closer in
/// raw capability, with each kind's character coming from which stat
/// is its strong suit rather than a 6× spread between Bee and
/// Destroyer.
inline constexpr float kShipKindStatReference = 2.5f;

/// 2026-05-28 — coefficient applied to each kind's PRIMARY stat over
/// the default baseline. 1.25 = 25% bonus; small enough that all
/// ships stay competitive, large enough to give each one a
/// recognizable feel. The other two stats stay flat at the baseline.
inline constexpr float kBalancedKindBonus = 1.25f;

/// 9 ship designs (Strength / Thrusters / Turning). 2026-05-28 —
/// rebalanced per M5.2: every kind starts from the (2.5, 2.5, 2.5)
/// baseline and one stat (the "primary") is boosted by
/// kBalancedKindBonus. Slot indices are stable — DON'T reorder;
/// `Ship.shipKindIdx` is a direct index into this array AND is
/// round-tripped via WorldSnapshot.
inline constexpr ShipKind kShipKinds[] = {
    //                                                  STR    THRUST TURN
    {{'B','a','s','i','c',' ','s','h','i','p',0,0},     2.5f,  2.5f,  2.5f}, // 0 — well-rounded baseline
    {{'B','a','t','m','a','n',0,0,0,0,0,0},             2.5f,  2.5f,  3.125f}, // 1 — turn-bonus
    {{'B','2',' ','S','t','e','a','l','t','h',0,0},     2.5f,  2.5f,  3.125f}, // 2 — turn-bonus
    {{'S','p','e','e','d','i','e',0,0,0,0,0},           2.5f,  3.125f, 2.5f}, // 3 — thrust-bonus
    {{'X',' ','W','i','n','g',0,0,0,0,0,0},             2.5f,  2.5f,  3.125f}, // 4 — turn-bonus
    {{'T','i','e',' ','F','i','g','h','t','e','r',0},   2.5f,  2.5f,  3.125f}, // 5 — turn-bonus
    {{'B','e','e',0,0,0,0,0,0,0,0,0},                   2.5f,  3.125f, 2.5f}, // 6 — thrust-bonus
    {{'F','l','y',0,0,0,0,0,0,0,0,0},                   3.125f, 2.5f, 2.5f}, // 7 — strength-bonus
    {{'D','e','s','t','r','o','y','e','r',0,0,0},       3.125f, 2.5f, 2.5f}, // 8 — strength-bonus
};
inline constexpr std::size_t kShipKindCount =
    sizeof(kShipKinds) / sizeof(kShipKinds[0]);

/// Bounds-checked lookup. Out-of-range returns Basic so a corrupt
/// `shipKindIdx` from a stale snapshot can't crash anything.
inline const ShipKind& shipKindAt(std::uint16_t idx) noexcept {
    return kShipKinds[idx < kShipKindCount ? idx : 0];
}

/// M3.5 — emitted once when any slot crosses `kFragLimit`. Listeners
/// (currently just the host logger) can react however they like; the
/// engine itself does NOT halt or modify state on round-end.
struct RoundEnded {
    std::uint8_t  winnerSlot  = 0;
    std::uint8_t  _pad        = 0;
    std::uint16_t winnerKills = 0;
};
static_assert(sizeof(RoundEnded) == 4, "RoundEnded must stay 4 bytes");

/// Number of kills required to end a round. 10 matches the M3-acceptance
/// "deathmatch round ends on score limit" requirement; small enough that
/// bot-vs-bot self-play settles a winner within a few minutes of sim.
inline constexpr std::uint16_t kFragLimit = 10;

/// M4.3 — round-mode selector.
///
/// `Deathmatch` (default) — every kill counts; first to `kFragLimit`
/// wins; ships respawn after `kRespawnTicks` ticks.
///
/// `LastShipStanding` — death is permanent for the round. When only
/// one ship has `currentHp > 0` remaining (or zero if mutual annihilation
/// in the same tick), the round ends with that ship as the winner
/// (mutual annihilation falls back to the slot with the most kills).
/// `Ship::respawnIn` is set to `kPermanentDeathSentinel` on death so
/// `ShipLifecycleSystem` skips the countdown / respawn branch.
enum class MatchMode : std::uint8_t {
    Deathmatch       = 0,
    LastShipStanding = 1,
};

/// Sentinel respawn-counter value meaning "this ship is out of the
/// round; never respawn." Distinct from any valid countdown value
/// (max is kRespawnTicks ≈ 180). Used only when `matchMode ==
/// LastShipStanding`.
inline constexpr std::uint16_t kPermanentDeathSentinel = 0xFFFFu;

/// Holdoff after round-end before the restart key is accepted. Prevents
/// a player who is still mashing fire on round-end from immediately
/// restarting before they have noticed the winner banner. 90 ticks @
/// 60 Hz = 1.5 s — long enough to register the banner, short enough
/// that the next round starts feel responsive.
inline constexpr std::uint16_t kRestartHoldoffTicks = 90;

/// Tile-grid terrain classification. Mirrors the layout of the imported
/// `.lev` attribute byte.
///
/// M5.7 — `RepairBase` (previously `Repair`) is a non-blocking pickup
/// tile. M7.5 reworked the semantics: ships that overlap a base
/// regenerate HP per tick (`kRepairBaseHpPerTick`) and have their
/// `WeaponLoadout::specialKind` advanced exactly once on the entry
/// edge — the tile DOES NOT consume on touch and keeps offering regen
/// + cycle for as long as the ship stays on it. The procedural
/// generator + synthetic arena sprinkle a few of these. Single-shot
/// healing now lives on the new entity-based `Pickup` user component
/// (`PickupKind::RepairKit`, handled by `RepairKitSystem`), keeping
/// the tile/entity split data-driven.
enum class Attribute : std::uint8_t {
    Air        = 0,
    Solid      = 1,
    Damage     = 2,
    RepairBase = 3,   ///< M7.5 — renamed from Repair (kept value 3 for
                      ///  snapshot stability with pre-M7.5 levels).
};

/// In-flight projectile. `weaponKind` values:
///   * 0 = Dumbfire (M3.1)
///   * 1 = Spread   (M3.3)
///   * 2 = Rapid    (M5.6)
///   * 3 = Sniper   (M5.6)
///   * 4 = Quintet  (M5.6)
///   * 5 = Heavy    (M5.7)
///   * 6 = Quad     (M5.7)
///   * 7 = Shotgun  (M5.7)
///   * 8 = Mine     (M5.8 — stationary drop, sits at ship for ttl)
///   * 9 = Bouncer  (M5.8 — reflects off solid terrain `bouncesLeft` times
///                  before despawning; ship hit / bedrock still destroy)
///   *10 = Homer    (M5.8 — BulletHomingSystem steers it toward nearest
///                  enemy ship each tick by a bounded angular step)
/// `ownerSlot` mirrors the firing ship's `LocalPlayer::slot` so kill
/// credit can route to the right player score on impact.
///
/// M5.8 — `bouncesLeft` grew the POD from 8 → 12 bytes. WeaponFireSystem
/// initialises it from `SpecialWeaponSpec::bouncesLeft` so per-kind tuning
/// stays catalogue-driven; non-Bouncer kinds default it to 0 and the
/// terrain path's reflect branch only fires when both `weaponKind == 9`
/// AND `bouncesLeft > 0`.
struct Bullet {
    float         ttlSeconds  = 0.0f;
    std::uint8_t  damage      = 0;
    std::uint8_t  weaponKind  = 0;
    std::uint16_t ownerSlot   = 0;
    std::uint8_t  bouncesLeft = 0;
    std::uint8_t  _pad[3]     = {};
};
static_assert(sizeof(Bullet) == 12, "Bullet must stay 12 bytes");

/// M4.8 — per-ship sprite reference. `atlasIdx` points into the
/// SpriteCompositor's atlas table. The compositor maintains its own
/// per-entity prev-frame cache (last blit position + rotation) so the
/// user component stays a tiny POD that's safe to memcpy across mask
/// changes.
struct ShipSpriteRef {
    std::int32_t atlasIdx = -1;
    std::uint8_t _pad[4]  = {};
};
static_assert(sizeof(ShipSpriteRef) == 8, "ShipSpriteRef must stay 8 bytes");

/// M4.8 — typed event channel for one-shot audio cues. Game systems
/// emit; AudioSystem subscribes and routes to a miniaudio sound
/// instance. `soundId` is an opaque index into AudioSystem's bank.
/// `volume` is 0..1; 0 means "use bank default".
struct AudioPlay {
    std::uint16_t soundId = 0;
    std::uint8_t  volume  = 0;
    std::uint8_t  _pad    = 0;
};
static_assert(sizeof(AudioPlay) == 4, "AudioPlay must stay 4 bytes");

/// Stable IDs into the AudioSystem sound bank. Add new entries at the
/// end; never reorder (would invalidate any persisted sound choices).
namespace audio {
inline constexpr std::uint16_t kSoundDumbfire   = 0;  ///< basic weapon fire
inline constexpr std::uint16_t kSoundSpread     = 1;  ///< spread weapon fire
inline constexpr std::uint16_t kSoundHit        = 2;  ///< bullet hits a ship
inline constexpr std::uint16_t kSoundExplode    = 3;  ///< ship explosion / death
inline constexpr std::uint16_t kSoundTileBreak  = 4;  ///< terrain tile destroyed
inline constexpr std::uint16_t kSoundCount      = 5;
} // namespace audio

/// M4.2 / M4.5 — per-ship weapon ammo + reload state.
///
/// Two cycles per weapon:
///   * **Loader cooldown** (`*Cooldown`) — per-shot / per-burst gap. A
///     fire ALWAYS sets this to `kFire*CooldownTicks`; the next fire is
///     gated on `cooldown == 0`. M4.5 — this is the "few hundred ms
///     between bursts" the user asked for, and matches the chambered-
///     round mental model. Dumbfire cooldown is short (~100 ms);
///     special cooldown is per-kind.
///   * **Magazine reload** (`*Ammo` + `*ReloadIn`) — runs out of ammo
///     → `reloadIn = kReload*Ticks`, ticks down each step, on zero the
///     magazine refills.
///
/// Fire gate is `ammo > 0 && reloadIn == 0 && cooldown == 0`. The
/// loader and reload counters are independent — pressing fire while
/// the loader is recharging just no-ops; pressing fire after an empty
/// mag has reloaded ALSO requires the loader to be clear (it always
/// is right after a refill — refill never sets a cooldown).
///
/// M5.6 — the second weapon is now generic "special" (no longer
/// hard-coded as spread). `specialKind` selects one of the
/// `kSpecialWeaponSpecs` table entries; the runtime reads the per-kind
/// tunables (cooldown / magazine / reload / spawn pattern). Layout
/// stays 16 bytes — `specialKind` replaces the old `_pad0` slot.
struct WeaponLoadout {
    std::uint16_t dumbfireAmmo     = 0;   ///< rounds remaining in magazine
    std::uint16_t dumbfireReloadIn = 0;   ///< ticks until mag refill; 0 = ready
    std::uint16_t dumbfireCooldown = 0;   ///< ticks until next shot loader-ready
    std::uint8_t  specialKind      = 0;   ///< M5.6 — index into kSpecialWeaponSpecs
    std::uint8_t  _pad0            = 0;
    std::uint16_t specialAmmo      = 0;
    std::uint16_t specialReloadIn  = 0;
    std::uint16_t specialCooldown  = 0;   ///< burst-to-burst gap
    std::uint16_t _pad1            = 0;
};
static_assert(sizeof(WeaponLoadout) == 16, "WeaponLoadout must stay 16 bytes");

/// Magazine + reload tuning.
///
/// 2026-05-28 — reworked per the M5.2 design: the BASIC weapon is now
/// infinite-fire on a fixed cooldown (no reload, no magazine) and the
/// SPECIAL (spread) weapon reloads after every single burst. The
/// dumbfire ammo field is still part of `WeaponLoadout` for layout
/// stability but the runtime never decrements it — a huge magazine
/// guards against any stale reload code in the wild that still checks
/// `ammo > 0`.
inline constexpr std::uint16_t kDumbfireMagazine = 1;  // visual "ready" pip; ammo never decrements

/// Dumbfire never reloads — basic is infinite-fire on a fixed cooldown.
inline constexpr std::uint16_t kDumbfireReloadTicks = 0;

/// M5.6 — special-weapon catalogue. M5.7 extends with Heavy / Quad /
/// Shotgun. M5.8 extends with Mine / Bouncer / Homer.
///
///   * **Spread**  (kind 0) — 3-bullet ±17° fan; single-burst mag, 1.25 s
///                            reload. Unchanged from the M3.3 design.
///   * **Rapid**   (kind 1) — high-fire-rate single bullets like a
///                            machine gun. Small per-bullet damage,
///                            short loader cooldown, large magazine.
///   * **Sniper**  (kind 2) — single very fast bullet, long-range, high
///                            damage, slow cadence, small magazine.
///   * **Quintet** (kind 3) — 5-bullet ±10°/±20° narrow fan; tighter
///                            than Spread but more lanes.
///   * **Heavy**   (kind 4) — single slow heavy shell. Hard-hitter
///                            ideal for stationary targets; brutal
///                            against thin terrain. Small mag, long
///                            cadence.
///   * **Quad**    (kind 5) — 4-bullet even fan (parallel-feeling
///                            chaingun). Medium damage, fast cadence.
///   * **Shotgun** (kind 6) — 7-bullet wide fan at point-blank range,
///                            short TTL. The room-clearer.
///   * **Mine**    (kind 7) — drop-in-place stationary bullet. `muzzleSpeed`
///                            = 0 → WeaponFireSystem spawns at ship
///                            position with zero forward velocity; long
///                            ttl so the mine persists, normal damage on
///                            contact via BulletShipCollisionSystem.
///   * **Bouncer** (kind 8) — reflects off solid terrain `bouncesLeft`
///                            times before despawning. Each bounce halves
///                            the surface-normal velocity to keep
///                            corner-grazing from accelerating; ship hit
///                            and bedrock still destroy normally.
///   * **Homer**   (kind 9) — BulletHomingSystem steers it toward the
///                            nearest enemy ship each tick by a bounded
///                            angular step (`kHomerTurnPerTickRad`).
///                            Slow speed so steering matters; low damage
///                            so it doesn't overshadow the catalogue.
///
/// `weaponKind` is the byte stamped into spawned `Bullet`s so impact
/// effects (sparks, audio) can route per-weapon. The values 0 (Dumbfire)
/// and 1 (Spread) are stable from pre-M5.6; M5.6 added 2/3/4; M5.7
/// extends with 5/6/7; M5.8 extends with 8/9/10.
enum class SpecialKind : std::uint8_t {
    Spread  = 0,
    Rapid   = 1,
    Sniper  = 2,
    Quintet = 3,
    Heavy   = 4,
    Quad    = 5,
    Shotgun = 6,
    Mine    = 7,
    Bouncer = 8,
    Homer   = 9,
};
inline constexpr std::uint8_t kSpecialKindCount = 10;

/// M5.8 — Homer angular-step ceiling (radians per tick @ 60 Hz).
/// Picked so a Homer chasing an evasive Bee at 200 wu/s can still
/// nominally turn 18°/tick → tracks but is dodgeable.
inline constexpr float kHomerTurnPerTickRad = 0.32f;

/// M5.8 — per-bounce velocity damping on Bouncer reflections. 0.9
/// keeps the shot meaningfully fast across its 3 bounces (final speed
/// ≈ 73% of muzzle) while preventing corner-grazes from accelerating.
inline constexpr float kBouncerDamping = 0.9f;

struct SpecialWeaponSpec {
    std::uint16_t magazine;          ///< rounds per mag (1 = one-shot per reload)
    std::uint16_t reloadTicks;       ///< post-empty reload duration
    std::uint16_t cooldownTicks;     ///< between-shot loader cooldown
    std::uint8_t  bulletsPerShot;    ///< how many bullets `spawnBullet` is called for
    std::uint8_t  weaponKind;        ///< stamped into Bullet.weaponKind
    std::uint8_t  damagePerBullet;   ///< Bullet.damage
    std::uint8_t  bouncesLeft;       ///< M5.8 — Bouncer initial wall-hits budget (0 = non-reflecting)
    float         muzzleSpeed;       ///< world units / s (0 = drop-in-place, M5.8 Mine)
    float         ttlSeconds;        ///< per-bullet lifetime
    float         spreadStepRad;     ///< angle between adjacent bullets in the fan
};
static_assert(sizeof(SpecialWeaponSpec) == 24, "SpecialWeaponSpec must stay 24 bytes");

inline constexpr SpecialWeaponSpec kSpecialWeaponSpecs[kSpecialKindCount] = {
    // {mag, reload, cool, n, kind, dmg, bounces, speed,  ttl,  step}
    // ── Spread (legacy) ── 3 bullets, ±17°, 1.25 s reload.
    {  1,   75,    22,    3, 1,    5,  0, 520.0f, 0.9f, 0.30f },
    // ── Rapid ── 12-round mag, 8-tick loader, 80-tick reload. Long
    //   burst window per mag so the player can sustain pressure;
    //   per-bullet damage is below dumbfire to keep TTK competitive
    //   with the rest of the catalogue.
    {  12,  80,    8,     1, 2,    5,  0, 600.0f, 1.0f, 0.0f  },
    // ── Sniper ── 3-shot mag, 60-tick loader, 120-tick reload. Single
    //   very fast bullet with a long lifetime → covers the level
    //   diagonal at speed.
    {  3,   120,   60,    1, 3,    24, 0, 1100.0f, 1.6f, 0.0f },
    // ── Quintet ── 5-bullet fan at ±10°/±20°, 30-tick loader,
    //   90-tick reload. Tighter than Spread, harder to track but
    //   covers more lanes when it lands.
    {  1,   90,    30,    5, 4,    4,  0, 560.0f, 0.95f, 0.175f },
    // ── Heavy ── M5.7. 4-round mag, 35-tick loader, 90-tick reload.
    //   Single slow heavy shell at 440 wu/s with 20 damage — punches
    //   through Basic-ship HP in 8 hits, kills a Bee in 3. Long ttl
    //   so the shell traverses the level without despawning early.
    {  4,   90,    35,    1, 5,    20, 0, 440.0f, 2.0f, 0.0f  },
    // ── Quad ── M5.7. 2-round mag, 80-tick reload, 25-tick loader,
    //   4 bullets in an even fan at ±~5° / ±~15° (step 0.10 rad).
    //   Mid-damage chaingun-feel for medium range.
    {  2,   80,    25,    4, 6,    4,  0, 560.0f, 1.0f, 0.10f },
    // ── Shotgun ── M5.7. 1-round mag, 85-tick reload, 30-tick
    //   loader, 7 bullets in a wide fan at ±~5° / ±~15° / ±~25°
    //   (step 0.18 rad). Per-bullet damage low (3) but the bullets
    //   stack at close range to one-shot Bee-class hulls. Short ttl
    //   (0.65 s) so the spread doesn't persist past 1 screen.
    {  1,   85,    30,    7, 7,    3,  0, 500.0f, 0.65f, 0.18f },
    // ── Mine ── M5.8. Drop-in-place stationary projectile.
    //   `muzzleSpeed = 0` → spawn at ship position with zero forward
    //   velocity. 4-round mag (carries a few traps), 110-tick reload,
    //   30-tick loader. 3.5 s ttl lets a mine outlast a typical
    //   maneuver. Heavy damage (28) — the trade-off for being passive.
    {  4,   110,   30,    1, 8,    28, 0, 0.0f,   3.5f, 0.0f },
    // ── Bouncer ── M5.8. Single bullet, 3 wall-hits before despawn.
    //   2-round mag, 75-tick reload, 22-tick loader, decent damage
    //   per-bullet (10) so a corridor-bounce kill is meaningful. Long
    //   ttl (1.8 s) so the bounces have time to land. `spreadStepRad`
    //   0 — single straight shot at the muzzle.
    {  2,   75,    22,    1, 9,    10, 3, 480.0f, 1.8f, 0.0f },
    // ── Homer ── M5.8. Single steering bullet at slow speed (320 wu/s)
    //   so the angular-step ceiling actually matters; high ttl (2.4 s)
    //   gives it a wide kill window. Damage 14 — enough to one-shot
    //   half-HP Bee-class, but the slow speed makes it dodgeable.
    //   3-round mag, 130-tick reload, 50-tick loader.
    {  3,   130,   50,    1, 10,   14, 0, 320.0f, 2.4f, 0.0f },
};

inline const SpecialWeaponSpec& specialSpecAt(std::uint8_t kind) noexcept {
    return kSpecialWeaponSpecs[kind < kSpecialKindCount ? kind : 0];
}

/// M5.6 — back-compat alias. Pre-M5.6 callers wrote
/// `kSpreadMagazine` / `kSpreadReloadTicks`; both still resolve to the
/// Spread spec's values so legacy log and audit traces don't need
/// rewriting.
inline constexpr std::uint16_t kSpreadMagazine    = 1;
inline constexpr std::uint16_t kSpreadReloadTicks = 75;

/// M5.7 — repair-tile pickup tunables.
///
/// * `kRepairHealAmount` — HP restored when a ship overlaps a Repair
///   cell. Clamped at `Ship::maxHp` (so a full-HP ship still grabs
///   the tile for the weapon-switch effect — same as the original).
/// * `kRepairTileColor` — visual marker emitted by the background
///   painter when a Repair tile is FIRST written into the grid (the
///   painter draws a slot-colored dot at the world-center of the
///   cell). Procedural-gen + synthetic-arena seeders fire the same
///   destroy callback the bullet-break path uses; the painter
///   distinguishes by attribute.
inline constexpr float        kRepairHealAmount = 40.0f;
inline constexpr std::uint32_t kRepairTileColor = 0xFF40FFA0u;  // teal

/// M7.5 — RepairBase regen rate (HP per tick) while a ship's AABB
/// overlaps the tile. Picked so a max-HP ship (200 HP) refills in
/// roughly 3 seconds at 60 Hz from empty — fast enough to feel useful,
/// slow enough that hovering on a base isn't a get-out-of-jail-free
/// vs. sustained fire. Edge-triggered special-cycle still happens on
/// entry (once per visit), preserving the M5.7 weapon-rotation feel.
inline constexpr float kRepairBaseHpPerTick = 1.0f;

/// M7.5 — data-driven pickup catalogue. `PickupKind` is the discriminator
/// stored on the `Pickup` user component; `PickupSpec` carries the per-
/// kind tunables. Keep additions append-only — the value persists into
/// snapshots / level files via the `Pickup::kind` byte.
///
/// `RepairKit` mirrors the pre-M7.5 single-shot Repair tile behaviour
/// (heal + cycle special) but on an entity (not a terrain cell) so a
/// kit can despawn on pickup and respawn after a fixed delay.
enum class PickupKind : std::uint8_t {
    RepairKit = 0,
    Count
};
inline constexpr std::uint8_t kPickupKindCount =
    static_cast<std::uint8_t>(PickupKind::Count);

/// M7.5 — per-kind tuning. Flat POD table indexed by `PickupKind` —
/// no virtual interface, the consumer system switches on `kind` at the
/// apply site (`RepairKitSystem` for kits, `RepairPickupSystem` for
/// the corresponding RepairBase tile semantics).
struct PickupSpec {
    std::uint16_t respawnIntervalTicks = 0;
    std::uint16_t effectMagnitude      = 0;
    std::uint8_t  _pad[4]              = {};
};
static_assert(sizeof(PickupSpec) == 8, "PickupSpec must stay 8 bytes");

/// Catalogue. Indexed by `static_cast<std::size_t>(kind)`.
inline constexpr std::array<PickupSpec, kPickupKindCount> kPickupSpecs = {{
    // RepairKit: 40 HP per pickup (matches pre-M7.5 `kRepairHealAmount`),
    // respawn after 6 seconds at 60 Hz so the map stays populated.
    PickupSpec{ /*respawnIntervalTicks*/ 360, /*effectMagnitude*/ 40 },
}};

inline constexpr const PickupSpec& pickupSpecAt(PickupKind k) noexcept {
    return kPickupSpecs[static_cast<std::size_t>(k)];
}

/// M7.5 — user component carried by entity-based pickups (i.e. NOT
/// terrain tiles — `Attribute::RepairBase` keeps the tile lane). One
/// pickup entity = one `Pickup` POD + a `Transform`. `state` tracks
/// the active vs. respawning lane; `respawnIn` is the ticks-until-
/// re-enable counter advanced by `RepairKitSystem`.
///
///   state == 0 → active. AABB-overlapped by a ship → apply effect
///                (per `pickupSpecAt(kind).effectMagnitude`), flip
///                state to 1, attach `DisabledTag`, set `respawnIn`.
///   state == 1 → respawning. System ticks `respawnIn` down each step;
///                on zero, flip back to active and remove `DisabledTag`.
struct Pickup {
    std::uint8_t  kind      = 0;   ///< `PickupKind`
    std::uint8_t  state     = 0;   ///< 0=active, 1=respawning
    std::uint16_t respawnIn = 0;   ///< ticks until re-activate
    std::uint8_t  _pad[4]   = {};
};
static_assert(sizeof(Pickup) == 8, "Pickup must stay 8 bytes");

/// Number of source attribute / visual-JPG pixels that map to one
/// runtime tile along each axis. After M3.3's flat-grid terrain the
/// pxPerTile knob only governs destruction granularity + JPG-paint
/// resolution — there is no longer a per-tile entity, so reducing this
/// is bounded by RAM for the grid (cellsX * cellsY bytes ×2) and the
/// JPG paint cost, not by ECS allocation pressure.
///
/// 2026-05-28 — dropped from 8 to 4 per M5.2 — denser terrain reads as
/// finer detail in the destructible JPG and finer destruction-rect
/// granularity. 4× the cell count (4 → 16 px² per tile) and ~4× the
/// per-paint cost; still well under the per-frame budget at 60 Hz on
/// the audit box.
inline constexpr std::int32_t kImportedPxPerTile = 4;

/// World units per source image pixel — held INDEPENDENT of
/// `kImportedPxPerTile` so changing the tile granularity does NOT
/// change the JPG's on-screen size, the ship's visual scale, the
/// camera zoom, or the level's playable extent in world units.
inline constexpr float kWorldUnitsPerImagePixel = 0.875f;   // = 28.0 / 32

/// World-units per terrain tile.
inline constexpr float kTileWorldUnits =
    static_cast<float>(kImportedPxPerTile) * kWorldUnitsPerImagePixel;

inline constexpr int   kArenaHalfCells   = 16;   // synthetic arena is 33×33 cells

/// M5.1 — maximum humans (slots 0..kMaxHumans-1 are reserved for
/// keyboard players). The 4 hand-rolled key-binding rows in
/// `InputSystem` set this ceiling; bots take the next slot range
/// `[kMaxHumans .. kMaxHumans + numBots)`.
inline constexpr std::uint8_t kMaxHumans = 4;

/// M5.1 — maximum AI bots. Matches the original TOU's stated 63-bot
/// cap. The slot field is `uint8_t`, so the hard ceiling is 255 minus
/// `kMaxHumans` even if this constant grows.
inline constexpr std::uint8_t kMaxBots = 63;

/// M5.1 — maximum total player count (humans + bots). Per-slot
/// bookkeeping arrays in BulletShipCollisionSystem / BulletTerrainSystem
/// / BotControlSystem / etc. size to this constant.
inline constexpr std::size_t kMaxPlayerSlots =
    static_cast<std::size_t>(kMaxHumans) +
    static_cast<std::size_t>(kMaxBots);

/// M6.0 — action enum that abstracts gameplay + UI inputs from raw
/// key codes. `InputSystem` reads `KeyMap` (below) instead of
/// hard-coded GLFW scancodes; `UISystem` (M6.1) routes the same
/// actions to menu navigation.
///
/// Stable bit order — do NOT reorder. `KeyMap` indexes by
/// `static_cast<std::size_t>(Action::Foo)`, and M6.5's
/// `settings.dat` writes the binding table in this order. New
/// actions append before `kActionCount`.
enum class Action : std::uint8_t {
    Thrust       = 0,
    Back         = 1,   ///< reverse thrust (weaker)
    TurnLeft     = 2,
    TurnRight    = 3,
    FireDumb     = 4,   ///< Dumbfire weapon
    FireSpecial  = 5,   ///< Special slot (M3.3+)
    MenuButton   = 6,   ///< original TOU's "launch all" / menu trigger
    Pause        = 7,   ///< M6.4 — pause menu trigger
    UiUp         = 8,
    UiDown       = 9,
    UiLeft       = 10,
    UiRight      = 11,
    UiAccept     = 12,
    UiCancel     = 13,
    kActionCount = 14,
};

/// M6.0 — convenience constant for the enum size as `std::size_t`.
inline constexpr std::size_t kActionCount =
    static_cast<std::size_t>(Action::kActionCount);

/// M6.0 — sentinel for "no key bound." Real GLFW keys are positive.
inline constexpr std::uint16_t kKeyUnbound = 0;

/// M6.0 — per-slot per-action key binding table. `binding[slot][action]`
/// is a GLFW key code (`uint16_t` covers GLFW's whole key range).
/// `KeyMap` is intentionally a plain POD so it round-trips through
/// `settings.dat` (M6.5) via memcpy.
///
/// The default population is `makeDefaultKeyMap()` declared in
/// `InputSystem.hpp` — that's where the GLFW header is already
/// included so DemoTypes.hpp can stay GLFW-free.
struct KeyMap {
    std::uint16_t binding[kMaxHumans][kActionCount] = {};
};
static_assert(sizeof(KeyMap) ==
                  static_cast<std::size_t>(kMaxHumans) *
                  kActionCount *
                  sizeof(std::uint16_t),
              "KeyMap layout is the settings.dat wire shape — keep flat "
              "and uint16_t-sized. Adding an Action requires bumping "
              "the settings.dat version (M6.5).");

/// M6.0b — top-level UI screen the player is currently on. `None` is
/// the gameplay-only path (legacy / CLI-direct-jump); the rest are
/// menu screens owned by `UISystem`.
///
/// Stable enum order — don't reorder. M6.5's settings persistence and
/// future replay determinism depend on a stable wire shape for any
/// UI transition stored in a save file. Add new screens at the end.
enum class UIScreen : std::uint8_t {
    None                 = 0,   ///< gameplay-only; UISystem swallows nothing
    MainMenu             = 1,
    MatchSetup           = 2,
    PlayerSetup          = 3,
    Options              = 4,   ///< M6.5 — top-level Options category list
    Pause                = 5,
    Results              = 6,
    Credits              = 7,
    /// M6.5 — Options sub-screens. Order is stable; append-only.
    OptionsVideo         = 8,
    OptionsAudio         = 9,
    OptionsControls      = 10,
    OptionsGameplay      = 11,
    OptionsAccessibility = 12,
    OptionsBenchmark     = 13,
};

/// M6.0b — typed event emitted by `UISystem::setCurrent` on transition.
/// Subscribers: AudioSystem (UI click SFX), Replay (skip emit while
/// non-gameplay screen is active), HudSystem (suppress combat HUD on
/// menu screens). The engine never produces this — it's example-side.
struct UIScreenChanged {
    UIScreen     from;
    UIScreen     to;
    std::uint8_t _pad[6] = {};
};
static_assert(sizeof(UIScreenChanged) == 8,
              "UIScreenChanged stays 8 bytes — single cache line "
              "shared with PlayerInput / LocalPlayer.");

/// M6.8 — broadcast slot sentinel for `UIToast::slot`. A toast tagged
/// with this slot value is pushed onto every active per-slot stack.
inline constexpr std::uint8_t kToastSlotBroadcast = 0xFFu;

/// M6.8 — typed event for the notification / dialog layer.
/// `ToastRenderSystem` subscribes once at registration; any system can
/// emit (kill feed, pickup confirmation, settings-saved blurb). The
/// channel is render-side only — events are NOT replayed through
/// `WorldSnapshot` and do NOT affect commitHash. `message` is a fixed
/// 28-byte NUL-padded inline buffer so the POD stays trivially copyable
/// and fits 32 B / one cache line.
struct UIToast {
    std::uint8_t           slot          = 0;   ///< 0..3 viewport, or kToastSlotBroadcast
    std::uint8_t           severity      = 0;   ///< 0=info, 1=warn, 2=critical
    std::uint16_t          durationTicks = 0;   ///< how long the toast stays visible
    std::array<char, 28>   message       = {};  ///< NUL-padded inline text
};
static_assert(sizeof(UIToast) == 32,
              "UIToast must stay 32 bytes — render-side POD memcpy'd "
              "through the typed event channel.");

/// M6.5 — typed event emitted by the host whenever the Options→Audio
/// sub-screen cycles a volume knob (and once at startup after loading
/// settings.dat). `AudioSystem` subscribes and applies the new values
/// via miniaudio. Render-side only — never round-tripped through
/// `WorldSnapshot`, never affects `commitHash`. Values are 0..100;
/// AudioSystem scales by 1/100 before handing to ma_engine_set_volume.
struct AudioVolumeChanged {
    std::uint8_t master;
    std::uint8_t music;
    std::uint8_t sfx;
    std::uint8_t _pad = 0;
};
static_assert(sizeof(AudioVolumeChanged) == 4,
              "AudioVolumeChanged stays 4 bytes — single u32 payload "
              "carried through the typed event channel.");

/// Ids handed back by `Engine::registerUserComponent`.
struct UserComponentIds {
    threadmaxx::UserComponentId playerInput;
    threadmaxx::UserComponentId localPlayer;
    threadmaxx::UserComponentId ship;
    threadmaxx::UserComponentId bullet;
    threadmaxx::UserComponentId loadout;  ///< M4.2 — per-ship ammo / reload state
    threadmaxx::UserComponentId sprite;   ///< M4.8 — per-ship sprite reference
    threadmaxx::UserComponentId pickup;   ///< M7.5 — entity-based collectible kit
};

/// M3.3 — flat array of (hp, attribute) per cell. Replaces the per-tile
/// `TerrainBlock` entity model from M2-M3.2: each cell is one byte of
/// HP plus one byte of attribute, so the entire 1024×1120 jungle level
/// at pxPerTile=4 is 144 KiB instead of ~70 000 entities × archetype
/// chunk overhead.
///
/// Coordinate convention:
///   * `worldCellX` / `worldCellY` are signed and centered on origin —
///     same convention used by every system, the painter, and the
///     `.lev` importer.
///   * Internally the grid is stored row-major in image-pixel
///     orientation: index = (halfY - worldCellY) * cellsX +
///                          (worldCellX + halfX).
///     (Matches the LevelLoader's TGA→world mapping; consumers never
///     need to know.)
///
/// Threading: BulletTerrainSystem and TerrainCollisionSystem mutate
/// inside their `ctx.single` lambdas, on the sim thread — both inherit
/// the engine's wave serialization so concurrent reads/writes never
/// alias. The destroy callback fires inline from the same lambda.
struct TerrainGrid {
    std::int32_t cellsX = 0;
    std::int32_t cellsY = 0;
    std::int32_t halfX  = 0;
    std::int32_t halfY  = 0;

    /// 0 = Air (passable); 0xFF = bedrock (indestructible); 1..254 =
    /// destructible solid with that hit-point pool.
    std::vector<std::uint8_t> hp;
    std::vector<Attribute>    attr;

    void reset(std::int32_t cx, std::int32_t cy) {
        cellsX = cx;
        cellsY = cy;
        halfX  = cx / 2;
        halfY  = cy / 2;
        const std::size_t n =
            static_cast<std::size_t>(cx) * static_cast<std::size_t>(cy);
        hp.assign(n, 0);
        attr.assign(n, Attribute::Air);
    }

    bool inBounds(std::int32_t worldCellX, std::int32_t worldCellY) const noexcept {
        const std::int32_t ix = worldCellX + halfX;
        const std::int32_t iy = halfY - worldCellY;
        return ix >= 0 && ix < cellsX && iy >= 0 && iy < cellsY;
    }

    std::size_t indexOf(std::int32_t worldCellX, std::int32_t worldCellY) const noexcept {
        const std::int32_t ix = worldCellX + halfX;
        const std::int32_t iy = halfY - worldCellY;
        return static_cast<std::size_t>(iy) * static_cast<std::size_t>(cellsX) +
               static_cast<std::size_t>(ix);
    }

    std::uint8_t hpAt(std::int32_t worldCellX, std::int32_t worldCellY) const noexcept {
        if (!inBounds(worldCellX, worldCellY)) return 0;
        return hp[indexOf(worldCellX, worldCellY)];
    }

    Attribute attrAt(std::int32_t worldCellX, std::int32_t worldCellY) const noexcept {
        if (!inBounds(worldCellX, worldCellY)) return Attribute::Air;
        return attr[indexOf(worldCellX, worldCellY)];
    }

    void setSolid(std::int32_t worldCellX, std::int32_t worldCellY,
                  std::uint8_t hpVal, Attribute a = Attribute::Solid) noexcept {
        if (!inBounds(worldCellX, worldCellY)) return;
        const std::size_t i = indexOf(worldCellX, worldCellY);
        hp[i]   = hpVal;
        attr[i] = a;
    }

    /// M5.7 / M7.5 — flip a cell to a non-blocking RepairBase pickup.
    /// `hp` stays nonzero so `RepairPickupSystem` can sentinel a still-
    /// live tile vs. one that has already been claimed mid-tick. The
    /// cell does NOT consume on overlap — see `Attribute::RepairBase`
    /// for the regen + edge-triggered special-cycle semantics that
    /// landed with M7.5.
    void setRepairBase(std::int32_t worldCellX, std::int32_t worldCellY,
                       std::uint8_t hpVal = 1) noexcept {
        if (!inBounds(worldCellX, worldCellY)) return;
        const std::size_t i = indexOf(worldCellX, worldCellY);
        hp[i]   = hpVal;
        attr[i] = Attribute::RepairBase;
    }

    void clear(std::int32_t worldCellX, std::int32_t worldCellY) noexcept {
        if (!inBounds(worldCellX, worldCellY)) return;
        const std::size_t i = indexOf(worldCellX, worldCellY);
        hp[i]   = 0;
        attr[i] = Attribute::Air;
    }

    /// M6.9b — count non-Air cells (Solid + Repair). Used by the F3
    /// overlay to show how many destructible blocks remain on the level.
    /// O(cellsX * cellsY); the overlay is opt-in so the scan cost only
    /// fires when visible (largest demo level is 144 KiB; a linear scan
    /// hits ~10 µs which is dwarfed by the buildRenderFrame budget).
    std::size_t solidCellCount() const noexcept {
        std::size_t n = 0;
        for (Attribute a : attr) {
            if (a != Attribute::Air) ++n;
        }
        return n;
    }
};

/// Pick a random `Attribute::Air` cell from `grid`, convert to world
/// coords, and write to (outX, outY). Returns true on success.
///
/// Used by `ShipLifecycleSystem` (post-death respawn) and
/// `RoundRestartSystem` (round reset) so respawning ships don't pile
/// back onto the same opening positions every time. Samples uniformly
/// from the interior (2-cell margin from the perimeter so a ship never
/// spawns flush against a bedrock wall), rejects non-Air cells, gives
/// up after `kMaxRespawnSampleAttempts` tries and reports failure —
/// the caller then falls back to whatever spawn it was going to use
/// anyway.
inline constexpr int kMaxRespawnSampleAttempts = 32;

inline bool sampleRandomRespawn(const TerrainGrid& grid,
                                std::mt19937& rng,
                                float& outX, float& outY) noexcept {
    if (grid.cellsX <= 4 || grid.cellsY <= 4) return false;
    const std::int32_t marginX = std::min<std::int32_t>(2, grid.halfX);
    const std::int32_t marginY = std::min<std::int32_t>(2, grid.halfY);
    const std::int32_t minCx = -grid.halfX + marginX;
    const std::int32_t maxCx =  grid.halfX - marginX;
    const std::int32_t minCy = -grid.halfY + marginY;
    const std::int32_t maxCy =  grid.halfY - marginY;
    if (minCx >= maxCx || minCy >= maxCy) return false;

    std::uniform_int_distribution<std::int32_t> dx(minCx, maxCx);
    std::uniform_int_distribution<std::int32_t> dy(minCy, maxCy);
    for (int attempt = 0; attempt < kMaxRespawnSampleAttempts; ++attempt) {
        const std::int32_t cx = dx(rng);
        const std::int32_t cy = dy(rng);
        if (grid.attrAt(cx, cy) == Attribute::Air) {
            outX = static_cast<float>(cx) * kTileWorldUnits;
            outY = static_cast<float>(cy) * kTileWorldUnits;
            return true;
        }
    }
    return false;
}

} // namespace tou2d
