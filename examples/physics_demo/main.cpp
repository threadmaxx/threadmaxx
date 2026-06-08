// physics_demo — end-to-end scene exercising the threadmaxx_physics
// library: world + ground plate + obstacle wall + dynamic boxes +
// character controller walking toward the wall, stepping up onto the
// floor behind it, and falling back down once the controller crosses
// the edge. Contact events are printed as they fire.
//
// Prefers JoltBackend when the library was built with Jolt; falls back
// to StubBackend (kinematic-only integration; the dynamic boxes don't
// actually fall, but the character controller still walks). Either way
// the demo prints periodic state lines plus the contact event stream,
// then exits cleanly after a bounded number of ticks so the binary is
// CI-friendly.
//
// Usage:
//   threadmaxx_physics_demo                # 180 ticks (~3 sec sim time)
//   threadmaxx_physics_demo <tickCount>    # custom tick budget

#include "threadmaxx_physics/threadmaxx_physics.hpp"

#include <cstdio>
#include <cstdlib>
#include <span>
#include <vector>

using namespace threadmaxx::physics;

namespace {

// Half-extents for the various box-shaped pieces of the scene.
constexpr Vec3 kGroundHalf{20.0f, 0.5f, 20.0f};   // 40 x 1 x 40 ground plate
constexpr Vec3 kWallHalf  { 0.5f, 0.5f,  2.0f};   // low wall the controller steps onto
constexpr Vec3 kBoxHalf   { 0.5f, 0.5f,  0.5f};   // 1 m unit boxes (dynamic + obstacles)

// Capsule character lives at (X, 0.9, 0) with height 1.8 (bottom of
// capsule flush with the y=0 ground top).
constexpr float kCharStartX = -8.0f;
constexpr float kCharY      =  0.9f;

// Number of dynamic boxes raining down on the scene to give the
// contact-event callback something to do.
constexpr int kDynamicBoxCount = 6;

void printState(int tick, const CharacterState& s) {
    std::printf("[t=%4d] char pos=(%.2f, %.2f, %.2f) vel=(%.2f, %.2f, %.2f) grounded=%d\n",
                tick,
                static_cast<double>(s.position.x), static_cast<double>(s.position.y),
                static_cast<double>(s.position.z),
                static_cast<double>(s.velocity.x), static_cast<double>(s.velocity.y),
                static_cast<double>(s.velocity.z),
                s.grounded ? 1 : 0);
}

} // namespace

int main(int argc, char** argv) {
    int tickBudget = 180;
    if (argc > 1) {
        tickBudget = std::atoi(argv[1]);
        if (tickBudget <= 0) tickBudget = 180;
    }

    std::printf("physics_demo v%s — running %d ticks\n",
                version_string(), tickBudget);

    // 1. Pick a backend. Jolt is preferred so the dynamic boxes actually
    //    fall; Stub keeps the rest of the scene meaningful.
    std::unique_ptr<IPhysicsBackend> backend = makeJoltBackend();
    if (backend) {
        std::printf("[init] using JoltBackend (real physics)\n");
    } else {
        backend = makeStubBackend();
        std::printf("[init] using StubBackend (kinematic-only; rebuild with "
                    "-DTHREADMAXX_PHYSICS_FETCH_JOLT=ON for full physics)\n");
    }

    // 2. World with deterministic profile.
    PhysicsConfig cfg;
    cfg.fixedTimestep        = 1.0f / 60.0f;
    cfg.maxSubSteps          = 1;
    cfg.allowSolverThreading = false;
    PhysicsWorldId world = backend->createWorld(cfg);

    // 3. Static geometry: ground plate + low wall.
    ShapeDesc groundDesc;
    groundDesc.type = ShapeType::Box;
    groundDesc.halfExtents = kGroundHalf;
    ShapeId groundShape = backend->createShape(groundDesc);

    ShapeDesc wallDesc;
    wallDesc.type = ShapeType::Box;
    wallDesc.halfExtents = kWallHalf;
    ShapeId wallShape = backend->createShape(wallDesc);

    ShapeDesc boxDesc;
    boxDesc.type = ShapeType::Box;
    boxDesc.halfExtents = kBoxHalf;
    ShapeId boxShape = backend->createShape(boxDesc);

    BodyDesc groundBody;
    groundBody.type = BodyType::Static;
    groundBody.position = Vec3{0.0f, -0.5f, 0.0f};   // top face at y=0
    ShapeId groundShapes[1] = {groundShape};
    BodyId ground = backend->createBody(world, groundBody,
                                       std::span<const ShapeId>(groundShapes, 1));

    BodyDesc wallBody;
    wallBody.type = BodyType::Static;
    wallBody.position = Vec3{0.0f, 0.5f, 0.0f};      // top at y=1.0 — within stepHeight
    ShapeId wallShapes[1] = {wallShape};
    BodyId wall = backend->createBody(world, wallBody,
                                     std::span<const ShapeId>(wallShapes, 1));

    // 4. Dynamic boxes scattered above the ground plate.
    ShapeId dynShapes[1] = {boxShape};
    std::vector<BodyId> dynamicBoxes;
    dynamicBoxes.reserve(static_cast<std::size_t>(kDynamicBoxCount));
    for (int i = 0; i < kDynamicBoxCount; ++i) {
        BodyDesc d;
        d.type = BodyType::Dynamic;
        const float lane = (static_cast<float>(i) - static_cast<float>(kDynamicBoxCount) * 0.5f) * 1.5f;
        d.position = Vec3{lane, 6.0f + static_cast<float>(i) * 0.4f, 5.0f};
        d.mass = 1.0f;
        dynamicBoxes.push_back(backend->createBody(world, d,
                                                   std::span<const ShapeId>(dynShapes, 1)));
    }
    std::printf("[init] static: ground=%llu wall=%llu  dynamic boxes: %d\n",
                static_cast<unsigned long long>(ground.value),
                static_cast<unsigned long long>(wall.value),
                kDynamicBoxCount);

    // 5. Character controller starting 8 m to the wall's left.
    CharacterControllerDesc desc;
    desc.startPosition = Vec3{kCharStartX, kCharY, 0.0f};
    desc.radius        = 0.5f;
    desc.height        = 1.8f;
    desc.stepHeight    = 0.4f;     // tall enough to climb the 1.0 m wall ledge in step-up form
    desc.maxMoveSpeed  = 4.0f;
    desc.gravity       = -9.81f;
    desc.layerMask     = 0xFFFFFFFFu;

    CharacterController character(*backend, world, desc);

    // 6. Contact callback — print every begin / end. We count rather
    //    than spamming so the demo's output stays bounded even when
    //    real Jolt physics dumps hundreds of pairs.
    int contactBegins = 0;
    int contactEnds   = 0;
    backend->setContactCallback(world, [&](const ContactEvent& ev) {
        if (ev.phase == ContactPhase::Begin) {
            ++contactBegins;
            if (contactBegins <= 8) {
                std::printf("[contact] Begin  a=%llu b=%llu\n",
                            static_cast<unsigned long long>(ev.bodyA.value),
                            static_cast<unsigned long long>(ev.bodyB.value));
            } else if (contactBegins == 9) {
                std::printf("[contact] ... (further Begin events suppressed)\n");
            }
        } else {
            ++contactEnds;
            if (contactEnds <= 4) {
                std::printf("[contact] End    a=%llu b=%llu\n",
                            static_cast<unsigned long long>(ev.bodyA.value),
                            static_cast<unsigned long long>(ev.bodyB.value));
            } else if (contactEnds == 5) {
                std::printf("[contact] ... (further End events suppressed)\n");
            }
        }
    });

    // 7. Run the simulation loop. Character walks forward (+X), jumps
    //    once at t=120 to clear the back side of the wall, and the
    //    backend integrates everything else.
    CharacterInput input;
    input.moveIntent = Vec3{1.0f, 0.0f, 0.0f};   // forward at full speed
    input.jumpSpeed  = 5.0f;

    for (int t = 0; t < tickBudget; ++t) {
        input.jump = (t == 120);
        character.move(input, cfg.fixedTimestep);
        backend->stepWorld(world, cfg.fixedTimestep);
        if (t % 30 == 0 || t == tickBudget - 1) {
            printState(t, character.state());
        }
    }

    // 8. Final snapshot — how many dynamic boxes came to rest, what the
    //    character's final pose is, and the contact totals.
    std::printf("\n[summary]\n");
    std::printf("  character final pos: (%.2f, %.2f, %.2f) grounded=%d\n",
                static_cast<double>(character.state().position.x),
                static_cast<double>(character.state().position.y),
                static_cast<double>(character.state().position.z),
                character.state().grounded ? 1 : 0);

    int restingBoxes = 0;
    for (BodyId b : dynamicBoxes) {
        auto st = backend->getBodyState(world, b);
        if (st && st->position.y < 2.0f) ++restingBoxes;
    }
    std::printf("  dynamic boxes settled (y < 2.0): %d / %d\n",
                restingBoxes, kDynamicBoxCount);
    std::printf("  contact events: %d Begin, %d End\n",
                contactBegins, contactEnds);

    // 9. Teardown.
    backend->setContactCallback(world, ContactCallback{});
    for (BodyId b : dynamicBoxes) backend->destroyBody(world, b);
    backend->destroyBody(world, wall);
    backend->destroyBody(world, ground);
    backend->destroyShape(boxShape);
    backend->destroyShape(wallShape);
    backend->destroyShape(groundShape);
    backend->destroyWorld(world);

    std::printf("[done] demo OK\n");
    return EXIT_SUCCESS;
}
