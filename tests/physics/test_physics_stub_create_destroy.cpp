#include "Check.hpp"

#include "threadmaxx_physics/stub_backend.hpp"

using namespace threadmaxx::physics;

int main() {
    auto backend = makeStubBackend();
    CHECK(backend != nullptr);

    // Create world, body, shape; tear them down in reverse. Each call
    // returns a non-zero id; double-destroy is a no-op.
    PhysicsConfig cfg;
    PhysicsWorldId world = backend->createWorld(cfg);
    CHECK(static_cast<bool>(world));

    ShapeDesc sd;
    sd.type = ShapeType::Box;
    sd.halfExtents = Vec3{0.5f, 0.5f, 0.5f};
    ShapeId shape = backend->createShape(sd);
    CHECK(static_cast<bool>(shape));

    BodyDesc bd;
    bd.type = BodyType::Dynamic;
    bd.position = Vec3{1.0f, 2.0f, 3.0f};
    ShapeId shapes[1] = {shape};
    BodyId body = backend->createBody(world, bd, std::span<const ShapeId>(shapes, 1));
    CHECK(static_cast<bool>(body));

    // bodies share their world; destroy on a wrong world is a no-op.
    PhysicsWorldId otherWorld = backend->createWorld(cfg);
    CHECK(static_cast<bool>(otherWorld));
    backend->destroyBody(otherWorld, body); // wrong world — no crash
    backend->destroyWorld(otherWorld);

    // Body is still alive; sync still reports its state.
    BodyId bodies[1] = {body};
    BodyState outStates[1] = {};
    backend->syncBodiesToGame(world, std::span<const BodyId>(bodies, 1),
                              std::span<BodyState>(outStates, 1));
    CHECK(outStates[0].position.x == 1.0f);
    CHECK(outStates[0].position.y == 2.0f);
    CHECK(outStates[0].position.z == 3.0f);

    backend->destroyBody(world, body);

    // After destroy, sync returns a zeroed state for that handle.
    BodyState outAfter[1] = {};
    backend->syncBodiesToGame(world, std::span<const BodyId>(bodies, 1),
                              std::span<BodyState>(outAfter, 1));
    CHECK(outAfter[0].position.x == 0.0f);
    CHECK(outAfter[0].position.y == 0.0f);

    // Double-destroy is a no-op.
    backend->destroyBody(world, body);

    backend->destroyShape(shape);
    backend->destroyShape(shape); // no-op on stale shape
    backend->destroyWorld(world);
    backend->destroyWorld(world); // no-op on stale world

    // Invalid-id paths are no-ops, not crashes.
    backend->destroyBody(PhysicsWorldId{}, BodyId{});
    backend->destroyWorld(PhysicsWorldId{});
    backend->destroyShape(ShapeId{});

    EXIT_WITH_RESULT();
}
