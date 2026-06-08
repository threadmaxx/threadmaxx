#include "Check.hpp"

#include "threadmaxx_physics/character.hpp"
#include "threadmaxx_physics/stub_backend.hpp"

#include <cmath>

// P7 — character hitting an obstacle of height ≤ `stepHeight` climbs
// it; an obstacle taller than `stepHeight` blocks. Two sub-cases share
// the floor + character setup; the obstacle is swapped between worlds.
//
// Geometry:
//   - Floor at y=-0.5 with half-extents (50, 0.5, 50), top face at y=0.
//   - Low step: half-extents (24, 0.15, 50), centered at (26, 0.15, 0).
//     Top at y=0.3 = stepHeight. Spans x∈[2, 50].
//   - High step: same X / Z footprint, half-extents (24, 0.25, 50),
//     centered at (26, 0.25, 0). Top at y=0.5 > stepHeight. Spans
//     x∈[2, 50].
//
// Character: radius=0.5, height=1.8, stepHeight=0.3, maxMoveSpeed=5.
// Capsule center starts at (0, 0.9, 0) — feet resting on floor.
//
// Drive 10 ticks of dt=0.1s with forward-X intent. Total horizontal
// travel target: 5m.
//
// **Low step**: at some tick the character meets the step's front
// edge (x≈2). The horizontal sweep at bottom-hemisphere center y=0.5
// hits; the elevated retry at y=0.801 misses (the step's inflated
// upper edge sits at y=0.8 < 0.801). The character is raised by
// stepHeight=0.3 — final capsule center y=1.2. Total +X travel is
// still 5m (the step-up tick advances by the same horizDelta as a
// flat tick).
//
// **High step**: the elevated sweep at y=0.801 also hits (the step's
// inflated upper edge is at y=1.0 > 0.801). Step-up is rejected; the
// character slides to a stop at the step's front edge (x≈2). Capsule
// center y stays at 0.9 (still on lower floor).

using namespace threadmaxx::physics;

namespace {

struct StepScene {
    std::unique_ptr<IPhysicsBackend> backend;
    PhysicsWorldId world{};
    ShapeId floorShape{};
    ShapeId stepShape{};
    BodyId floor{};
    BodyId step{};
};

StepScene buildScene(float stepHalfHeightY) {
    StepScene scene;
    scene.backend = makeStubBackend();
    PhysicsConfig cfg;
    scene.world = scene.backend->createWorld(cfg);

    ShapeDesc floorDesc;
    floorDesc.type = ShapeType::Box;
    floorDesc.halfExtents = Vec3{50.0f, 0.5f, 50.0f};
    scene.floorShape = scene.backend->createShape(floorDesc);
    ShapeId floorShapes[1] = {scene.floorShape};

    BodyDesc floorBody;
    floorBody.type = BodyType::Static;
    floorBody.position = Vec3{0.0f, -0.5f, 0.0f};
    scene.floor = scene.backend->createBody(
        scene.world, floorBody, std::span<const ShapeId>(floorShapes, 1));

    ShapeDesc stepDesc;
    stepDesc.type = ShapeType::Box;
    stepDesc.halfExtents = Vec3{24.0f, stepHalfHeightY, 50.0f};
    scene.stepShape = scene.backend->createShape(stepDesc);
    ShapeId stepShapes[1] = {scene.stepShape};

    BodyDesc stepBody;
    stepBody.type = BodyType::Static;
    stepBody.position = Vec3{26.0f, stepHalfHeightY, 0.0f};
    scene.step = scene.backend->createBody(
        scene.world, stepBody, std::span<const ShapeId>(stepShapes, 1));

    return scene;
}

void tearDown(StepScene& scene) {
    scene.backend->destroyShape(scene.floorShape);
    scene.backend->destroyShape(scene.stepShape);
    scene.backend->destroyWorld(scene.world);
}

CharacterControllerDesc makeCharacterDesc() {
    CharacterControllerDesc desc;
    desc.startPosition = Vec3{0.0f, 0.9f, 0.0f};
    desc.radius = 0.5f;
    desc.height = 1.8f;
    desc.stepHeight = 0.3f;
    desc.maxMoveSpeed = 5.0f;
    desc.gravity = -9.81f;
    return desc;
}

} // namespace

int main() {
    // ===== Low step (top y=0.3 = stepHeight): character climbs =====
    {
        StepScene scene = buildScene(/*stepHalfHeightY=*/0.15f);
        CharacterController character(*scene.backend, scene.world,
                                      makeCharacterDesc());
        CHECK(character.state().grounded);

        CharacterInput input;
        input.moveIntent = Vec3{1.0f, 0.0f, 0.0f};

        constexpr int kTicks = 10;
        constexpr float kDt = 0.1f;
        for (int i = 0; i < kTicks; ++i) {
            character.move(input, kDt);
        }

        // Total horizontal travel = 5m even with the step-up midway.
        CHECK(std::fabs(character.state().position.x - 5.0f) < 1.0e-3f);
        // Capsule center should be raised to stepTop + height/2 = 0.3 + 0.9 = 1.2.
        CHECK(std::fabs(character.state().position.y - 1.2f) < 1.0e-3f);
        CHECK(character.state().grounded);

        tearDown(scene);
    }

    // ===== High step (top y=0.5 > stepHeight): character blocks =====
    {
        StepScene scene = buildScene(/*stepHalfHeightY=*/0.25f);
        CharacterController character(*scene.backend, scene.world,
                                      makeCharacterDesc());
        CHECK(character.state().grounded);

        CharacterInput input;
        input.moveIntent = Vec3{1.0f, 0.0f, 0.0f};

        constexpr int kTicks = 10;
        constexpr float kDt = 0.1f;
        for (int i = 0; i < kTicks; ++i) {
            character.move(input, kDt);
        }

        // Step front edge at x=2. Bottom-hem sphere of radius 0.5
        // touches step when capsule center x = 1.5; the slide-stop
        // back-off leaves it parked just shy of x=1.5. We accept any
        // x in [1.4, 2.1] — well short of 5m.
        const float endX = character.state().position.x;
        CHECK(endX > 1.4f);
        CHECK(endX < 2.1f);
        // Still on the lower floor.
        CHECK(std::fabs(character.state().position.y - 0.9f) < 1.0e-3f);
        CHECK(character.state().grounded);

        tearDown(scene);
    }

    EXIT_WITH_RESULT();
}
