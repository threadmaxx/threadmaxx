#include "Check.hpp"

#include "threadmaxx_physics/stub_backend.hpp"

using namespace threadmaxx::physics;

int main() {
    // P1 contract: StubBackend::stepWorld is a no-op. A dynamic body
    // with a non-zero linearVelocity does NOT move when the world is
    // stepped. Kinematic integration lands in P4 (`position +=
    // linearVelocity * dt`). Locking this in here means the day P4
    // ships, this test gets revised — and stale assumptions in other
    // batches surface before they corrupt later work.
    auto backend = makeStubBackend();
    PhysicsConfig cfg;
    PhysicsWorldId world = backend->createWorld(cfg);

    BodyDesc bd;
    bd.type = BodyType::Dynamic;
    bd.position = Vec3{0.0f, 0.0f, 0.0f};
    bd.linearVelocity = Vec3{1.0f, 0.0f, 0.0f};
    BodyId body = backend->createBody(world, bd, std::span<const ShapeId>{});

    // Step for what should be 1 second of simulated time — 60 ticks at
    // 60 Hz. Stub doesn't integrate, so position stays at origin.
    for (int i = 0; i < 60; ++i) {
        backend->stepWorld(world, 1.0f / 60.0f);
    }

    BodyId bodies[1] = {body};
    BodyState states[1] = {};
    backend->syncBodiesToGame(world, std::span<const BodyId>(bodies, 1),
                              std::span<BodyState>(states, 1));

    CHECK(states[0].position.x == 0.0f);
    CHECK(states[0].position.y == 0.0f);
    CHECK(states[0].position.z == 0.0f);
    // Velocity is preserved (stub stores the create-time value).
    CHECK(states[0].linearVelocity.x == 1.0f);

    // Stepping an invalid world is a no-op (no crash).
    backend->stepWorld(PhysicsWorldId{}, 1.0f / 60.0f);

    EXIT_WITH_RESULT();
}
