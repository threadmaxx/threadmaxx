#include "Check.hpp"

#include "threadmaxx_physics/backend.hpp"
#include "threadmaxx_physics/stub_backend.hpp"

// P2 — `createShape` round-trips its descriptor through `getShapeDesc`
// and answers a sensible local-space AABB for each primitive type.

using namespace threadmaxx::physics;

int main() {
    auto backend = makeStubBackend();
    CHECK(backend != nullptr);

    // Box: 1m³ at the origin → halfExtents 0.5 on every axis.
    ShapeDesc boxDesc;
    boxDesc.type = ShapeType::Box;
    boxDesc.halfExtents = Vec3{0.5f, 0.5f, 0.5f};
    ShapeId box = backend->createShape(boxDesc);
    CHECK(static_cast<bool>(box));

    const ShapeDesc* boxBack = backend->getShapeDesc(box);
    CHECK(boxBack != nullptr);
    CHECK(boxBack->type == ShapeType::Box);
    CHECK(boxBack->halfExtents.x == 0.5f);
    CHECK(boxBack->halfExtents.y == 0.5f);
    CHECK(boxBack->halfExtents.z == 0.5f);

    auto boxAabb = backend->getShapeAabb(box);
    CHECK(boxAabb.has_value());
    CHECK(boxAabb->min.x == -0.5f);
    CHECK(boxAabb->min.y == -0.5f);
    CHECK(boxAabb->min.z == -0.5f);
    CHECK(boxAabb->max.x == 0.5f);
    CHECK(boxAabb->max.y == 0.5f);
    CHECK(boxAabb->max.z == 0.5f);

    // Sphere.
    ShapeDesc sphereDesc;
    sphereDesc.type = ShapeType::Sphere;
    sphereDesc.radius = 0.75f;
    ShapeId sphere = backend->createShape(sphereDesc);
    CHECK(static_cast<bool>(sphere));

    const ShapeDesc* sphereBack = backend->getShapeDesc(sphere);
    CHECK(sphereBack != nullptr);
    CHECK(sphereBack->type == ShapeType::Sphere);
    CHECK(sphereBack->radius == 0.75f);

    auto sphereAabb = backend->getShapeAabb(sphere);
    CHECK(sphereAabb.has_value());
    CHECK(sphereAabb->min.x == -0.75f);
    CHECK(sphereAabb->max.x == 0.75f);

    // Capsule: height=1, radius=0.4 → AABB Y extent = 1/2 + 0.4 = 0.9.
    ShapeDesc capsuleDesc;
    capsuleDesc.type = ShapeType::Capsule;
    capsuleDesc.radius = 0.4f;
    capsuleDesc.height = 1.0f;
    ShapeId capsule = backend->createShape(capsuleDesc);
    CHECK(static_cast<bool>(capsule));

    auto capsuleAabb = backend->getShapeAabb(capsule);
    CHECK(capsuleAabb.has_value());
    CHECK(capsuleAabb->min.x == -0.4f);
    CHECK(capsuleAabb->max.x == 0.4f);
    CHECK(capsuleAabb->min.y == -0.9f);
    CHECK(capsuleAabb->max.y == 0.9f);

    // Mesh / ConvexHull: AABB from vertex point cloud.
    ShapeDesc meshDesc;
    meshDesc.type = ShapeType::Mesh;
    meshDesc.vertices = {
        Vec3{-1.0f, -2.0f, 0.0f},
        Vec3{ 3.0f,  0.5f, -1.5f},
        Vec3{ 0.0f,  4.0f, 2.0f},
    };
    ShapeId mesh = backend->createShape(meshDesc);
    CHECK(static_cast<bool>(mesh));

    auto meshAabb = backend->getShapeAabb(mesh);
    CHECK(meshAabb.has_value());
    CHECK(meshAabb->min.x == -1.0f);
    CHECK(meshAabb->min.y == -2.0f);
    CHECK(meshAabb->min.z == -1.5f);
    CHECK(meshAabb->max.x == 3.0f);
    CHECK(meshAabb->max.y == 4.0f);
    CHECK(meshAabb->max.z == 2.0f);

    // Invalid / default-id paths.
    CHECK(backend->getShapeDesc(ShapeId{}) == nullptr);
    CHECK(!backend->getShapeAabb(ShapeId{}).has_value());

    // After destroy, lookups return null (no body holds these refs).
    backend->destroyShape(box);
    CHECK(backend->getShapeDesc(box) == nullptr);
    CHECK(!backend->getShapeAabb(box).has_value());

    backend->destroyShape(sphere);
    backend->destroyShape(capsule);
    backend->destroyShape(mesh);

    EXIT_WITH_RESULT();
}
