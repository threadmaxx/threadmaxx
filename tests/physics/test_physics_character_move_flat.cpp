#include "Check.hpp"

#include "threadmaxx_physics/character.hpp"
#include "threadmaxx_physics/stub_backend.hpp"

#include <cmath>

// P7 — character on a flat floor receiving forward intent moves
// forward at `maxMoveSpeed`. Geometry: floor centered at y=-0.5 with
// half-extents (50, 0.5, 50), top face at y=0. Character starts with
// capsule center at (0, 0.9, 0) — bottom of the capsule touching the
// floor's top face (height=1.8, so half-height=0.9).
//
// Drive 10 sub-ticks of 0.1s each with unit-magnitude intent along +X.
// At maxMoveSpeed=5 m/s × 1.0s total, the expected horizontal travel
// is 5.0 m. Tolerance is generous (1e-3) — sub-stepping accumulates
// some kCollisionBackoff slack but on a flat floor with no obstacle
// the sweep is always clear.

using namespace threadmaxx::physics;

int main() {
    auto backend = makeStubBackend();
    PhysicsConfig cfg;
    PhysicsWorldId world = backend->createWorld(cfg);

    // Floor.
    ShapeDesc floorShape;
    floorShape.type = ShapeType::Box;
    floorShape.halfExtents = Vec3{50.0f, 0.5f, 50.0f};
    ShapeId floorShapeId = backend->createShape(floorShape);
    ShapeId floorShapes[1] = {floorShapeId};

    BodyDesc floorBody;
    floorBody.type = BodyType::Static;
    floorBody.position = Vec3{0.0f, -0.5f, 0.0f};
    BodyId floor = backend->createBody(world, floorBody,
                                       std::span<const ShapeId>(floorShapes, 1));
    (void)floor;

    CharacterControllerDesc desc;
    desc.startPosition = Vec3{0.0f, 0.9f, 0.0f};
    desc.radius = 0.5f;
    desc.height = 1.8f;
    desc.stepHeight = 0.3f;
    desc.maxMoveSpeed = 5.0f;

    CharacterController character(*backend, world, desc);

    // Initial state: should be grounded on the floor.
    CHECK(character.state().grounded);
    CHECK(std::fabs(character.state().position.y - 0.9f) < 1.0e-4f);

    CharacterInput input;
    input.moveIntent = Vec3{1.0f, 0.0f, 0.0f};  // forward, full speed

    constexpr int kSubTicks = 10;
    constexpr float kDt = 0.1f;
    for (int i = 0; i < kSubTicks; ++i) {
        character.move(input, kDt);
    }

    // Expected horizontal distance: maxMoveSpeed * total_dt = 5.0 * 1.0 = 5.0 m
    const float expectedX = desc.maxMoveSpeed * static_cast<float>(kSubTicks) * kDt;
    CHECK(std::fabs(character.state().position.x - expectedX) < 1.0e-3f);
    CHECK(std::fabs(character.state().position.z) < 1.0e-4f);
    CHECK(std::fabs(character.state().position.y - 0.9f) < 1.0e-4f);
    CHECK(character.state().grounded);
    // Horizontal velocity matches the most recent applied intent.
    CHECK(std::fabs(character.state().velocity.x - desc.maxMoveSpeed) < 1.0e-4f);
    CHECK(std::fabs(character.state().velocity.y) < 1.0e-4f);

    // Half-intent → half speed.
    CharacterInput halfInput;
    halfInput.moveIntent = Vec3{0.5f, 0.0f, 0.0f};
    const float xBefore = character.state().position.x;
    for (int i = 0; i < kSubTicks; ++i) {
        character.move(halfInput, kDt);
    }
    const float halfTravel = (desc.maxMoveSpeed * 0.5f) *
                             static_cast<float>(kSubTicks) * kDt;
    CHECK(std::fabs(character.state().position.x - (xBefore + halfTravel)) < 1.0e-3f);

    // Zero intent → no horizontal motion; grounded stays true.
    CharacterInput zeroInput;
    const float xRest = character.state().position.x;
    for (int i = 0; i < kSubTicks; ++i) {
        character.move(zeroInput, kDt);
    }
    CHECK(std::fabs(character.state().position.x - xRest) < 1.0e-4f);
    CHECK(character.state().grounded);

    backend->destroyShape(floorShapeId);
    backend->destroyWorld(world);
    EXIT_WITH_RESULT();
}
