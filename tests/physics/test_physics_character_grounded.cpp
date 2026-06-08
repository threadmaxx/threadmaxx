#include "Check.hpp"

#include "threadmaxx_physics/character.hpp"
#include "threadmaxx_physics/stub_backend.hpp"

#include <cmath>

// P7 — the grounded flag transitions correctly across an arc:
//   - true on the floor at creation,
//   - false immediately after a jump,
//   - false while mid-air,
//   - true again after landing.
//
// Geometry: a single flat floor at y=-0.5 with half-extents
// (50, 0.5, 50), top face at y=0. Character has feet at the floor
// (capsule center y=0.9, height=1.8).
//
// jumpSpeed = 5.0 m/s with gravity=-9.81 and dt=0.1s. The character
// reaches a peak roughly at y=2.4, then falls back. The
// CharacterController's descent-CCD sweep catches the high-velocity
// landing tick that would otherwise tunnel through the floor at
// dt=0.1.

using namespace threadmaxx::physics;

int main() {
    auto backend = makeStubBackend();
    PhysicsConfig cfg;
    PhysicsWorldId world = backend->createWorld(cfg);

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
    desc.gravity = -9.81f;

    CharacterController character(*backend, world, desc);

    // 1. Initial state on the floor: grounded.
    CHECK(character.state().grounded);
    CHECK(std::fabs(character.state().position.y - 0.9f) < 1.0e-4f);
    CHECK(std::fabs(character.state().velocity.y) < 1.0e-4f);

    // 2. Jump tick: grounded immediately drops to false.
    CharacterInput jumpInput;
    jumpInput.jump = true;
    jumpInput.jumpSpeed = 5.0f;
    character.move(jumpInput, 0.1f);
    CHECK(!character.state().grounded);
    // Rising — velocity.y positive, position.y above start.
    CHECK(character.state().velocity.y > 0.0f);
    CHECK(character.state().position.y > 0.9f);

    // 3. Stay mid-air through the ascent / apex.
    CharacterInput noJump;
    for (int i = 0; i < 4; ++i) {
        character.move(noJump, 0.1f);
        CHECK(!character.state().grounded);
    }

    // 4. Walk-off-ledge equivalent: after enough ticks of gravity,
    //    the character lands back on the floor. The descent CCD
    //    handles the high-velocity landing tick (without it the
    //    capsule would tunnel through the floor at dt=0.1).
    bool landed = false;
    for (int i = 0; i < 40; ++i) {
        character.move(noJump, 0.1f);
        if (character.state().grounded) {
            landed = true;
            break;
        }
    }
    CHECK(landed);
    // Landed snapped to resting y = 0.9 (capsule center over floor top).
    CHECK(std::fabs(character.state().position.y - 0.9f) < 1.0e-3f);
    // Vertical velocity zeroed on landing.
    CHECK(std::fabs(character.state().velocity.y) < 1.0e-4f);

    // 5. Standing still on the floor after landing keeps grounded
    //    true tick after tick.
    for (int i = 0; i < 5; ++i) {
        character.move(noJump, 0.1f);
        CHECK(character.state().grounded);
        CHECK(std::fabs(character.state().position.y - 0.9f) < 1.0e-4f);
    }

    // 6. Jump without grounded: input.jump on an already-airborne
    //    character has no effect (the controller only honors jump
    //    when grounded).
    character.move(jumpInput, 0.1f);  // grounded jump
    CHECK(!character.state().grounded);
    const float midairY = character.state().position.y;
    CharacterInput midairJump;
    midairJump.jump = true;
    midairJump.jumpSpeed = 100.0f;  // huge but should be ignored
    character.move(midairJump, 0.1f);
    // Not catapulted skyward — velocity.y is the gravity-decremented
    // jump pulse, NOT 100.
    CHECK(character.state().velocity.y < 6.0f);
    // Sanity: position moved upward modestly, not by ~10m.
    CHECK(character.state().position.y < midairY + 1.5f);

    backend->destroyShape(floorShapeId);
    backend->destroyWorld(world);
    EXIT_WITH_RESULT();
}
