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

}  // namespace

int main() {
    // Shoulder at origin, elbow bent up at (1, 0.5, 0), wrist at (2, 0, 0).
    // Bone lengths: upper = lower = sqrt(1.25) ≈ 1.118.
    // Target = (1, 0, 0). Distance shoulder→target = 1.
    // Law of cosines for elbow:
    //   cos(α) = (1.25 + 1 - 1.25) / (2 * 1.118 * 1) = 1 / 2.236 ≈ 0.4472
    //   sin(α) = sqrt(1 - 0.2) ≈ 0.8944
    // Elbow = shoulder + upper*(cos(α), sin(α), 0)
    //       = 1.118 * (0.4472, 0.8944, 0)
    //       ≈ (0.5, 1.0, 0)
    // Initial bend (elbow y=+0.5) disambiguates the two solutions
    // toward the +y half-plane.
    std::vector<Vec3> chain = {
        Vec3{0.0f, 0.0f, 0.0f},
        Vec3{1.0f, 0.5f, 0.0f},
        Vec3{2.0f, 0.0f, 0.0f},
    };

    IKTarget t;
    t.position = Vec3{1.0f, 0.0f, 0.0f};
    t.maxIterations = 64;
    t.tolerance = 1e-6f;

    const IKSolveResult r = solveIK(std::span<Vec3>(chain), t);

    // FABRIK should converge to the analytical elbow.
    CHECK(r.converged);
    CHECK(nearly(chain[1], Vec3{0.5f, 1.0f, 0.0f}));
    // And the wrist should be at the target.
    CHECK(nearly(chain[2], t.position));
    // Root never moves.
    CHECK(nearly(chain[0], Vec3{0.0f, 0.0f, 0.0f}));

    // Bone lengths are preserved:
    //   |E - S| ≈ 1.118
    //   |W - E| ≈ 1.118
    const float ls = std::sqrt((chain[1].x - chain[0].x) * (chain[1].x - chain[0].x) +
                               (chain[1].y - chain[0].y) * (chain[1].y - chain[0].y) +
                               (chain[1].z - chain[0].z) * (chain[1].z - chain[0].z));
    const float le = std::sqrt((chain[2].x - chain[1].x) * (chain[2].x - chain[1].x) +
                               (chain[2].y - chain[1].y) * (chain[2].y - chain[1].y) +
                               (chain[2].z - chain[1].z) * (chain[2].z - chain[1].z));
    CHECK(nearly(ls, std::sqrt(1.25f), 1e-3f));
    CHECK(nearly(le, std::sqrt(1.25f), 1e-3f));

    // Analytical 2-bone helper produces the same elbow (pole = +y).
    const Vec3 analytic = solve2BoneIK(Vec3{0.0f, 0.0f, 0.0f},
                                       Vec3{1.0f, 0.0f, 0.0f},
                                       Vec3{0.0f, 1.0f, 0.0f},
                                       std::sqrt(1.25f),
                                       std::sqrt(1.25f));
    CHECK(nearly(analytic, Vec3{0.5f, 1.0f, 0.0f}));

    EXIT_WITH_RESULT();
}
