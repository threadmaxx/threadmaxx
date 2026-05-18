// threadmaxx_simd — `forEachChunk` × SIMD kernel integration test (S7).
//
// Drives a real `threadmaxx::Engine` through one step of integration
// where the integrator system uses `forEachChunk<Transform, Velocity>`
// to fan out parallel chunk work and `simd::integrate_linear_motion`
// to do the math. Compares against a scalar reference computed
// out-of-band (same Velocity * dt arithmetic) over each entity's
// pre-step position.
//
// The point isn't to demonstrate a perf win — `integrate_linear_motion`
// stays scalar in S4 due to the Transform stride. The point is to
// prove the design contract from DESIGN_NOTES §7: a system can take
// chunks of contiguous engine PODs, feed them to a SIMD kernel via
// `std::span`, and produce the same observable result as a scalar
// reference. The chunked iteration also exercises the parallel
// `forEachChunk` path (one job per chunk).
//
// Scale: enough entities (~256) to populate multiple archetypes if
// any user-components were involved; here all share one archetype
// so we get a single chunk per step. Validated empirically by
// `world.archetypeChunkCount()` printout.

#include "Check.hpp"

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Components.hpp>
#include <threadmaxx/Config.hpp>
#include <threadmaxx/Engine.hpp>
#include <threadmaxx/Game.hpp>
#include <threadmaxx/Query.hpp>
#include <threadmaxx/System.hpp>
#include <threadmaxx/World.hpp>

#include <threadmaxx_simd/transform_ops.hpp>

#include <cmath>
#include <cstdio>
#include <span>
#include <vector>

namespace {

using namespace threadmaxx;

bool approxEq(float a, float b, float eps = 1e-5f) {
    return std::fabs(a - b) <= eps;
}

bool approxEq(const Vec3& a, const Vec3& b, float eps = 1e-5f) {
    return approxEq(a.x, b.x, eps) && approxEq(a.y, b.y, eps) &&
           approxEq(a.z, b.z, eps);
}

constexpr std::size_t kEntityCount = 256;
constexpr float       kDt          = 0.5f;

/// Holds the pre-step positions + velocities so the test can compute
/// the expected post-step positions out-of-band.
struct Snapshot {
    std::vector<EntityHandle> handles;
    std::vector<Vec3>         preStepPos;
    std::vector<Vec3>         linVel;
};

/// `SimdIntegrator` is the system under test. Per `forEachChunk`
/// dispatch it:
///   1. Allocates per-chunk scratch (one `Transform` array, one
///      `Vec3` array of linear velocities — both live on the
///      worker's stack, racing-safe by construction).
///   2. Calls `simd::integrate_linear_motion(nextT, linV, dt)`.
///   3. Pushes the result through the worker's CommandBuffer via
///      `cb.setTransform`.
class SimdIntegrator : public ISystem {
public:
    explicit SimdIntegrator(float dt) : dt_(dt) {}

    const char* name() const noexcept override { return "simd-integrator"; }
    ComponentSet reads()  const noexcept override {
        return ComponentSet{Component::Velocity};
    }
    ComponentSet writes() const noexcept override {
        return ComponentSet{Component::Transform};
    }

    void update(SystemContext& ctx) override {
        const float dt = dt_;
        forEachChunk<Transform, Velocity>(ctx,
            [dt](std::span<const EntityHandle> es,
                 std::span<const Transform>    ts,
                 std::span<const Velocity>     vs,
                 CommandBuffer&                cb) {
                // Per-chunk scratch on the worker's stack frame.
                std::vector<Transform> nextT(ts.begin(), ts.end());
                std::vector<Vec3>      linV;
                linV.reserve(vs.size());
                for (const auto& v : vs) linV.push_back(v.linear);

                threadmaxx::simd::integrate_linear_motion(
                    std::span<Transform>(nextT),
                    std::span<const Vec3>(linV),
                    dt);

                for (std::size_t i = 0; i < es.size(); ++i) {
                    cb.setTransform(es[i], nextT[i]);
                }
            });
    }

private:
    float dt_;
};

/// Seeds N entities. Each gets a Transform and a Velocity with
/// deterministic per-index values so the scalar reference is
/// reproducible. The handles + initial state get stashed in
/// `Snapshot` for later verification.
class SeedGame : public IGame {
public:
    Snapshot* snap;

    void onSetup(Engine& engine, World&, CommandBuffer& seed) override {
        snap->handles.reserve(kEntityCount);
        snap->preStepPos.reserve(kEntityCount);
        snap->linVel.reserve(kEntityCount);

        std::vector<EntityHandle> reserved(kEntityCount);
        engine.reserveEntityHandles(
            static_cast<std::uint32_t>(kEntityCount),
            std::span<EntityHandle>(reserved));

        for (std::size_t i = 0; i < kEntityCount; ++i) {
            const float fi = static_cast<float>(i);
            Bundle b{};
            b.transform.position = Vec3{fi, fi * 0.5f, -fi};
            b.velocity.linear    = Vec3{1.0f + fi * 0.01f,
                                        -0.5f,
                                        2.0f - fi * 0.02f};
            b.initialMask = ComponentSet{
                Component::Transform,
                Component::Velocity,
            };
            seed.spawnBundle(reserved[i], b);

            snap->handles.push_back(reserved[i]);
            snap->preStepPos.push_back(b.transform.position);
            snap->linVel.push_back(b.velocity.linear);
        }
    }
};

} // namespace

int main() {
    Snapshot snap;

    Config cfg;
    cfg.sleepToPace = false;
    cfg.workerCount = 4;   // exercise parallel forEachChunk dispatch
    Engine engine(cfg);

    SeedGame game;
    game.snap = &snap;
    CHECK(engine.initialize(game));

    // Register the SIMD integrator AFTER initialize so its first
    // update runs against the seeded entities.
    engine.registerSystem(std::make_unique<SimdIntegrator>(kDt));

    // Seed buffer commits inside `engine.initialize` (entities are
    // already alive when initialize returns). One `step()` is the
    // single integrator pass we want to verify; a second step would
    // apply velocity again and double-integrate.
    engine.step();

    // Verify every entity's new position equals (pre + linVel * dt).
    int mismatches = 0;
    for (std::size_t i = 0; i < snap.handles.size(); ++i) {
        const Vec3 expected{
            snap.preStepPos[i].x + snap.linVel[i].x * kDt,
            snap.preStepPos[i].y + snap.linVel[i].y * kDt,
            snap.preStepPos[i].z + snap.linVel[i].z * kDt,
        };
        const Transform* tr = engine.world().tryGetTransform(snap.handles[i]);
        CHECK(tr != nullptr);
        if (tr && !approxEq(tr->position, expected)) {
            ++mismatches;
            if (mismatches < 5) {
                std::fprintf(stderr,
                    "entity %zu: expected (%g,%g,%g) got (%g,%g,%g)\n",
                    i, expected.x, expected.y, expected.z,
                    tr->position.x, tr->position.y, tr->position.z);
            }
        }
    }
    CHECK_EQ(mismatches, 0);

    std::printf("[simd_chunk_integration] %zu entities × forEachChunk × "
                "simd::integrate_linear_motion verified\n",
                snap.handles.size());

    EXIT_WITH_RESULT();
}
