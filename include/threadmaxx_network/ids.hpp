#pragma once

/// @file ids.hpp
/// @brief Network-side identifier types — PeerId / SessionId / TickId /
/// NetEntityId. All zero-default-constructible POD wrappers around
/// integers; the engine's `EntityHandle` stays separate and is bridged
/// via NetEntityId at the replication layer.

#include <cstdint>

namespace threadmaxx::network {

/// @brief Per-connection peer identifier, assigned by the server on
/// Hello/Welcome. `0` is the invalid value.
struct PeerId {
    std::uint32_t value{0};
    constexpr bool valid() const noexcept { return value != 0; }
    friend constexpr bool operator==(PeerId a, PeerId b) noexcept {
        return a.value == b.value;
    }
    friend constexpr bool operator!=(PeerId a, PeerId b) noexcept {
        return a.value != b.value;
    }
};

/// @brief Per-session identifier. Distinct from PeerId — one peer may
/// open several sessions across reconnects.
struct SessionId {
    std::uint64_t value{0};
    constexpr bool valid() const noexcept { return value != 0; }
    friend constexpr bool operator==(SessionId a, SessionId b) noexcept {
        return a.value == b.value;
    }
    friend constexpr bool operator!=(SessionId a, SessionId b) noexcept {
        return a.value != b.value;
    }
};

/// @brief Tick identifier. Wraps the engine's tick counter into a
/// 32-bit field for the wire protocol (lower half).
struct TickId {
    std::uint32_t value{0};
    friend constexpr bool operator==(TickId a, TickId b) noexcept {
        return a.value == b.value;
    }
    friend constexpr bool operator!=(TickId a, TickId b) noexcept {
        return a.value != b.value;
    }
    friend constexpr bool operator<(TickId a, TickId b) noexcept {
        return a.value < b.value;
    }
};

/// @brief Network-facing handle for an entity. The replication layer
/// maps these to the engine's `EntityHandle` at the bridge.
struct NetEntityId {
    std::uint64_t value{0};
    constexpr bool valid() const noexcept { return value != 0; }
    friend constexpr bool operator==(NetEntityId a, NetEntityId b) noexcept {
        return a.value == b.value;
    }
    friend constexpr bool operator!=(NetEntityId a, NetEntityId b) noexcept {
        return a.value != b.value;
    }
};

} // namespace threadmaxx::network
