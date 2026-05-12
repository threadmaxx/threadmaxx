#pragma once

#include <threadmaxx/System.hpp>

// Classic Reynolds boids: each entity steers based on neighbors within a
// perception radius using three rules — alignment, cohesion, separation.
// Reads Transform (positions) and Velocity (neighbor velocities), writes
// Velocity. The pairing with MoveSystem (W=Transform, R=Velocity) forces
// the scheduler to place them in distinct waves: boids steers, then move
// integrates, every tick.
//
// Neighbor search is O(N^2) — fine for the 256-boid example; a spatial
// hash would slot in cleanly behind the same interface.
class BoidsSystem : public threadmaxx::ISystem {
public:
    const char* name() const noexcept override { return "boids"; }

    threadmaxx::ComponentSet reads() const noexcept override {
        return threadmaxx::Component::Transform | threadmaxx::Component::Velocity;
    }
    threadmaxx::ComponentSet writes() const noexcept override {
        return threadmaxx::Component::Velocity;
    }

    void update(threadmaxx::SystemContext& ctx) override;
};
