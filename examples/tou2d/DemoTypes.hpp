#pragma once

// tou2d — game-side user component PODs.
//
// Every type here must be trivially copyable; the engine memcpys
// values into chunked storage and never invokes ctors/dtors on user
// components (see threadmaxx/UserComponent.hpp).

#include <threadmaxx/UserComponent.hpp>

#include <algorithm>
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

/// Marks an entity as one of the local players. `slot` ∈ [0, 3] picks
/// the key-binding row (P1 arrows / P2 WSAD / P3 IJKL / P4 numpad).
///
/// `isBot != 0` switches the input source from keyboard (InputSystem)
/// to seek-and-fire AI (BotControlSystem). Both systems are preStep —
/// BotControlSystem is registered AFTER InputSystem so it overrides
/// the keyboard read for bot ships only.
struct LocalPlayer {
    std::uint8_t slot  = 0;
    std::uint8_t isBot = 0;
    std::uint8_t _pad[6] = {};
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

/// HP per `strength` unit. Picked so Basic-ship strength 3 lands on
/// 150 HP — near M4.4's flat 200 baseline, so combat feels close to
/// the previous calibration while the kinds spread HP across 50..300.
inline constexpr float kShipHpPerStrength = 50.0f;

/// Reference strength/thrust/turn around which per-ship scalars are
/// computed. Basic ship sits exactly here; every other kind is a
/// signed ratio (Bee thrust 10/3 = 3.33×, Destroyer 1.5/3 = 0.5×).
inline constexpr float kShipKindStatReference = 3.0f;

/// 9 ship designs from the manual table (Strength / Thrusters / Turning).
/// Slot indices are stable — DON'T reorder; `Ship.shipKindIdx` is a
/// direct index into this array AND is round-tripped via WorldSnapshot.
inline constexpr ShipKind kShipKinds[] = {
    {{'B','a','s','i','c',' ','s','h','i','p',0,0}, 3.0f, 3.0f,  3.0f}, // 0
    {{'B','a','t','m','a','n',0,0,0,0,0,0},          2.5f, 4.0f,  3.5f}, // 1
    {{'B','2',' ','S','t','e','a','l','t','h',0,0},  2.0f, 5.0f,  4.0f}, // 2
    {{'S','p','e','e','d','i','e',0,0,0,0,0},        1.5f, 7.0f,  3.0f}, // 3
    {{'X',' ','W','i','n','g',0,0,0,0,0,0},          2.5f, 4.5f,  4.0f}, // 4
    {{'T','i','e',' ','F','i','g','h','t','e','r',0},2.5f, 5.0f,  4.5f}, // 5
    {{'B','e','e',0,0,0,0,0,0,0,0,0},                1.0f, 10.0f, 6.5f}, // 6
    {{'F','l','y',0,0,0,0,0,0,0,0,0},                4.0f, 3.0f,  2.0f}, // 7
    {{'D','e','s','t','r','o','y','e','r',0,0,0},    6.0f, 1.5f,  1.0f}, // 8
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
enum class Attribute : std::uint8_t {
    Air    = 0,
    Solid  = 1,
    Damage = 2,
};

/// In-flight projectile. `weaponKind == 0` is Dumbfire (M3.1);
/// `weaponKind == 1` is Spread (M3.3). `ownerSlot` mirrors the firing
/// ship's `LocalPlayer::slot` so kill credit can route to the right
/// player score on impact.
struct Bullet {
    float         ttlSeconds = 0.0f;
    std::uint8_t  damage     = 0;
    std::uint8_t  weaponKind = 0;
    std::uint16_t ownerSlot  = 0;
};
static_assert(sizeof(Bullet) == 8, "Bullet must stay 8 bytes");

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
///     Spread cooldown is the burst-to-burst gap (~370 ms).
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
/// Bumped from 8 → 16 bytes in M4.5. Still cheap (≤4 ships × 16 B);
/// the two trailing pad uint16s keep the layout stable if a third
/// weapon ever lands.
struct WeaponLoadout {
    std::uint16_t dumbfireAmmo     = 0;   ///< rounds remaining in magazine
    std::uint16_t dumbfireReloadIn = 0;   ///< ticks until mag refill; 0 = ready
    std::uint16_t dumbfireCooldown = 0;   ///< ticks until next shot loader-ready
    std::uint16_t _pad0            = 0;
    std::uint16_t spreadAmmo       = 0;
    std::uint16_t spreadReloadIn   = 0;
    std::uint16_t spreadCooldown   = 0;   ///< burst-to-burst gap
    std::uint16_t _pad1            = 0;
};
static_assert(sizeof(WeaponLoadout) == 16, "WeaponLoadout must stay 16 bytes");

/// Magazine sizes — chosen so a sustained burst lands a satisfying
/// volley before the forced reload. Dumbfire fires every 8 ticks
/// (M3.1's `kFireCooldownTicks`) so a 12-round mag = 96 ticks = 1.6 s
/// of continuous fire. Spread's 4-burst mag = 4 × 18 = 72 ticks ≈ 1.2 s.
inline constexpr std::uint16_t kDumbfireMagazine = 12;
inline constexpr std::uint16_t kSpreadMagazine   =  4;

/// Reload durations — wall-clock-comparable to original TOU's reload
/// feel (≈ 1.25 s for the basic weapon, ≈ 1.5 s for the burst).
inline constexpr std::uint16_t kDumbfireReloadTicks = 75;   // 60 Hz → 1.25 s
inline constexpr std::uint16_t kSpreadReloadTicks   = 90;   // 60 Hz → 1.50 s

/// Number of source attribute / visual-JPG pixels that map to one
/// runtime tile along each axis. After M3.3's flat-grid terrain the
/// pxPerTile knob only governs destruction granularity + JPG-paint
/// resolution — there is no longer a per-tile entity, so reducing this
/// is bounded by RAM for the grid (cellsX * cellsY bytes ×2) and the
/// JPG paint cost, not by ECS allocation pressure.
inline constexpr std::int32_t kImportedPxPerTile = 8;

/// World units per source image pixel — held INDEPENDENT of
/// `kImportedPxPerTile` so changing the tile granularity does NOT
/// change the JPG's on-screen size, the ship's visual scale, the
/// camera zoom, or the level's playable extent in world units.
inline constexpr float kWorldUnitsPerImagePixel = 0.875f;   // = 28.0 / 32

/// World-units per terrain tile.
inline constexpr float kTileWorldUnits =
    static_cast<float>(kImportedPxPerTile) * kWorldUnitsPerImagePixel;

inline constexpr int   kArenaHalfCells   = 16;   // synthetic arena is 33×33 cells

/// Ids handed back by `Engine::registerUserComponent`.
struct UserComponentIds {
    threadmaxx::UserComponentId playerInput;
    threadmaxx::UserComponentId localPlayer;
    threadmaxx::UserComponentId ship;
    threadmaxx::UserComponentId bullet;
    threadmaxx::UserComponentId loadout;  ///< M4.2 — per-ship ammo / reload state
    threadmaxx::UserComponentId sprite;   ///< M4.8 — per-ship sprite reference
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

    void clear(std::int32_t worldCellX, std::int32_t worldCellY) noexcept {
        if (!inBounds(worldCellX, worldCellY)) return;
        const std::size_t i = indexOf(worldCellX, worldCellY);
        hp[i]   = 0;
        attr[i] = Attribute::Air;
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
