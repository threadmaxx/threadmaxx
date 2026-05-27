#pragma once

// tou2d — game-side user component PODs.
//
// Every type here must be trivially copyable; the engine memcpys
// values into chunked storage and never invokes ctors/dtors on user
// components (see threadmaxx/UserComponent.hpp).

#include <threadmaxx/UserComponent.hpp>

#include <cstdint>
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
    float         currentHp      = 100.0f;
    float         maxHp          = 100.0f;
    float         spawnX         = 0.0f;
    float         spawnY         = 0.0f;
    std::uint16_t shipKindIdx    = 0;
    std::uint16_t respawnIn      = 0;     ///< ticks until respawn; 0 = alive
    std::uint16_t kills          = 0;     ///< deathmatch score
    std::uint16_t tilesDestroyed = 0;     ///< secondary terrain-wreckage counter
};
static_assert(sizeof(Ship) == 24, "Ship must stay 24 bytes");

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

/// M4.2 — per-ship weapon ammo + reload state.
///
/// Each weapon has a magazine (`*Ammo`) and a reload timer (`*ReloadIn`,
/// measured in sim ticks). Fire is gated on `ammo > 0 && reloadIn == 0`.
/// On a successful fire `ammo` decrements; when it hits 0 the system
/// sets `reloadIn = kReload*Ticks`. Every tick, a non-zero `reloadIn`
/// decrements; when it reaches 0 the magazine refills to its starting
/// value. The post-fire mid-reload state survives across input bursts
/// — players who hold the fire key get exactly `ammo` shots then a
/// forced silence, matching the original TOU's "weapons can't fire
/// infinitely" feel.
///
/// Kept at 8 bytes so it costs the same dense-storage footprint as
/// PlayerInput / LocalPlayer / Bullet (one cache line carries any
/// four).
struct WeaponLoadout {
    std::uint16_t dumbfireAmmo     = 0;   ///< rounds remaining in magazine
    std::uint16_t dumbfireReloadIn = 0;   ///< ticks until reload finishes; 0 = ready
    std::uint16_t spreadAmmo       = 0;
    std::uint16_t spreadReloadIn   = 0;
};
static_assert(sizeof(WeaponLoadout) == 8, "WeaponLoadout must stay 8 bytes");

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

} // namespace tou2d
