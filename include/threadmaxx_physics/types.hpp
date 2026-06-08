#pragma once

#include <cstdint>

/// Opaque identifier PODs for the threadmaxx_physics library.
///
/// All ids are trivially-copyable handles into the backend's internal
/// tables. The library never inspects the bit layout — backends are
/// free to encode (slot, generation) however they like. A zero-valued
/// id is reserved as "invalid" and must be returned by failing
/// `create*` calls (e.g. world-not-found).
namespace threadmaxx::physics {

/// Identifies a physics world owned by an `IPhysicsBackend`. Multiple
/// worlds can coexist behind the same backend (e.g. for split-screen
/// or sandboxed simulation).
struct PhysicsWorldId {
    std::uint64_t value{};

    constexpr bool operator==(const PhysicsWorldId&) const noexcept = default;
    constexpr explicit operator bool() const noexcept { return value != 0; }
};

/// Identifies a rigid body inside a `PhysicsWorldId`. Body ids are
/// scoped per-world — the same numeric value may refer to different
/// bodies across worlds.
struct BodyId {
    std::uint64_t value{};

    constexpr bool operator==(const BodyId&) const noexcept = default;
    constexpr explicit operator bool() const noexcept { return value != 0; }
};

/// Identifies a collider shape in the backend's shape registry. Shapes
/// are world-scoped at the backend's discretion: the StubBackend uses
/// a process-wide registry, real backends may use per-world.
struct ShapeId {
    std::uint64_t value{};

    constexpr bool operator==(const ShapeId&) const noexcept = default;
    constexpr explicit operator bool() const noexcept { return value != 0; }
};

/// Identifies a constraint between two bodies (joints). Reserved for
/// batch P6; declared here so the type lives next to its siblings.
struct JointId {
    std::uint64_t value{};

    constexpr bool operator==(const JointId&) const noexcept = default;
    constexpr explicit operator bool() const noexcept { return value != 0; }
};

} // namespace threadmaxx::physics
