/// @file EngineBridge.cpp
/// @brief Engine-bridge `registerBuiltins`. Walks every aggregate
/// component in `threadmaxx/Components.hpp` and pushes its `TypeInfo`
/// into the supplied registry. Applies `THREADMAXX_REFLECT` to engine
/// types inside this TU so the ADL hook is visible at registration
/// time without modifying engine headers.

#include <threadmaxx_reflect/engine_bridge.hpp>

#ifdef THREADMAXX_REFLECT_HAS_ENGINE_BRIDGE

#include <threadmaxx_reflect/macro.hpp>
#include <threadmaxx/Components.hpp>

// Emit the ADL hooks for every engine built-in inside the `threadmaxx`
// namespace so name lookup on `Transform*` etc. finds them. The macro
// is `[[maybe_unused]]`-tagged so clang doesn't flag the TU-local
// hook as "unneeded".
namespace threadmaxx {
THREADMAXX_REFLECT(Vec3,              x, y, z);
THREADMAXX_REFLECT(Quat,              x, y, z, w);
THREADMAXX_REFLECT(Transform,         position, orientation, scale);
THREADMAXX_REFLECT(Velocity,          linear, angular);
THREADMAXX_REFLECT(Acceleration,      linear, angular);
THREADMAXX_REFLECT(RenderTag,         meshId, materialId, flags);
THREADMAXX_REFLECT(UserData,          value);
THREADMAXX_REFLECT(Parent,            parent, localOffset);
THREADMAXX_REFLECT(Health,            current, max);
THREADMAXX_REFLECT(Faction,           id);
THREADMAXX_REFLECT(AnimationStateRef, graph, state, t);
THREADMAXX_REFLECT(PhysicsBodyRef,    handle);
THREADMAXX_REFLECT(NavAgentRef,       handle);
THREADMAXX_REFLECT(BoundingVolume,    min, max);
} // namespace threadmaxx

namespace threadmaxx::reflect::engine_bridge {

void registerBuiltins(TypeRegistry& reg) {
    using namespace ::threadmaxx;
    reg.registerType<Vec3>("Vec3");
    reg.registerType<Quat>("Quat");
    reg.registerType<Transform>("Transform");
    reg.registerType<Velocity>("Velocity");
    reg.registerType<Acceleration>("Acceleration");
    reg.registerType<RenderTag>("RenderTag");
    reg.registerType<UserData>("UserData");
    reg.registerType<Parent>("Parent");
    reg.registerType<Health>("Health");
    reg.registerType<Faction>("Faction");
    reg.registerType<AnimationStateRef>("AnimationStateRef");
    reg.registerType<PhysicsBodyRef>("PhysicsBodyRef");
    reg.registerType<NavAgentRef>("NavAgentRef");
    reg.registerType<BoundingVolume>("BoundingVolume");
}

} // namespace threadmaxx::reflect::engine_bridge

#endif // THREADMAXX_REFLECT_HAS_ENGINE_BRIDGE
