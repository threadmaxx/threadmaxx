#include "Check.hpp"

#include "threadmaxx_physics/backend.hpp"
#include "threadmaxx_physics/stub_backend.hpp"

// P3 — body lifecycle: create body, getBodyState returns the initial
// pose; destroyBody invalidates the handle so the next getBodyState
// returns nullopt. Also covers the cross-world invariant (a body id
// from world A is not readable from world B).

using namespace threadmaxx::physics;

int main() {
    auto backend = makeStubBackend();
    CHECK(backend != nullptr);

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
    bd.rotation = Quat{0.0f, 0.0f, 0.0f, 1.0f};
    bd.linearVelocity = Vec3{4.0f, 5.0f, 6.0f};
    bd.angularVelocity = Vec3{7.0f, 8.0f, 9.0f};

    BodyId body = backend->createBody(world, bd,
                                      std::span<const ShapeId>(&shape, 1));
    CHECK(static_cast<bool>(body));

    auto state = backend->getBodyState(world, body);
    CHECK(state.has_value());
    CHECK(state->position.x == 1.0f);
    CHECK(state->position.y == 2.0f);
    CHECK(state->position.z == 3.0f);
    CHECK(state->rotation.w == 1.0f);
    CHECK(state->linearVelocity.x == 4.0f);
    CHECK(state->linearVelocity.y == 5.0f);
    CHECK(state->linearVelocity.z == 6.0f);
    CHECK(state->angularVelocity.x == 7.0f);
    CHECK(state->angularVelocity.y == 8.0f);
    CHECK(state->angularVelocity.z == 9.0f);

    // Stale-id paths return nullopt.
    CHECK(!backend->getBodyState(world, BodyId{}).has_value());

    backend->destroyBody(world, body);
    CHECK(!backend->getBodyState(world, body).has_value());

    // A second body recycles the slot; the original handle must NOT
    // alias it (generation tag flipped).
    BodyId fresh = backend->createBody(world, bd,
                                       std::span<const ShapeId>(&shape, 1));
    CHECK(static_cast<bool>(fresh));
    CHECK(fresh.value != body.value);
    CHECK(!backend->getBodyState(world, body).has_value());
    CHECK(backend->getBodyState(world, fresh).has_value());

    // Cross-world isolation: a body from world A must not be readable
    // through world B even if the numeric slot happens to match.
    PhysicsWorldId otherWorld = backend->createWorld(cfg);
    CHECK(!backend->getBodyState(otherWorld, fresh).has_value());

    backend->destroyBody(world, fresh);
    backend->destroyShape(shape);
    backend->destroyWorld(otherWorld);
    backend->destroyWorld(world);

    // Post-world-destroy: even a previously-valid id reports nullopt.
    CHECK(!backend->getBodyState(world, fresh).has_value());

    EXIT_WITH_RESULT();
}
