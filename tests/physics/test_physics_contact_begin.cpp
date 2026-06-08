#include "Check.hpp"

#include "threadmaxx_physics/contact.hpp"
#include "threadmaxx_physics/stub_backend.hpp"

#include <vector>

// P8 — `ContactPhase::Begin` fires exactly once when two bodies'
// world-space AABBs transition from disjoint to overlapping. The
// callback receives a canonicalized pair (`bodyA.value < bodyB.value`).
//
// Setup: body A static at (0,0,0); body B kinematic at (3,0,0) with
// linearVelocity (-1, 0, 0). Both are unit boxes (half-extents 0.5).
//
// Step trace at dt=1.0s:
//   tick 1: B at x=2  → AABB [1.5, 2.5] vs A [-0.5, 0.5] → DISJOINT
//   tick 2: B at x=1  → AABB [0.5, 1.5] vs A [-0.5, 0.5] → TOUCH (Begin)
//   tick 3: B at x=0  → AABB [-0.5, 0.5] vs A [-0.5, 0.5] → OVERLAP (no re-Begin)
//   tick 4: B at x=-1 → AABB [-1.5, -0.5] vs A [-0.5, 0.5] → TOUCH (no re-Begin)

using namespace threadmaxx::physics;

int main() {
    auto backend = makeStubBackend();
    PhysicsConfig cfg;
    PhysicsWorldId world = backend->createWorld(cfg);

    ShapeDesc box;
    box.type = ShapeType::Box;
    box.halfExtents = Vec3{0.5f, 0.5f, 0.5f};
    ShapeId shape = backend->createShape(box);
    ShapeId shapes[1] = {shape};

    BodyDesc a;
    a.type = BodyType::Static;
    a.position = Vec3{0.0f, 0.0f, 0.0f};
    BodyId bodyA = backend->createBody(world, a,
                                       std::span<const ShapeId>(shapes, 1));

    BodyDesc b;
    b.type = BodyType::Kinematic;
    b.position = Vec3{3.0f, 0.0f, 0.0f};
    b.linearVelocity = Vec3{-1.0f, 0.0f, 0.0f};
    BodyId bodyB = backend->createBody(world, b,
                                       std::span<const ShapeId>(shapes, 1));

    std::vector<ContactEvent> events;
    setContactCallback(*backend, world,
                       [&events](const ContactEvent& ev) {
                           events.push_back(ev);
                       });

    // tick 1: separation. No contact yet.
    backend->stepWorld(world, 1.0f);
    CHECK(events.empty());

    // tick 2: AABBs touch at x=0.5. Begin fires.
    backend->stepWorld(world, 1.0f);
    CHECK(events.size() == 1);
    CHECK(events[0].phase == ContactPhase::Begin);
    // Canonical: lo.value < hi.value.
    CHECK(events[0].bodyA.value < events[0].bodyB.value);
    // The pair is (A, B) regardless of which got the canonical lo slot.
    const bool aIsLo = events[0].bodyA == bodyA;
    const bool bIsLo = events[0].bodyA == bodyB;
    CHECK(aIsLo || bIsLo);
    if (aIsLo) {
        CHECK(events[0].bodyB == bodyB);
    } else {
        CHECK(events[0].bodyB == bodyA);
    }

    // tick 3: continuing overlap. NO re-Begin.
    backend->stepWorld(world, 1.0f);
    CHECK(events.size() == 1);

    // tick 4: still touching on the other side. Still no re-Begin.
    backend->stepWorld(world, 1.0f);
    CHECK(events.size() == 1);

    backend->destroyBody(world, bodyA);
    backend->destroyBody(world, bodyB);
    backend->destroyShape(shape);
    backend->destroyWorld(world);
    EXIT_WITH_RESULT();
}
