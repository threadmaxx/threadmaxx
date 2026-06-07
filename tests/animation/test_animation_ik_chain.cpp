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

float distance(const Vec3& a, const Vec3& b) noexcept {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    const float dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

}  // namespace

int main() {
    // 5-joint chain along x-axis, each bone length 1. Total reach = 4.
    std::vector<Vec3> chain = {
        Vec3{0.0f, 0.0f, 0.0f},
        Vec3{1.0f, 0.0f, 0.0f},
        Vec3{2.0f, 0.0f, 0.0f},
        Vec3{3.0f, 0.0f, 0.0f},
        Vec3{4.0f, 0.0f, 0.0f},
    };

    IKTarget t;
    t.position = Vec3{2.0f, 2.0f, 0.0f};  // distance from root ≈ 2.83 < 4
    t.maxIterations = 50;
    t.tolerance = 1e-3f;

    const IKSolveResult r = solveIK(std::span<Vec3>(chain), t);

    CHECK(r.converged);
    CHECK(r.iterations <= 50);
    CHECK(r.finalDistance <= 1e-3f);
    CHECK(distance(chain.back(), t.position) <= 1e-3f);

    // Bone lengths preserved within tolerance.
    for (std::size_t i = 0; i + 1 < chain.size(); ++i) {
        const float L = distance(chain[i], chain[i + 1]);
        CHECK(nearly(L, 1.0f, 1e-3f));
    }

    // Root never moves.
    CHECK(nearly(chain[0].x, 0.0f));
    CHECK(nearly(chain[0].y, 0.0f));
    CHECK(nearly(chain[0].z, 0.0f));

    EXIT_WITH_RESULT();
}
