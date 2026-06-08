// physics_jolt_bench — 1024 dynamic boxes falling onto a static ground
// plane under Jolt's solver. Reports ms / tick at 60 Hz over 60 ticks.
//
// Build: configure with `-DTHREADMAXX_BUILD_BENCHMARKS=ON
// -DTHREADMAXX_PHYSICS_FETCH_JOLT=ON` (or with a system Jolt install),
// build the `physics_jolt_bench` target, run the binary. The bench is
// silently absent when Jolt was not located at configure time.
//
// Workload: a 32 x 32 grid of 1 m unit boxes spaced 2 m apart and
// stacked 10 m above the ground, plus a 100 x 100 x 1 static ground
// plate at y=0. Solver runs single-threaded (deterministic profile)
// with `maxSubSteps = 1`.

#include "threadmaxx_physics/jolt_backend.hpp"
#include "threadmaxx_physics/threadmaxx_physics.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <vector>

using namespace threadmaxx::physics;

int main() {
    auto backend = makeJoltBackend();
    if (!backend) {
        std::fprintf(stderr,
                     "physics_jolt_bench: makeJoltBackend() returned nullptr — "
                     "library was built without Jolt.\n");
        return EXIT_FAILURE;
    }

    PhysicsConfig cfg;
    cfg.fixedTimestep = 1.0f / 60.0f;
    cfg.maxSubSteps = 1;
    cfg.allowSolverThreading = false;
    PhysicsWorldId world = backend->createWorld(cfg);

    // Ground plate (static).
    ShapeDesc groundDesc;
    groundDesc.type = ShapeType::Box;
    groundDesc.halfExtents = Vec3{50.0f, 0.5f, 50.0f};
    ShapeId groundShape = backend->createShape(groundDesc);

    BodyDesc groundBody;
    groundBody.type = BodyType::Static;
    groundBody.position = Vec3{0.0f, -0.5f, 0.0f};
    ShapeId groundShapes[1] = {groundShape};
    BodyId ground = backend->createBody(world, groundBody,
                                        std::span<const ShapeId>(groundShapes, 1));

    // 1024 unit boxes on a 32x32 grid.
    ShapeDesc boxDesc;
    boxDesc.type = ShapeType::Box;
    boxDesc.halfExtents = Vec3{0.5f, 0.5f, 0.5f};
    ShapeId boxShape = backend->createShape(boxDesc);
    ShapeId boxShapes[1] = {boxShape};

    std::vector<BodyId> boxes;
    boxes.reserve(1024);
    constexpr int kGrid = 32;
    constexpr float kSpacing = 2.0f;
    constexpr float kStartY = 10.0f;
    for (int z = 0; z < kGrid; ++z) {
        for (int x = 0; x < kGrid; ++x) {
            BodyDesc d;
            d.type = BodyType::Dynamic;
            d.position = Vec3{(static_cast<float>(x) - static_cast<float>(kGrid) * 0.5f) * kSpacing,
                              kStartY,
                              (static_cast<float>(z) - static_cast<float>(kGrid) * 0.5f) * kSpacing};
            d.mass = 1.0f;
            boxes.push_back(backend->createBody(world, d,
                                                std::span<const ShapeId>(boxShapes, 1)));
        }
    }
    std::printf("physics_jolt_bench: spawned %zu dynamic boxes\n", boxes.size());

    // Run 60 ticks and report per-tick wall-clock.
    constexpr int kTicks = 60;
    double totalMs = 0.0;
    double maxMs = 0.0;
    for (int i = 0; i < kTicks; ++i) {
        auto t0 = std::chrono::steady_clock::now();
        backend->stepWorld(world, cfg.fixedTimestep);
        auto t1 = std::chrono::steady_clock::now();
        const double ms =
            std::chrono::duration<double, std::milli>(t1 - t0).count();
        totalMs += ms;
        if (ms > maxMs) maxMs = ms;
    }

    std::printf("physics_jolt_bench: %d ticks, avg %.3f ms/tick, max %.3f ms/tick, total %.3f ms\n",
                kTicks, totalMs / kTicks, maxMs, totalMs);

    for (BodyId b : boxes) backend->destroyBody(world, b);
    backend->destroyBody(world, ground);
    backend->destroyShape(boxShape);
    backend->destroyShape(groundShape);
    backend->destroyWorld(world);
    return EXIT_SUCCESS;
}
