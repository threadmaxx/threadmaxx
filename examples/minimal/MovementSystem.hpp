#pragma once

#include <threadmaxx/System.hpp>

// Advances every entity's position by velocity * dt. Pure read of the world,
// emits SetTransform commands per chunk — the perfect parallel pattern.
class MovementSystem : public threadmaxx::ISystem {
public:
    const char* name() const noexcept override { return "MovementSystem"; }
    void update(threadmaxx::SystemContext& ctx) override;
};
