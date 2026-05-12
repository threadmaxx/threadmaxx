#pragma once

#include "Components.hpp"
#include "Handles.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace threadmaxx {

// One renderable entity, flattened to a POD the renderer can consume without
// touching engine internals.
struct RenderInstance {
    EntityHandle entity;
    Transform transform;
    std::int32_t meshId = -1;
    std::int32_t materialId = -1;
    std::uint32_t flags = 0;
    std::uint64_t userData = 0;
};

// Snapshot the renderer sees for a given tick. Owned by the engine; the
// renderer borrows it via std::span and must not retain pointers across
// submitFrame() calls.
struct RenderFrame {
    std::uint64_t tick = 0;
    double simulationTime = 0.0;
    double deltaTime = 0.0;

    std::span<const RenderInstance> instances;
};

} // namespace threadmaxx
