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

/// Ids handed back by `Engine::registerUserComponent`. One per
/// user-component type; carried on the IGame to give every system a
/// resolved handle without re-doing the typeid() lookup.
struct UserComponentIds {
    threadmaxx::UserComponentId playerInput;
    threadmaxx::UserComponentId localPlayer;
    threadmaxx::UserComponentId ship;
};

} // namespace tou2d
