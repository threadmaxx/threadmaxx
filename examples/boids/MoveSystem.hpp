#pragma once

#include <threadmaxx/System.hpp>

// Integrates Velocity into Transform.position with toroidal wrap at the
// window edges. Runs in a wave after BoidsSystem (W:Velocity, R:Velocity
// conflict forces the split).
class BoidsMoveSystem : public threadmaxx::ISystem {
public:
    const char* name() const noexcept override { return "boids_move"; }

    threadmaxx::ComponentSet reads() const noexcept override {
        return threadmaxx::Component::Velocity | threadmaxx::Component::Transform;
    }
    threadmaxx::ComponentSet writes() const noexcept override {
        return threadmaxx::Component::Transform;
    }

    void update(threadmaxx::SystemContext& ctx) override;
};
