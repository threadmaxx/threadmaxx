#include "Check.hpp"

#include "threadmaxx_physics/backend.hpp"
#include "threadmaxx_physics/stub_backend.hpp"

// P3 — kinematic teleport: setBodyTransform writes the body pose
// directly. A subsequent getBodyState reflects the new position and
// rotation, and a sync batch reads the same value back. setBodyTransform
// on an invalid id must no-op (no crash, no state mutation).

using namespace threadmaxx::physics;

int main() {
    auto backend = makeStubBackend();
    CHECK(backend != nullptr);

    PhysicsConfig cfg;
    PhysicsWorldId world = backend->createWorld(cfg);
    CHECK(static_cast<bool>(world));

    ShapeDesc sd;
    sd.type = ShapeType::Capsule;
    sd.radius = 0.5f;
    sd.height = 1.8f;
    ShapeId shape = backend->createShape(sd);

    BodyDesc bd;
    bd.type = BodyType::Kinematic;
    bd.position = Vec3{0.0f, 0.0f, 0.0f};
    bd.rotation = Quat{0.0f, 0.0f, 0.0f, 1.0f};
    BodyId body = backend->createBody(world, bd,
                                      std::span<const ShapeId>(&shape, 1));
    CHECK(static_cast<bool>(body));

    // Sanity: initial pose mirrors the descriptor.
    auto initial = backend->getBodyState(world, body);
    CHECK(initial.has_value());
    CHECK(initial->position.x == 0.0f);
    CHECK(initial->position.y == 0.0f);
    CHECK(initial->position.z == 0.0f);

    // Teleport to a new pose.
    const Vec3 target{10.0f, 5.0f, -3.0f};
    // 90° rotation around Y as a unit quat: (0, sin(45°), 0, cos(45°))
    const float s = 0.70710678f;
    const Quat targetRot{0.0f, s, 0.0f, s};
    backend->setBodyTransform(world, body, target, targetRot);

    // getBodyState now reflects the teleport.
    auto teleported = backend->getBodyState(world, body);
    CHECK(teleported.has_value());
    CHECK(teleported->position.x == 10.0f);
    CHECK(teleported->position.y == 5.0f);
    CHECK(teleported->position.z == -3.0f);
    CHECK(teleported->rotation.x == 0.0f);
    CHECK(teleported->rotation.y == s);
    CHECK(teleported->rotation.z == 0.0f);
    CHECK(teleported->rotation.w == s);

    // syncBodiesToGame mirrors the same value.
    BodyState out[1];
    backend->syncBodiesToGame(world,
                              std::span<const BodyId>(&body, 1),
                              std::span<BodyState>(out, 1));
    CHECK(out[0].position.x == 10.0f);
    CHECK(out[0].position.y == 5.0f);
    CHECK(out[0].position.z == -3.0f);

    // Teleport on a stale / invalid id is a no-op (and crucially
    // doesn't crash). The original body's state must stay put.
    backend->setBodyTransform(world, BodyId{},
                              Vec3{99.0f, 99.0f, 99.0f},
                              Quat{0.0f, 0.0f, 0.0f, 1.0f});
    auto still = backend->getBodyState(world, body);
    CHECK(still.has_value());
    CHECK(still->position.x == 10.0f);

    // Teleport against an unknown world is also a no-op.
    PhysicsWorldId fakeWorld{0xDEADBEEFull};
    backend->setBodyTransform(fakeWorld, body,
                              Vec3{1.0f, 1.0f, 1.0f},
                              Quat{0.0f, 0.0f, 0.0f, 1.0f});
    auto unchanged = backend->getBodyState(world, body);
    CHECK(unchanged.has_value());
    CHECK(unchanged->position.x == 10.0f);

    backend->destroyBody(world, body);
    backend->destroyShape(shape);
    backend->destroyWorld(world);

    EXIT_WITH_RESULT();
}
