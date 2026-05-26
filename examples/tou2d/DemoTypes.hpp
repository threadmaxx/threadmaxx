#pragma once

// tou2d — game-side user component PODs.
//
// Every type here must be trivially copyable; the engine memcpys
// values into chunked storage and never invokes ctors/dtors on user
// components (see threadmaxx/UserComponent.hpp).

#include <threadmaxx/UserComponent.hpp>

#include <cstdint>

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
    std::uint8_t fireBasic    = 0;  ///< Wired in M3
    std::uint8_t fireSpecial  = 0;  ///< Wired in M3
    std::uint8_t menuButton   = 0;  ///< "Launch all" / menu in TOU
    std::uint8_t _pad         = 0;
};
static_assert(sizeof(PlayerInput) == 8, "PlayerInput must stay 8 bytes");

/// Marks an entity as one of the local players. `slot` ∈ [0, 3] picks
/// the key-binding row (matches TOU's P1 ↑↓←→ / P2 WSAD / P3 IJKL /
/// P4 numpad). Drives which keyboard row InputSystem maps into the
/// entity's PlayerInput.
struct LocalPlayer {
    std::uint8_t slot = 0;
    std::uint8_t _pad[7] = {};
};
static_assert(sizeof(LocalPlayer) == 8, "LocalPlayer must stay 8 bytes");

/// Ship runtime state — kept tiny in M1; expanded in M3 (weapon, ammo,
/// score). The ship's geometry/look is referenced by `shipKindIdx`
/// (M4 imports `.SHP` and populates a table; until then index 0 is the
/// hard-coded "basic ship").
struct Ship {
    float        currentHp   = 100.0f;
    float        maxHp       = 100.0f;
    std::uint16_t shipKindIdx = 0;
    std::uint16_t _pad0      = 0;
    std::uint32_t _pad1      = 0;
};
static_assert(sizeof(Ship) == 16, "Ship must stay 16 bytes");

/// Tile-grid terrain classification. The byte is sourced from the
/// imported `.lev` attribute layer (M2.5/M2.6) and rounded into one of
/// these categories at load time. Per-tile semantics:
///
///   * `Air`     — no collision, no damage. Drawn empty.
///   * `Solid`   — blocks ship movement, blocks projectiles. HP=0xFF
///                 means indestructible bedrock; anything lower is
///                 chip-away terrain (M3 will mutate this).
///   * `Damage`  — passable but applies a per-tick HP drain on overlap
///                 (lava / acid / spikes in the original).
enum class Attribute : std::uint8_t {
    Air    = 0,
    Solid  = 1,
    Damage = 2,
};

/// Per-tile terrain state. Spawned as a regular entity for every
/// non-Air cell of the active level so the chunk-iteration paths in
/// CollisionSystem and (M3) WeaponDamageSystem can walk the dense
/// arrays just like any other game-side entity.
///
/// Air tiles are *not* spawned — empty cells contribute nothing.
struct TerrainBlock {
    Attribute    attr   = Attribute::Solid;
    std::uint8_t hp     = 0xFF;       ///< 0xFF = indestructible.
    std::int16_t cellX  = 0;          ///< Grid coordinate, signed so the world can be centered on origin.
    std::int16_t cellY  = 0;
    std::uint8_t _pad[2] = {};
};
static_assert(sizeof(TerrainBlock) == 8, "TerrainBlock must stay 8 bytes");

/// In-flight projectile. Spawned by WeaponFireSystem, advanced by
/// ProjectileSystem (integrates Transform via the entity's Velocity +
/// decrements `ttlSeconds`), consumed by BulletTerrainSystem on tile
/// contact. The bullet entity also carries Transform + Velocity +
/// RenderTag so it renders as a small forward-traveling cube and
/// participates in the standard movement integration.
///
/// `weaponKind == 0` is Dumbfire — the one weapon shipped in M3.1.
/// Other weapon kinds land in M3.2+.
struct Bullet {
    float         ttlSeconds = 0.0f;
    std::uint8_t  damage     = 0;
    std::uint8_t  weaponKind = 0;
    std::uint16_t ownerSlot  = 0;
};
static_assert(sizeof(Bullet) == 8, "Bullet must stay 8 bytes");

/// Number of source attribute / visual-JPG pixels that map to one
/// runtime tile along each axis. Picked so a typical 1000×1091
/// imported level produces ~32×35 tiles — comfortably visible inside
/// the ~11×7-tile camera viewport without flooding the scene with
/// cubes. Reducing this gives finer destruction granularity (each
/// shot punches a smaller hole) at the cost of many more terrain
/// entities; see TOU_PLAN M3+ for the path toward 1 px/tile.
inline constexpr std::int32_t kImportedPxPerTile = 32;

/// World units per source image pixel — held INDEPENDENT of
/// `kImportedPxPerTile` so changing the tile granularity does NOT
/// change the JPG's on-screen size, the ship's visual scale, the
/// camera zoom, or the level's playable extent in world units. At
/// the M3.1 sweet-spot of 32 px/tile this gives kTileWorldUnits =
/// 28 (matching the original M1 value); halving px/tile halves
/// kTileWorldUnits proportionally so the cell grid stays at the
/// same overall world size, just denser.
inline constexpr float kWorldUnitsPerImagePixel = 0.875f;   // = 28.0 / 32

/// World-units per terrain tile = pxPerTile × wuPerImagePixel.
/// Derived so the cell grid's world extent stays ≈ constant as
/// `kImportedPxPerTile` changes.
inline constexpr float kTileWorldUnits =
    static_cast<float>(kImportedPxPerTile) * kWorldUnitsPerImagePixel;

inline constexpr int   kArenaHalfCells   = 16;   // synthetic arena is 33×33 cells

/// Ids handed back by `Engine::registerUserComponent`. One per
/// user-component type; carried on the IGame to give every system a
/// resolved handle without re-doing the typeid() lookup.
struct UserComponentIds {
    threadmaxx::UserComponentId playerInput;
    threadmaxx::UserComponentId localPlayer;
    threadmaxx::UserComponentId ship;
    threadmaxx::UserComponentId terrainBlock;
    threadmaxx::UserComponentId bullet;
};

} // namespace tou2d
