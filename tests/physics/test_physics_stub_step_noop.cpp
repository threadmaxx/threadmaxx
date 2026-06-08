#include "Check.hpp"

#include "threadmaxx_physics/stub_backend.hpp"

using namespace threadmaxx::physics;

int main() {
    // Post-P4 contract: `stepWorld` advances every non-Static alive
    // body by `linearVelocity * dt`. Static bodies and invalid worlds
    // remain "no-ops" — this test pins that quiet half of the surface.
    //
    // Pre-P4 this file asserted that a Dynamic body stayed put. P4
    // ships kinematic integration, so the meaningful "no movement"
    // assertions now live on Static bodies and on the
    // unknown-world path. The Dynamic-body integration semantics
    // themselves are covered by `test_physics_step_linear`.
    auto backend = makeStubBackend();
    PhysicsConfig cfg;
    PhysicsWorldId world = backend->createWorld(cfg);

    BodyDesc bd;
    bd.type = BodyType::Static;
    bd.position = Vec3{2.0f, 3.0f, 4.0f};
    bd.linearVelocity = Vec3{1.0f, 0.0f, 0.0f};
    bd.angularVelocity = Vec3{0.0f, 1.0f, 0.0f};
    BodyId body = backend->createBody(world, bd, std::span<const ShapeId>{});

    // 60 ticks of 1/60s — would move a Dynamic body by 1 metre, but a
    // Static body must stay exactly at the create-time pose.
    for (int i = 0; i < 60; ++i) {
        backend->stepWorld(world, 1.0f / 60.0f);
    }

    BodyId bodies[1] = {body};
    BodyState states[1] = {};
    backend->syncBodiesToGame(world, std::span<const BodyId>(bodies, 1),
                              std::span<BodyState>(states, 1));

    CHECK(states[0].position.x == 2.0f);
    CHECK(states[0].position.y == 3.0f);
    CHECK(states[0].position.z == 4.0f);
    // Rotation also untouched on Static.
    CHECK(states[0].rotation.x == 0.0f);
    CHECK(states[0].rotation.y == 0.0f);
    CHECK(states[0].rotation.z == 0.0f);
    CHECK(states[0].rotation.w == 1.0f);
    // Velocity preserved verbatim — the integrator never damps it.
    CHECK(states[0].linearVelocity.x == 1.0f);
    CHECK(states[0].angularVelocity.y == 1.0f);

    // Stepping an invalid world is a no-op (no crash).
    backend->stepWorld(PhysicsWorldId{}, 1.0f / 60.0f);

    backend->destroyBody(world, body);
    backend->destroyWorld(world);

    EXIT_WITH_RESULT();
}
