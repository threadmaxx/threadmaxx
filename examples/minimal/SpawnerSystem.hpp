#pragma once

#include <threadmaxx/System.hpp>

// Periodically spawns a few entities with random velocities, and reaps
// entities that drift outside a box. Demonstrates a non-parallel commit
// (the spawner runs single-threaded but its output is committed deterministically).
class SpawnerSystem : public threadmaxx::ISystem {
public:
    const char* name() const noexcept override { return "SpawnerSystem"; }
    void update(threadmaxx::SystemContext& ctx) override;

private:
    // Fixed seed for deterministic mode demos.
    std::uint64_t rngState_ = 0xC0FFEEULL;
    std::uint32_t spawnEvery_ = 30;  // ticks

    std::uint32_t xorshift();
};
