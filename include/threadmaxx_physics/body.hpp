#pragma once

#include "threadmaxx/Components.hpp"

#include <cstdint>

/// Rigid body descriptor + per-tick state PODs.
///
/// `BodyDesc` is the create-time blueprint; `BodyState` is the
/// per-tick read-back. Both are trivially-copyable so they round-trip
/// through `memcpy`, snapshot, and span-based batch sync paths.
namespace threadmaxx::physics {

/// Reuse the core engine's math PODs so a single `Transform` from
/// game code can be assigned straight into a `BodyDesc::position` /
/// `rotation`.
using ::threadmaxx::Quat;
using ::threadmaxx::Vec3;

/// Behavior class of a rigid body.
///
/// - **Static**   — never moves; backend may skip integration entirely.
/// - **Dynamic**  — full simulation; forces / gravity / collisions
///   integrate into position over time.
/// - **Kinematic** — script-driven; backend uses the body's velocity
///   but never applies forces. Set the desired motion via
///   `setLinearVelocity` / direct position teleport.
enum class BodyType : std::uint8_t {
    Static    = 0,
    Dynamic   = 1,
    Kinematic = 2,
};

/// Create-time blueprint for a rigid body. Trivially-copyable POD —
/// host code constructs one on the stack and hands it to
/// `IPhysicsBackend::createBody`.
struct BodyDesc {
    BodyType type{BodyType::Dynamic};

    Vec3 position{};
    Quat rotation{};
    Vec3 linearVelocity{};
    Vec3 angularVelocity{};

    float mass{1.0f};
    float friction{0.5f};
    float restitution{0.0f};

    bool enableCCD{false};
    bool canSleep{true};
};

/// Per-tick state read back from the backend after `stepWorld`. The
/// engine writes this into `Transform` / `Velocity` components via
/// the sync helpers (see batch P3 `sync.hpp`).
struct BodyState {
    Vec3 position{};
    Quat rotation{};
    Vec3 linearVelocity{};
    Vec3 angularVelocity{};
};

} // namespace threadmaxx::physics
