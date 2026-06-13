/// @file BackendDefaults.cpp
/// @brief Default out-of-line definitions for non-pure
/// `IPhysicsBackend` virtuals introduced in v1.x. Backends that don't
/// override fall through here so they don't break the link surface.

#include "threadmaxx_physics/backend.hpp"
#include "threadmaxx_physics/diagnostics.hpp"

namespace threadmaxx::physics {

PhysicsWorldStats IPhysicsBackend::worldStats(PhysicsWorldId) const noexcept {
    return PhysicsWorldStats{};
}

} // namespace threadmaxx::physics
