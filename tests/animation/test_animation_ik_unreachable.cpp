#include "Check.hpp"
#include "threadmaxx_animation/ik.hpp"

#include <cmath>
#include <vector>

using namespace threadmaxx::animation;

namespace {

constexpr bool nearly(float a, float b, float eps = 1e-4f) noexcept {
    const float d = a - b;
    return (d < eps) && (d > -eps);
}

constexpr bool nearly(const Vec3& a, const Vec3& b, float eps = 1e-4f) noexcept {
    return nearly(a.x, b.x, eps) && nearly(a.y, b.y, eps) && nearly(a.z, b.z, eps);
}

bool isFiniteVec(const Vec3& v) noexcept {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

}  // namespace

int main() {
    // 5-joint chain along x-axis (total reach = 4). Target = (10, 0, 0):
    // distance from root = 10, beyond the chain's reach. Solver must
    // stretch the chain toward the target without NaN or infinite loop.
    std::vector<Vec3> chain = {
        Vec3{0.0f, 0.0f, 0.0f},
        Vec3{1.0f, 0.0f, 0.0f},
        Vec3{2.0f, 0.0f, 0.0f},
        Vec3{3.0f, 0.0f, 0.0f},
        Vec3{4.0f, 0.0f, 0.0f},
    };

    IKTarget t;
    t.position = Vec3{10.0f, 0.0f, 0.0f};
    t.maxIterations = 16;
    t.tolerance = 1e-3f;

    const IKSolveResult r = solveIK(std::span<Vec3>(chain), t);

    CHECK(!r.converged);
    // Stretched-toward solution: chain laid out along (10,0,0) direction
    // at original bone lengths.
    CHECK(nearly(chain[0], Vec3{0.0f, 0.0f, 0.0f}));
    CHECK(nearly(chain[1], Vec3{1.0f, 0.0f, 0.0f}));
    CHECK(nearly(chain[2], Vec3{2.0f, 0.0f, 0.0f}));
    CHECK(nearly(chain[3], Vec3{3.0f, 0.0f, 0.0f}));
    CHECK(nearly(chain[4], Vec3{4.0f, 0.0f, 0.0f}));
    // Leftover distance = 10 - 4 = 6.
    CHECK(nearly(r.finalDistance, 6.0f, 1e-3f));

    // No NaN anywhere.
    for (const Vec3& p : chain) {
        CHECK(isFiniteVec(p));
    }

    // Off-axis target also stretches without NaN. Target at (0, 100, 0)
    // — distance 100 from root, way beyond reach.
    std::vector<Vec3> chain2 = {
        Vec3{0.0f, 0.0f, 0.0f},
        Vec3{1.0f, 0.0f, 0.0f},
        Vec3{2.0f, 0.0f, 0.0f},
    };
    IKTarget t2;
    t2.position = Vec3{0.0f, 100.0f, 0.0f};
    const IKSolveResult r2 = solveIK(std::span<Vec3>(chain2), t2);
    CHECK(!r2.converged);
    // Chain lays along +y at bone lengths 1, 1.
    CHECK(nearly(chain2[0], Vec3{0.0f, 0.0f, 0.0f}));
    CHECK(nearly(chain2[1], Vec3{0.0f, 1.0f, 0.0f}));
    CHECK(nearly(chain2[2], Vec3{0.0f, 2.0f, 0.0f}));
    for (const Vec3& p : chain2) {
        CHECK(isFiniteVec(p));
    }

    EXIT_WITH_RESULT();
}
