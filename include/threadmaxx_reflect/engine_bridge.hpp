#pragma once

/// @file engine_bridge.hpp
/// @brief Opt-in engine bridge. Registers every aggregate
/// built-in component (`Transform`, `Velocity`, `Acceleration`,
/// `Health`, `Faction`, `Parent`, `RenderTag`, `UserData`,
/// `PhysicsBodyRef`, `NavAgentRef`, `AnimationStateRef`,
/// `BoundingVolume`, plus the math PODs `Vec3` and `Quat`) into a
/// `TypeRegistry`. Available when `threadmaxx_reflect` is built with
/// `THREADMAXX_REFLECT_HAS_ENGINE_BRIDGE=1` (auto-enabled when the
/// engine target is in the build).

#include "registry.hpp"

#ifdef THREADMAXX_REFLECT_HAS_ENGINE_BRIDGE

namespace threadmaxx::reflect::engine_bridge {

/// @brief Register every aggregate built-in component in the engine
/// into `reg` (default: process-wide instance). Idempotent — safe to
/// call multiple times.
void registerBuiltins(TypeRegistry& reg = TypeRegistry::defaultInstance());

/// @brief Register a user component `T` reflected via
/// `THREADMAXX_REFLECT`. Idempotent. `nameOverride` is optional and
/// recommended.
template <typename T>
const TypeInfo* registerUserComponentType(
    TypeRegistry& reg = TypeRegistry::defaultInstance(),
    std::string_view nameOverride = {}) {
    return reg.registerType<T>(nameOverride);
}

} // namespace threadmaxx::reflect::engine_bridge

#endif // THREADMAXX_REFLECT_HAS_ENGINE_BRIDGE
